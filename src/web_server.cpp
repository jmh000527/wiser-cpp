#include <../include/wiser/3rdparty/httplib.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <mutex>
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
#include "wiser/utils.h" // use Utils helpers

namespace fs = std::filesystem;

// 简易 Web 服务：
// - 提供静态文件托管
// - 提供搜索接口 /api/search
// - 提供多文件导入接口 /api/import（异步队列处理）
// - 提供任务查询接口 /api/tasks 与 /api/task

int main(int argc, char* argv[]) {
    // 初始化日志
    #ifdef NDEBUG
    spdlog::set_level(spdlog::level::info);
    #else
    spdlog::set_level(spdlog::level::debug);
    #endif
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");

    // 命令行参数：wiser_web [db_path]
    // - 若提供 db_path，则使用该路径；否则默认 ./wiser_web.db
    // - -h/--help 打印帮助
    if (argc > 1) {
        std::string arg1 = argv[1];
        if (arg1 == "-h" || arg1 == "--help") {
            std::cout << "Usage: wiser_web [db_file]\n"
                    << "  db_file: SQLite database file path (default: ./wiser_web.db)\n";
            return 0;
        }
    }

    // 初始化检索环境（默认或自定义数据库路径）
    std::string db_path = (argc > 1) ? std::string(argv[1]) : std::string("./wiser_web.db");
    const bool existed_before = fs::exists(db_path);
    spdlog::info("Starting wiser_web with DB: {} (existed: {})", db_path, existed_before ? "yes" : "no");

    wiser::WiserEnvironment env;
    if (!env.initialize(db_path)) {
        spdlog::error("Failed to initialize search engine.");
        return 1;
    }

    // 配置检索参数：若是新库则应用默认；若加载已有库则沿用库内保存的设置
    if (!existed_before) {
        env.setPhraseSearchEnabled(false);                  // 短语搜索（默认关闭）
        env.setTokenLength(2);                              // 令牌长度（默认 bi-gram）
        env.setBufferUpdateThreshold(2048);                 // 缓冲区阈值
        env.setCompressMethod(wiser::CompressMethod::NONE); // 关闭压缩
        env.setMaxIndexCount(-1);                           // 不限制索引条目

        spdlog::info("Initialized new DB with default settings. TokenLen={}, PhraseSearch={}, CompressMethod={}, BufferThreshold={}, MaxIndexCount={}.",
                     env.getTokenLength(), env.isPhraseSearchEnabled() ? "on" : "off",
                     static_cast<int>(env.getCompressMethod()) ? "golomb" : "none",
                     env.getBufferUpdateThreshold(), env.getMaxIndexCount());
    } else {
        spdlog::info("Loaded settings from existing DB. TokenLen={}, CompressMethod={}.",
                     env.getTokenLength(), static_cast<int>(env.getCompressMethod()) ? "golomb" : "none");
    }

    wiser::SearchEngine search_engine(&env);

    // 并发相关：写入需要串行化，任务表需要保护
    std::mutex index_mutex;                                  // 串行化对 env/db 的写操作
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
            wiser::web::Task tk; {
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
                    std::lock_guard<std::mutex> lock(index_mutex);
                    wiser::JsonLoader loader(&env);
                    success = loader.loadFromFile(tk.temp_path);
                } else if (ends_with(tk.filename, ".tsv")) {
                    std::lock_guard<std::mutex> lock(index_mutex);
                    wiser::TsvLoader loader(&env);
                    success = loader.loadFromFile(tk.temp_path, true);
                } else if (ends_with(tk.filename, ".xml")) {
                    std::lock_guard<std::mutex> lock(index_mutex);
                    success = env.getWikiLoader().loadFromFile(tk.temp_path);
                } else {
                    set_result(wiser::web::TaskStatus::Unsupported, "Unsupported file type");
                    std::error_code ec;
                    fs::remove(tk.temp_path, ec);
                    continue;
                } {
                    std::lock_guard<std::mutex> lock(index_mutex);
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

    // 启动工作线程（至少 2 个）
    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned worker_count = std::max(2u, hw ? hw : 2u);
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
    // 使用独立的路由注册函数替代内联定义的所有 HTTP 处理逻辑
    wiser::web::register_routes(svr, env, search_engine, index_mutex, tasks_mu, tasks, queue, seq);

    // 启动服务并监听
    spdlog::info("Starting server on http://localhost:54321 (press Ctrl+C to stop)");
    svr.listen("0.0.0.0", 54321);

    // 服务器退出：停止工作线程并回收
    shutting_down.store(true, std::memory_order_release);
    queue.stop();
    for (auto& th: workers) {
        if (th.joinable())
            th.join();
    }
    // 在退出前确保索引缓冲刷新（如果还有）
    {
        std::lock_guard<std::mutex> lock(index_mutex);
        env.flushIndexBuffer();
    }

    spdlog::info("Server stopped. Bye.");
    return 0;
}

// 移除末尾的匿名命名空间内的优雅关闭实现，改用 graceful.cpp.
