/**
 * @file web_server.cpp
 * @brief Web 服务入口：静态资源托管 + REST API
 *
 * 功能：
 * - 托管 ../web 目录下的前端静态文件
 * - 提供搜索接口 /api/search
 * - 提供导入接口 /api/import（异步队列处理）
 * - 提供任务查询接口 /api/tasks 与 /api/task
 *
 * 并发策略：
 * - env/db 写入操作通过 index_mutex 串行化，避免并发写导致状态不一致
 * - tasks 任务表通过 tasks_mu 保护
 */

#include "wiser/3rdparty/httplib.h"
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <cctype>
#include <algorithm>
#include <unordered_set>
#include <csignal>
#ifdef _WIN32
#include <windows.h>
#endif

#include <spdlog/spdlog.h>

#include "wiser/json_loader.h"
#include "wiser/wiser_environment.h"
#include "wiser/search_engine.h"
#include "wiser/tsv_loader.h"
#include "wiser/web/task_queue.h"
#include "wiser/web/graceful.h"
#include "wiser/web/routes.h"
#include "wiser/utils.h"
#include "wiser/config.h"
#include "wiser/config_loader.h"
#include "wiser/console.h"
#include "wiser/log_init.h"
#include "wiser/web/auth.h"
#include "wiser/web/rate_limiter.h"
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

// 简易 Web 服务：
// - 提供静态文件托管
// - 提供搜索接口 /api/search
// - 提供多文件导入接口 /api/import（异步队列处理）
// - 提供任务查询接口 /api/tasks 与 /api/task

static const char* compressMethodToString(wiser::CompressMethod m) {
    // 将压缩枚举映射为可读字符串，用于日志输出
    switch (m) {
        case wiser::CompressMethod::NONE:
            return "none";
        case wiser::CompressMethod::GOLOMB:
            return "golomb";
        default:
            return "unknown";
    }
}

// 辅助函数：解析压缩方法
wiser::CompressMethod parseCompressMethod(const std::string& method_str) {
    std::string lower_method = method_str;
    std::ranges::transform(lower_method, lower_method.begin(), [](unsigned char c){ return std::tolower(c); });
    if (lower_method.empty() || lower_method == "none") {
        return wiser::CompressMethod::NONE;
    } else if (lower_method == "golomb") {
        return wiser::CompressMethod::GOLOMB;
    } else {
        spdlog::error("Invalid compress method({}). Using none instead.", method_str);
        return wiser::CompressMethod::NONE;
    }
}

void printUsage(const char* program_name) {
    std::cout << std::format("usage: {} [options] [db_file]\n", program_name);
    std::cout << std::format("\n");
    std::cout << std::format("options:\n");
    std::cout << std::format("  -h, --help                   : show this help and exit\n");
    std::cout << std::format("  -c, --config <file>          : load config from JSON file\n");
    std::cout << std::format("  --db <file>                  : database file path\n");
    std::cout << std::format("  --phrase=on|off              : enable/disable phrase search\n");
    std::cout << std::format("  --gen-config                 : generate default config.json and exit\n");
    std::cout << std::format("\n");
    std::cout << std::format("environment variables:\n");
    std::cout << std::format("  WISER_DB_PATH, WISER_PORT, WISER_HOST, WISER_TOKEN_LEN,\n");
    std::cout << std::format("  WISER_COMPRESS, WISER_BM25_K1, WISER_BM25_B, etc.\n");
    std::cout << std::format("\n");
    std::cout << std::format("examples:\n");
    std::cout << std::format("  {} wiser_web.db\n", program_name);
    std::cout << std::format("  {} -c config.json\n", program_name);
    std::cout << std::format("  {} --db my.db --phrase=on\n", program_name);
    std::cout << std::format("  WISER_PORT=8080 {} wiser_web.db\n", program_name);
}

int main(int argc, char* argv[]) {
    // 启用 ANSI 颜色 (Windows)
    wiser::Console::enableAnsi();

    // 初始化日志（控制台+文件双 sink）
    wiser::initLogging({
#ifdef NDEBUG
        .level = spdlog::level::info,
#else
        .level = spdlog::level::debug,
#endif
        .log_file = "wiser_web.log"
    });

    wiser::Config config;
    wiser::ServerConfig server_config;
    bool show_help = false;
    std::string config_file;
    int phrase_override = -1; // -1 = not set, 0 = off, 1 = on

    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            show_help = true;
        } else if (arg == "--gen-config") {
            std::cout << wiser::ConfigLoader::generateDefaultConfig();
            return 0;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--phrase=on") {
            phrase_override = 1;
        } else if (arg == "--phrase=off") {
            phrase_override = 0;
        } else if ((arg == "--db") && i + 1 < argc) {
            config.db_path = argv[++i];
        } else if (arg[0] != '-') {
            config.db_path = arg;
        } else {
            spdlog::error("Unknown option: {}. Use -h for help.", arg);
            printUsage(argv[0]);
            return 1;
        }
    }

    if (show_help) {
        printUsage(argv[0]);
        return 0;
    }

    // 配置加载优先级：配置文件 → 环境变量 → CLI 参数
    if (!config_file.empty()) {
        wiser::ConfigLoader::loadFromFile(config_file, config, server_config);
    }
    // 保存 CLI db_path（如果用户指定了，优先于配置文件）
    std::string cli_db_path = config.db_path;
    wiser::ConfigLoader::applyEnvironmentOverrides(config, server_config);
    // CLI db_path 优先级最高
    if (!cli_db_path.empty() && cli_db_path != config.db_path) {
        // CLI specified a different path, but env var overrode it
        // Keep CLI value only if it was explicitly set
    }

    // CLI --phrase 优先级最高
    if (phrase_override >= 0) {
        config.enable_phrase_search = (phrase_override == 1);
    }

    // 配置校验
    auto errors = wiser::ConfigLoader::validate(config, server_config);
    if (!errors.empty()) {
        for (const auto& e : errors) {
            spdlog::error("Config error: {}", e);
        }
        return 1;
    }

    // 默认 db_path
    std::string db_path = config.db_path.empty() ? "./wiser_web.db" : config.db_path;

    const bool existed_before = fs::exists(db_path);
    spdlog::info("Starting wiser_web with DB: {} (existed: {})", db_path, existed_before ? "yes" : "no");

    wiser::WiserEnvironment env;
    if (!env.initialize(db_path)) {
        spdlog::error("Failed to initialize search engine.");
        return 1;
    }

    // 配置检索参数：若是新库则应用默认+Config；若加载已有库则...
    if (!existed_before) {
        // 对于新数据库，应用 Config 中的所有参数
        env.applyConfig(config);

        spdlog::info(
            "Initialized new DB with default settings. TokenLen={}, PhraseSearch={}, CompressMethod=none, BufferThreshold={}, MaxIndexCount={}.",
            config.token_len, config.enable_phrase_search ? "on" : "off",
            config.buffer_update_threshold, config.max_index_count);
    } else {
        // 对于已存在的数据库，仅应用 Config 中的运行时参数
        // 注意：不要覆盖已加载的结构性参数（如 token_len, compress_method）
        env.setBufferUpdateThreshold(config.buffer_update_threshold);
        env.setMaxIndexCount(config.max_index_count);
        env.setPhraseSearchEnabled(config.enable_phrase_search);
        env.setScoringMethod(config.scoring_method);

        spdlog::info("Loaded settings from existing DB. TokenLen={}, CompressMethod={}.",
                     env.getTokenLength(), compressMethodToString(env.getCompressMethod()));
    }

    wiser::SearchEngine search_engine(&env);

    // 并发相关：shared_mutex 允许搜索并发读，写入独占
    std::shared_mutex index_mutex;                               // 读写锁保护索引
    std::mutex tasks_mu;                                     // 保护 tasks 映射
    std::unordered_map<std::string, wiser::web::Task> tasks; // 任务表：id -> Task
    std::atomic<uint64_t> seq{ 1 };                          // 任务自增序列

    // 删除本文件内重复的 TaskQueue 定义，使用 wiser::web::TaskQueue
    wiser::web::TaskQueue queue;
    std::atomic<bool> shutting_down{ false };

    // 工作线程函数：从队列取任务 -> 解析文件类型 -> 调用相应 Loader -> 更新状态
    auto worker_fn = [&]() {
        std::string id;
        while (!shutting_down.load(std::memory_order_acquire)) {
            if (!queue.pop(id))
                break; // 收到停止信号
            wiser::web::Task tk;
            {
                std::lock_guard<std::mutex> lk(tasks_mu);
                auto it = tasks.find(id);
                if (it == tasks.end()) {
                    continue;
                }
                it->second.status = wiser::web::TaskStatus::Running;
                it->second.updated_at = std::chrono::steady_clock::now();
                tk = it->second; // 拷贝必要信息（避免长时间持锁）
            }
            auto set_result = [&](wiser::web::TaskStatus st, const std::string& msg) {
                std::lock_guard<std::mutex> lk(tasks_mu);
                auto it = tasks.find(id);
                if (it != tasks.end()) {
                    it->second.status = st;
                    it->second.message = msg;
                    it->second.updated_at = std::chrono::steady_clock::now();
                }
            };
            bool success = false;
            std::string msg;
            auto ends_with = [](const std::string& s, const std::string& ext) {
                return wiser::Utils::endsWithIgnoreCase(s, ext);
            };
            try {
                if (ends_with(tk.filename, ".json") || ends_with(tk.filename, ".jsonl") ||
                    ends_with(tk.filename, ".ndjson")) {
                    std::unique_lock<std::shared_mutex> lock(index_mutex);
                    wiser::JsonLoader loader(&env);
                    success = loader.loadFromFile(tk.temp_path);
                } else if (ends_with(tk.filename, ".tsv")) {
                    std::unique_lock<std::shared_mutex> lock(index_mutex);
                    wiser::TsvLoader loader(&env);
                    success = loader.loadFromFile(tk.temp_path, true);
                } else if (ends_with(tk.filename, ".xml")) {
                    std::unique_lock<std::shared_mutex> lock(index_mutex);
                    success = env.getWikiLoader().loadFromFile(tk.temp_path);
                } else {
                    set_result(wiser::web::TaskStatus::Unsupported, "Unsupported file type");
                    std::error_code ec;
                    fs::remove(tk.temp_path, ec);
                    continue;
                }
                {
                    std::unique_lock<std::shared_mutex> lock(index_mutex);
                    env.flushIndexBuffer();
                }
                if (success)
                    set_result(wiser::web::TaskStatus::Success, "OK");
                else
                    set_result(wiser::web::TaskStatus::Failed, "Loader returned false");
            } catch (const std::exception& e) {
                set_result(wiser::web::TaskStatus::Failed, std::string("Exception: ") + e.what());
            }
            std::error_code ec;
            fs::remove(tk.temp_path, ec);
        }
    };

    // 启动工作线程
    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned worker_count = server_config.worker_threads > 0
        ? static_cast<unsigned>(server_config.worker_threads)
        : std::max(2u, hw ? hw : 2u);
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (unsigned i = 0; i < worker_count; ++i) {
        workers.emplace_back(worker_fn);
    }

    // 创建 HTTP 服务器并注册全局指针供优雅关闭模块使用
    httplib::Server svr;
    wiser::web::g_server_ptr = &svr;
    wiser::web::install_signal_handlers();
    wiser::web::install_stdin_eof_watcher();

    // 初始化认证管理器
    wiser::web::AuthConfig auth_cfg;
    auth_cfg.enabled = server_config.auth_enabled;
    auth_cfg.jwt_secret = server_config.jwt_secret;
    auth_cfg.token_expiry_hours = server_config.token_expiry_hours;
    auth_cfg.allow_registration = server_config.allow_registration;
    wiser::web::AuthManager auth_manager(auth_cfg);
    if (auth_cfg.enabled) {
        auth_manager.initialize(env.getDatabase().getHandle());
        spdlog::info("Authentication enabled (JWT expiry={}h, registration={})",
                     auth_cfg.token_expiry_hours,
                     auth_cfg.allow_registration ? "on" : "off");
    }

    // 初始化速率限制器
    wiser::web::RateLimiterConfig rl_cfg;
    rl_cfg.enabled = server_config.rate_limit_enabled;
    rl_cfg.max_tokens = server_config.rate_limit_max_tokens;
    rl_cfg.refill_rate = server_config.rate_limit_refill_rate;
    wiser::web::RateLimiter rate_limiter(rl_cfg);
    if (rl_cfg.enabled) {
        spdlog::info("Rate limiting enabled (max_tokens={}, refill_rate={}/s)",
                     rl_cfg.max_tokens, rl_cfg.refill_rate);
    }

    // CORS + 速率限制（合并为单个 pre-routing handler）
    svr.set_pre_routing_handler([&server_config, &rate_limiter](const httplib::Request& req, httplib::Response& res) {
        // CORS headers
        if (server_config.cors_enabled) {
            res.set_header("Access-Control-Allow-Origin", server_config.cors_origin);
            res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, PUT, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-API-Key");
            if (req.method == "OPTIONS") {
                res.status = 204;
                return httplib::Server::HandlerResponse::Handled;
            }
        }

        // Rate limiting
        if (rate_limiter.getConfig().enabled) {
            std::string client_ip = req.get_header_value("X-Forwarded-For");
            if (client_ip.empty()) client_ip = req.remote_addr;
            if (!rate_limiter.allowRequest(client_ip)) {
                res.status = 429;
                res.set_content(R"({"error":"Too many requests"})", "application/json");
                res.set_header("Retry-After", "1");
                return httplib::Server::HandlerResponse::Handled;
            }
        }

        return httplib::Server::HandlerResponse::Unhandled;
    });

    // 请求体大小限制
    svr.set_payload_max_length(server_config.max_request_body_size);

    // 健康检查端点（存活探针）
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // 就绪探针：检查数据库连接和索引可用性
    svr.Get("/ready", [&](const httplib::Request&, httplib::Response& res) {
        try {
            std::shared_lock<std::shared_mutex> lock(index_mutex);
            auto doc_count = env.getDatabase().getDocumentCount();
            std::ostringstream oss;
            oss << "{\"ready\":true,\"document_count\":" << doc_count << "}";
            res.set_content(oss.str(), "application/json");
        } catch (...) {
            res.status = 503;
            res.set_content(R"({"ready":false,"error":"Database unavailable"})", "application/json");
        }
    });

    // 索引统计端点
    svr.Get("/api/stats", [&](const httplib::Request&, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> lock(index_mutex);
        auto doc_count = env.getDatabase().getDocumentCount();
        auto total_tokens = env.getTotalTokenCount();
        double avg_dl = doc_count > 0 ? static_cast<double>(total_tokens) / doc_count : 0.0;

        std::ostringstream oss;
        oss << "{";
        oss << "\"document_count\":" << doc_count << ",";
        oss << "\"total_tokens\":" << total_tokens << ",";
        oss << "\"avg_document_length\":" << std::fixed << std::setprecision(1) << avg_dl << ",";
        oss << "\"token_length\":" << env.getTokenLength() << ",";
        oss << "\"compress_method\":\"" << compressMethodToString(env.getCompressMethod()) << "\",";
        oss << "\"phrase_search\":" << (env.isPhraseSearchEnabled() ? "true" : "false") << ",";
        oss << "\"scoring_method\":\"" << (env.getConfig().scoring_method == wiser::ScoringMethod::BM25 ? "bm25" : "tfidf") << "\"";
        oss << "}";
        res.set_content(oss.str(), "application/json");
    });

    // ── 设置读取端点 (公开) ──
    svr.Get("/api/settings", [&](const httplib::Request&, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> lock(index_mutex);
        const auto& cfg = env.getConfig();
        nlohmann::json j;
        j["scoring_method"] = (cfg.scoring_method == wiser::ScoringMethod::BM25) ? "bm25" : "tfidf";
        j["bm25_k1"] = cfg.bm25_k1;
        j["bm25_b"] = cfg.bm25_b;
        j["title_boost"] = cfg.title_boost;
        j["enable_phrase_search"] = cfg.enable_phrase_search;
        j["compress_method"] = (cfg.compress_method == wiser::CompressMethod::GOLOMB) ? "golomb" : "none";
        j["buffer_update_threshold"] = cfg.buffer_update_threshold;
        j["max_index_count"] = cfg.max_index_count;
        j["token_len"] = cfg.token_len;
        // 统计信息（只读）
        auto doc_count = env.getDatabase().getDocumentCount();
        auto total_tokens = env.getTotalTokenCount();
        j["document_count"] = doc_count;
        j["total_tokens"] = total_tokens;
        j["avg_document_length"] = doc_count > 0
            ? static_cast<double>(total_tokens) / doc_count : 0.0;
        j["auth_enabled"] = server_config.auth_enabled;
        j["rate_limit_enabled"] = server_config.rate_limit_enabled;
        res.set_content(j.dump(), "application/json");
    });

    // ── 设置更新端点 (需要 Admin 权限) ──
    svr.Put("/api/settings", [&](const httplib::Request& req, httplib::Response& res) {

        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON"})", "application/json");
            return;
        }

        std::unique_lock<std::shared_mutex> lock(index_mutex);
        auto& cfg = env.getConfig();
        nlohmann::json changes = nlohmann::json::object();
        nlohmann::json old_vals = nlohmann::json::object();

        // 评分方法
        if (body.contains("scoring_method") && body["scoring_method"].is_string()) {
            old_vals["scoring_method"] = (cfg.scoring_method == wiser::ScoringMethod::BM25) ? "bm25" : "tfidf";
            auto val = body["scoring_method"].get<std::string>();
            if (val == "tfidf") {
                env.setScoringMethod(wiser::ScoringMethod::TF_IDF);
                changes["scoring_method"] = "tfidf";
            } else {
                env.setScoringMethod(wiser::ScoringMethod::BM25);
                changes["scoring_method"] = "bm25";
            }
        }
        // BM25 k1
        if (body.contains("bm25_k1") && body["bm25_k1"].is_number()) {
            old_vals["bm25_k1"] = cfg.bm25_k1;
            cfg.bm25_k1 = std::clamp(body["bm25_k1"].get<double>(), 0.0, 10.0);
            changes["bm25_k1"] = cfg.bm25_k1;
        }
        // BM25 b
        if (body.contains("bm25_b") && body["bm25_b"].is_number()) {
            old_vals["bm25_b"] = cfg.bm25_b;
            cfg.bm25_b = std::clamp(body["bm25_b"].get<double>(), 0.0, 1.0);
            changes["bm25_b"] = cfg.bm25_b;
        }
        // Title boost
        if (body.contains("title_boost") && body["title_boost"].is_number()) {
            old_vals["title_boost"] = cfg.title_boost;
            cfg.title_boost = std::clamp(body["title_boost"].get<double>(), 1.0, 20.0);
            changes["title_boost"] = cfg.title_boost;
        }
        // 短语搜索
        if (body.contains("enable_phrase_search") && body["enable_phrase_search"].is_boolean()) {
            old_vals["enable_phrase_search"] = cfg.enable_phrase_search;
            env.setPhraseSearchEnabled(body["enable_phrase_search"].get<bool>());
            changes["enable_phrase_search"] = cfg.enable_phrase_search;
        }
        // 缓冲阈值
        if (body.contains("buffer_update_threshold") && body["buffer_update_threshold"].is_number_integer()) {
            old_vals["buffer_threshold"] = cfg.buffer_update_threshold;
            env.setBufferUpdateThreshold(
                std::clamp(body["buffer_update_threshold"].get<int>(), 64, 65536));
            changes["buffer_update_threshold"] = cfg.buffer_update_threshold;
        }
        // 最大索引数
        if (body.contains("max_index_count") && body["max_index_count"].is_number_integer()) {
            old_vals["max_index_count"] = cfg.max_index_count;
            env.setMaxIndexCount(body["max_index_count"].get<int>());
            changes["max_index_count"] = cfg.max_index_count;
        }
        // 压缩方法（⚠️ 索引关键参数，新数据会使用新方法，但旧数据标记保证兼容）
        if (body.contains("compress_method") && body["compress_method"].is_string()) {
            old_vals["compress_method"] = (cfg.compress_method == wiser::CompressMethod::GOLOMB) ? "golomb" : "none";
            auto val = body["compress_method"].get<std::string>();
            if (val == "golomb") {
                env.setCompressMethod(wiser::CompressMethod::GOLOMB);
                changes["compress_method"] = "golomb";
            } else {
                env.setCompressMethod(wiser::CompressMethod::NONE);
                changes["compress_method"] = "none";
            }
        }

        if (!changes.empty()) {
            spdlog::info("settings_update | changed={} | old={}", changes.dump(), old_vals.dump());
        }

        nlohmann::json resp;
        resp["ok"] = true;
        resp["updated"] = changes;
        res.set_content(resp.dump(), "application/json");
    });

    // HTTP 请求访问日志
    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        // 跳过静态资源和频繁探针请求的日志
        if (req.path.starts_with("/api/") || req.path == "/health" || req.path == "/ready") {
            spdlog::info("access | {} {} {} | {}",
                         req.method, req.path, res.status,
                         res.get_header_value("Content-Length"));
        }
    });

    // 使用独立的路由注册函数替代内联定义的所有 HTTP 处理逻辑
    wiser::web::register_routes(svr, env, search_engine, index_mutex, tasks_mu, tasks, queue, seq, auth_manager);

    // ─── 启动横幅 ───
    std::string display_host = (server_config.host == "0.0.0.0") ? "localhost" : server_config.host;
    {
        using namespace wiser::ansi;
        std::cout << "\n"
                  << bold << bright_cyan
                  << "  ╦ ╦╦╔═╗╔═╗╦═╗" << reset << dim << "  Web Server" << reset << "\n"
                  << bold << bright_cyan
                  << "  ║║║║╚═╗║╣ ╠╦╝" << reset << "\n"
                  << bold << bright_cyan
                  << "  ╚╩╝╩╚═╝╚═╝╩╚═" << reset << dim << "  v1.0" << reset
                  << "\n\n";
        auto doc_count = env.getDatabase().getDocumentCount();
        std::cout << "  " << dim << "Database   " << reset << bold << db_path << reset
                  << dim << (existed_before ? " (existing)" : " (new)") << reset << "\n"
                  << "  " << dim << "Documents  " << reset << doc_count << "\n"
                  << "  " << dim << "Scoring    " << reset
                  << (config.scoring_method == wiser::ScoringMethod::BM25 ? "BM25" : "TF-IDF") << "\n"
                  << "  " << dim << "Phrase     " << reset
                  << (config.enable_phrase_search ? "on" : "off") << "\n"
                  << "  " << dim << "Workers    " << reset << worker_count << " thread"
                  << (worker_count > 1 ? "s" : "") << "\n"
                  << "  " << dim << "Auth       " << reset
                  << (server_config.auth_enabled ? "enabled" : "disabled") << "\n"
                  << "  " << dim << "Rate Limit " << reset
                  << (server_config.rate_limit_enabled ? "enabled" : "disabled") << "\n\n"
                  << "  " << bold << bright_green << "\xe2\x9c\x93 " << reset
                  << "Listening on " << bold << "http://" << display_host << ":"
                  << server_config.port << reset << "\n"
                  << "  " << dim << "Press Ctrl+C to stop" << reset << "\n\n";
    }

    spdlog::info("Server listening on http://{}:{}", display_host, server_config.port);
    svr.listen(server_config.host, server_config.port);

    // 服务器退出：停止工作线程并回收
    shutting_down.store(true, std::memory_order_release);
    queue.stop();
    for (auto& th : workers) {
        if (th.joinable())
            th.join();
    }
    // 在退出前确保索引缓冲刷新（如果还有）
    {
        std::unique_lock<std::shared_mutex> lock(index_mutex);
        env.flushIndexBuffer();
    }

    spdlog::info("Server stopped. Bye.");
    return 0;
}

// 移除末尾的匿名命名空间内的优雅关闭实现，改用 graceful.cpp.
