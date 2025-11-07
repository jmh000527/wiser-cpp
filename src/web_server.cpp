#include <httplib.h>
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

#include <spdlog/spdlog.h>

#include "wiser/json_loader.h"
#include "wiser/wiser_environment.h"
#include "wiser/search_engine.h"
#include "wiser/tsv_loader.h"

namespace fs = std::filesystem;

// 简易 Web 服务：
// - 提供静态文件托管
// - 提供搜索接口 /api/search
// - 提供多文件导入接口 /api/import（异步队列处理）
// - 提供任务查询接口 /api/tasks 与 /api/task

// JSON 字符串转义工具，确保响应中的字符串符合 JSON 规范
static std::string json_escape(const std::string& s) {
    std::ostringstream o;
    for (auto c = s.cbegin(); c != s.cend(); ++c) {
        switch (*c) {
            case '"':
                o << "\\\"";
                break;
            case '\\':
                o << "\\\\";
                break;
            case '\b':
                o << "\\b";
                break;
            case '\f':
                o << "\\f";
                break;
            case '\n':
                o << "\\n";
                break;
            case '\r':
                o << "\\r";
                break;
            case '\t':
                o << "\\t";
                break;
            default:
                if ('\x00' <= *c && *c <= '\x1f') {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)*c << std::dec;
                } else {
                    o << *c;
                }
        }
    }
    return o.str();
}

// 任务系统
enum class TaskStatus { Queued, Running, Success, Failed, Unsupported };

// 任务状态转字符串（用于 JSON 输出）
static const char* status_to_string(TaskStatus st) {
    switch (st) {
        case TaskStatus::Queued:
            return "queued";
        case TaskStatus::Running:
            return "running";
        case TaskStatus::Success:
            return "success";
        case TaskStatus::Failed:
            return "failed";
        case TaskStatus::Unsupported:
            return "unsupported";
    }
    return "unknown";
}

// 单个导入任务的元数据
struct Task {
    std::string id;                          // 任务 ID（十六进制）
    std::string field_key;                   // 表单字段名（当前未使用）
    std::string filename;                    // 原始文件名
    std::string temp_path;                   // 临时保存路径
    TaskStatus status{ TaskStatus::Queued }; // 当前状态
    std::string message;                     // 状态信息或错误信息
    std::chrono::steady_clock::time_point created_at{ std::chrono::steady_clock::now() };
    std::chrono::steady_clock::time_point updated_at{ std::chrono::steady_clock::now() };
};

// 简单的线程安全任务队列（基于条件变量）
class TaskQueue {
public:
    // 入队一个任务 ID
    void push(const std::string& id) { {
            std::lock_guard<std::mutex> lk(m_);
            q_.push_back(id);
        }
        cv_.notify_one();
    }

    // 阻塞出队：无任务时等待，返回 false 表示 stop 且队列已空
    bool pop(std::string& out_id) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] {
            return stop_ || !q_.empty();
        });
        if (stop_ && q_.empty())
            return false;
        out_id = std::move(q_.front());
        q_.pop_front();
        return true;
    }

    // 停止队列并唤醒所有等待线程
    void stop() { {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::string> q_;
    bool stop_{ false };
};

int main(int argc, char* argv[]) {
    // 初始化日志
    spdlog::set_level(spdlog::level::info);
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
    std::mutex index_mutex;                      // 串行化对 env/db 的写操作
    std::mutex tasks_mu;                         // 保护 tasks 映射
    std::unordered_map<std::string, Task> tasks; // 任务表：id -> Task
    std::atomic<uint64_t> seq{ 1 };              // 任务自增序列

    TaskQueue queue;
    std::atomic<bool> shutting_down{ false };

    // 生成下一个任务 ID（16 位十六进制字符串）
    auto next_id = [&]() {
        uint64_t v = seq.fetch_add(1, std::memory_order_relaxed);
        std::ostringstream oss;
        oss << std::hex << std::setw(16) << std::setfill('0') << v;
        return oss.str();
    };

    // 工作线程函数：从队列取任务 -> 解析文件类型 -> 调用相应 Loader -> 更新状态
    auto worker_fn = [&]() {
        std::string id;
        while (!shutting_down.load(std::memory_order_acquire)) {
            if (!queue.pop(id))
                break; // 收到停止信号

            // 查询任务并标记为运行中
            Task tk; {
                std::lock_guard<std::mutex> lk(tasks_mu);
                auto it = tasks.find(id);
                if (it == tasks.end()) {
                    continue;
                }
                it->second.status = TaskStatus::Running;
                it->second.updated_at = std::chrono::steady_clock::now();
                tk = it->second; // 拷贝必要信息（避免长时间持锁）
            }

            // 更新结果的便捷函数
            auto set_result = [&](TaskStatus st, const std::string& msg) {
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

            // 后缀匹配（不区分大小写）
            auto ends_with = [](const std::string& s, const std::string& ext) {
                if (s.size() < ext.size())
                    return false;
                return std::equal(ext.rbegin(), ext.rend(), s.rbegin(),
                                  [](char a, char b) {
                                      return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
                                  });
            };

            try {
                // 根据文件类型选择加载器，并在写入索引时加锁
                if (ends_with(tk.filename, ".json") || ends_with(tk.filename, ".jsonl") ||
                    ends_with(tk.filename, ".ndjson")) {
                    std::lock_guard<std::mutex> lock(index_mutex);
                    wiser::JsonLoader loader(&env);
                    success = loader.loadFromFile(tk.temp_path);
                } else if (ends_with(tk.filename, ".tsv")) {
                    std::lock_guard<std::mutex> lock(index_mutex);
                    wiser::TsvLoader loader(&env);
                    success = loader.loadFromFile(tk.temp_path, /*has_header=*/true);
                } else if (ends_with(tk.filename, ".xml")) {
                    std::lock_guard<std::mutex> lock(index_mutex);
                    // 通过环境提供的 Wikipedia XML 加载器
                    success = env.getWikiLoader().loadFromFile(tk.temp_path);
                } else {
                    // 不支持的文件类型
                    set_result(TaskStatus::Unsupported, "Unsupported file type");
                    // 清理临时文件并继续
                    std::error_code ec;
                    fs::remove(tk.temp_path, ec);
                    continue;
                } {
                    // 小文件时确保刷新缓冲到索引
                    std::lock_guard<std::mutex> lock(index_mutex);
                    env.flushIndexBuffer();
                }

                if (success) {
                    set_result(TaskStatus::Success, "OK");
                } else {
                    set_result(TaskStatus::Failed, "Loader returned false");
                }
            } catch (const std::exception& e) {
                set_result(TaskStatus::Failed, std::string("Exception: ") + e.what());
            }

            // 无论成功失败，清理临时文件
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

    // 创建 HTTP 服务器
    httplib::Server svr;

    // 静态资源托管：将根路径挂载到 ../web
    if (fs::exists("../web")) {
        svr.set_mount_point("/", "../web");
    } else {
        spdlog::warn("Web directory '../web' not found, static files will not be served.");
    }

    // 搜索接口：GET /api/search?q=xxx
    svr.Get("/api/search", [&](const httplib::Request& req, httplib::Response& res) {
        auto query = req.get_param_value("q");
        if (query.empty()) {
            res.status = 400;
            res.set_content(R"({"error": "Query parameter 'q' is required"})", "application/json");
            return;
        }

        // Helper: ignore logic (mirrors tokenizer)
        auto isIgnoredCharLocal = [](char32_t ch) {
            if (ch <= 127) {
                return std::isspace(static_cast<unsigned char>(ch)) || std::ispunct(static_cast<unsigned char>(ch));
            }
            switch (ch) {
                case 0x3000: case 0x3001: case 0x3002: case 0xFF08: case 0xFF09: case 0xFF01: case 0xFF0C:
                case 0xFF1A: case 0xFF1B: case 0xFF1F: case 0xFF3B: case 0xFF3D: case 0x201C: case 0x201D:
                case 0x2018: case 0x2019:
                    return true;
                default:
                    return false;
            }
        };
        // Tokenize query into N-gram tokens (fixed length env.getTokenLength())
        auto tokenizeQuery = [&](const std::string& q, int n) {
            std::vector<std::string> tokens;
            // Convert UTF-8 to UTF-32 via simple iteration (assuming valid UTF-8)
            std::u32string u32; u32.reserve(q.size());
            for (size_t i = 0; i < q.size();) {
                unsigned char c = static_cast<unsigned char>(q[i]);
                char32_t cp = 0; size_t adv = 1;
                if (c < 0x80) { cp = c; }
                else if ((c >> 5) == 0x6 && i + 1 < q.size()) { cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(q[i+1]) & 0x3F); adv = 2; }
                else if ((c >> 4) == 0xE && i + 2 < q.size()) { cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(q[i+1]) & 0x3F) << 6) | (static_cast<unsigned char>(q[i+2]) & 0x3F); adv = 3; }
                else if ((c >> 3) == 0x1E && i + 3 < q.size()) { cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(q[i+1]) & 0x3F) << 12) | ((static_cast<unsigned char>(q[i+2]) & 0x3F) << 6) | (static_cast<unsigned char>(q[i+3]) & 0x3F); adv = 4; }
                else { cp = c; }
                u32.push_back(cp); i += adv;
            }
            // Collect runs of non-ignored chars (lowercasing ASCII)
            std::vector<std::u32string> runs; std::u32string cur;
            for (auto cp : u32) {
                if (isIgnoredCharLocal(cp)) {
                    if (!cur.empty()) { runs.push_back(cur); cur.clear(); }
                } else {
                    if (cp <= 127) cp = static_cast<char32_t>(std::tolower(static_cast<unsigned char>(cp)));
                    cur.push_back(cp);
                }
            }
            if (!cur.empty()) runs.push_back(cur);
            // Generate sliding windows of length n
            for (auto& run : runs) {
                if (run.size() < static_cast<size_t>(n)) continue;
                for (size_t i = 0; i + static_cast<size_t>(n) <= run.size(); ++i) {
                    std::u32string ng = run.substr(i, static_cast<size_t>(n));
                    // Convert back to UTF-8
                    std::string utf8; utf8.reserve(ng.size());
                    for (auto cp : ng) {
                        if (cp < 0x80) utf8.push_back(static_cast<char>(cp));
                        else if (cp < 0x800) {
                            utf8.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
                            utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else if (cp < 0x10000) {
                            utf8.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
                            utf8.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            utf8.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
                            utf8.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                            utf8.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                    }
                    tokens.push_back(std::move(utf8));
                }
            }
            // Deduplicate preserving order
            std::vector<std::string> unique; unique.reserve(tokens.size());
            std::unordered_set<std::string> seen;
            for (auto& t : tokens) if (!seen.count(t)) { seen.insert(t); unique.push_back(t); }
            return unique;
        };

        const int n = env.getTokenLength();
        auto query_tokens = tokenizeQuery(query, n);

        // Perform search
        std::vector<std::pair<wiser::DocId, double>> results; {
            results = search_engine.searchWithResults(query);
        }

        // Lowercased query tokens already; build response
        std::ostringstream response;
        response << "[";
        bool first = true;
        for (const auto& item : results) {
            auto doc_id = item.first;
            auto score = item.second;
            std::string title = env.getDatabase().getDocumentTitle(doc_id);
            std::string body = env.getDatabase().getDocumentBody(doc_id);
            std::string body_snippet = body.substr(0, std::min<size_t>(200, body.size()));

            // Build lowercase copies for ASCII matching
            auto toLowerAscii = [](std::string s) {
                for (char& c : s) if (static_cast<unsigned char>(c) < 128) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return s;
            };
            std::string title_l = toLowerAscii(title);
            std::string snippet_l = toLowerAscii(body_snippet);

            std::vector<std::string> matched;
            matched.reserve(query_tokens.size());
            for (const auto& tok : query_tokens) {
                // ASCII tokens already lowercased; for search use same
                if (title_l.find(tok) != std::string::npos || snippet_l.find(tok) != std::string::npos) {
                    matched.push_back(tok);
                }
            }
            // Remove duplicates (should not exist if query_tokens unique)
            if (!first) response << ","; first = false;
            response << "{";
            response << "\"id\": " << doc_id << ",";
            response << "\"title\": \"" << json_escape(title) << "\",";
            response << "\"body\": \"" << json_escape(body_snippet) << "\",";
            response << "\"score\": " << score << ",";
            response << "\"matched_tokens\": [";
            for (size_t i = 0; i < matched.size(); ++i) {
                if (i) response << ",";
                response << "\"" << json_escape(matched[i]) << "\"";
            }
            response << "]";
            response << "}";
        }
        response << "]";
        res.set_content(response.str(), "application/json");
    });

    // 导入接口：POST /api/import，支持 multipart/form-data 多文件上传
    // 每个文件转为一个队列任务，后端异步处理，返回任务 ID 列表
    svr.Post("/api/import", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.is_multipart_form_data()) {
            res.status = 400;
            res.set_content(R"({"error": "Content-Type must be multipart/form-data"})", "application/json");
            return;
        }

        // 收集所有上传文件
        std::vector<httplib::FormData> all_files;
        all_files.reserve(req.form.files.size());
        for (const auto& kv: req.form.files) {
            all_files.push_back(kv.second);
        }

        if (all_files.empty()) {
            res.status = 400;
            res.set_content(R"({"error": "No files uploaded"})", "application/json");
            return;
        }

        std::vector<std::string> ids;
        ids.reserve(all_files.size());

        for (const auto& file: all_files) {
            // 分配任务 ID 与临时文件路径
            std::string id = next_id();
            std::string safe_name = file.filename.empty() ? std::string("unnamed") : file.filename;
            std::string temp_path;
            temp_path.reserve(6 + id.size() + 1 + safe_name.size());
            temp_path.append("temp_").append(id).append("_").append(safe_name);

            // 写入临时文件（原始二进制）
            std::ofstream out(temp_path, std::ios::binary);
            out.write(file.content.data(), static_cast<std::streamsize>(file.content.size()));
            out.close();

            // 注册任务到任务表
            Task tk;
            tk.id = id;
            tk.field_key = ""; // 当前未记录字段名
            tk.filename = safe_name;
            tk.temp_path = temp_path;
            tk.status = TaskStatus::Queued;
            tk.message.clear(); {
                std::lock_guard<std::mutex> lk(tasks_mu);
                tasks.emplace(id, std::move(tk));
            }
            // 入队
            queue.push(id);
            ids.push_back(std::move(id));
        }

        // 返回已接受的任务 ID 列表
        std::ostringstream oss;
        oss << "{\"accepted\": " << ids.size() << ", \"task_ids\": [";
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i)
                oss << ",";
            oss << "\"" << ids[i] << "\"";
        }
        oss << "]}";
        res.set_content(oss.str(), "application/json");
    });

    // 查询所有任务状态：GET /api/tasks
    svr.Get("/api/tasks", [&](const httplib::Request&, httplib::Response& res) {
        // 快照拷贝，避免长时间持锁与排序冲突
        std::vector<Task> snapshot; {
            std::lock_guard<std::mutex> lk(tasks_mu);
            snapshot.reserve(tasks.size());
            for (const auto& kv: tasks)
                snapshot.push_back(kv.second);
        }
        // 按创建时间排序
        std::sort(snapshot.begin(), snapshot.end(), [](const Task& a, const Task& b) {
            return a.created_at < b.created_at;
        });

        // 构造 JSON 数组
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < snapshot.size(); ++i) {
            const auto& t = snapshot[i];
            if (i)
                oss << ",";
            oss << "{\"id\":\"" << t.id << "\",";
            oss << "\"filename\":\"" << json_escape(t.filename) << "\",";
            oss << "\"status\":\"" << status_to_string(t.status) << "\",";
            if (!t.message.empty()) {
                oss << "\"message\":\"" << json_escape(t.message) << "\"";
            } else {
                oss << "\"message\":\"\"";
            }
            oss << "}";
        }
        oss << "]";
        res.set_content(oss.str(), "application/json");
    });

    // 查询单个任务状态：GET /api/task?id=<task_id>
    svr.Get("/api/task", [&](const httplib::Request& req, httplib::Response& res) {
        auto id = req.get_param_value("id");
        if (id.empty()) {
            res.status = 400;
            res.set_content(R"({"error": "Query parameter 'id' is required"})", "application/json");
            return;
        }
        Task t;
        bool found = false; {
            std::lock_guard<std::mutex> lk(tasks_mu);
            auto it = tasks.find(id);
            if (it != tasks.end()) {
                t = it->second;
                found = true;
            }
        }
        if (!found) {
            res.status = 404;
            res.set_content(R"({"error": "Task not found"})", "application/json");
            return;
        }
        std::ostringstream oss;
        oss << "{\"id\":\"" << t.id << "\",";
        oss << "\"filename\":\"" << json_escape(t.filename) << "\",";
        oss << "\"status\":\"" << status_to_string(t.status) << "\",";
        oss << "\"message\":\"" << json_escape(t.message) << "\"}";
        res.set_content(oss.str(), "application/json");
    });

    // 启动服务并监听
    spdlog::info("Starting server on http://localhost:54321");
    svr.listen("0.0.0.0", 54321);

    // 服务器退出：停止工作线程并回收
    shutting_down.store(true, std::memory_order_release);
    queue.stop();
    for (auto& th: workers) {
        if (th.joinable())
            th.join();
    }

    return 0;
}
