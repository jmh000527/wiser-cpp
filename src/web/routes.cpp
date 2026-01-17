/**
 * @file routes.cpp
 * @brief Web API 路由注册实现
 *
 * 负责将各个 HTTP 路由绑定到 httplib::Server：
 * - /api/search：查询检索
 * - /api/import：导入文件（异步任务）
 * - /api/tasks、/api/task：任务列表与任务详情
 *
 * 说明：
 * - 该文件手动拼装 JSON，输出前会对字符串进行转义，避免破坏 JSON 格式
 */

#include "wiser/web/routes.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <unordered_set>
#include <ranges>
#include <fstream>
#include "wiser/wiser_environment.h"
#include "wiser/search_engine.h"
#include "wiser/utils.h"
#include "wiser/3rdparty/httplib.h"

namespace fs = std::filesystem;

namespace wiser::web {
    // 生成下一个全局唯一 ID（十六进制 16 位，不足补 0），使用原子递增，内存序为 relaxed
    static std::string next_id(std::atomic<uint64_t>& seq) {
        std::uint64_t v = seq.fetch_add(1, std::memory_order_relaxed);
        std::ostringstream oss;
        oss << std::hex << std::setw(16) << std::setfill('0') << v;
        return oss.str();
    }

    // 注册所有 HTTP 路由
    void register_routes(httplib::Server& svr,
                         wiser::WiserEnvironment& env,
                         wiser::SearchEngine& search_engine,
                         std::mutex& index_mutex,
                         std::mutex& tasks_mu,
                         TaskTable& tasks,
                         TaskQueue& queue,
                         std::atomic<uint64_t>& seq) {
        // 静态文件目录挂载（前端页面）
        if (fs::exists("../web")) { svr.set_mount_point("/", "../web"); } else {
            spdlog::warn("Web directory '../web' not found, static files will not be served.");
        }

        // 搜索接口：/api/search?q=...
        // 返回：命中文档列表（含：id/title/body/score/matched_tokens）
        svr.Get("/api/search", [&](const httplib::Request& req, httplib::Response& res) {
            // 加锁保护索引读取和运行时配置修改，避免与 indexing 线程冲突
            std::lock_guard<std::mutex> lock(index_mutex);

            auto query = req.get_param_value("q");
            if (query.empty()) {
                res.status = 400;
                res.set_content(R"({"error": "Query parameter 'q' is required"})", "application/json");
                return;
            }

            // 处理运行时参数
            auto phrase_param = req.get_param_value("phrase");
            if (!phrase_param.empty()) {
                env.setPhraseSearchEnabled(phrase_param == "1");
            } else {
                // 如果未传，可以选择重置为默认，或者保持原有状态。
                // 考虑到前端现在总是传值，这里简单处理为若不传则设为 false
                env.setPhraseSearchEnabled(false);
            }

            auto scoring_param = req.get_param_value("scoring");
            if (scoring_param == "tfidf") {
                env.setScoringMethod(wiser::ScoringMethod::TF_IDF);
            } else {
                // Default to BM25
                env.setScoringMethod(wiser::ScoringMethod::BM25);
            }

            const int n = env.getTokenLength();                       // 查询 token 长度配置
            auto query_tokens = Utils::tokenizeQueryTokens(query, n); // 分词（已假设做过归一化）
            std::vector<std::pair<wiser::DocId, double>> results = search_engine.searchWithResults(query);

            // 局部 lambda：复制并转小写（只处理 ASCII）
            auto lowerCopy = [](std::string s) {
                Utils::toLowerAsciiInPlace(s);
                return s;
            };

            std::ostringstream response;
            response << "[";
            bool first = true;
            for (const auto& item: results) {
                auto doc_id = item.first;
                auto score = item.second;
                std::string title = env.getDatabase().getDocumentTitle(doc_id);
                std::string body = env.getDatabase().getDocumentBody(doc_id);

                // 为匹配高亮/提示做简单 token 包含判断（大小写不敏感）
                std::string title_l = lowerCopy(title);
                std::string body_l = lowerCopy(body);
                std::vector<std::string> matched;
                matched.reserve(query_tokens.size());
                for (const auto& tok: query_tokens)
                    if (title_l.find(tok) != std::string::npos || body_l.find(tok) != std::string::npos)
                        matched.push_back(tok);

                if (!first)
                    response << ",";
                first = false;

                // 手动拼 JSON（注意已做转义）
                response << "{";
                response << "\"id\": " << doc_id << ",";
                response << "\"title\": \"" << Utils::json_escape(title) << "\",";
                response << "\"body\": \"" << Utils::json_escape(body) << "\",";
                response << "\"score\": " << score << ",";
                response << "\"matched_tokens\": [";
                for (size_t i = 0; i < matched.size(); ++i) {
                    if (i)
                        response << ",";
                    response << "\"" << Utils::json_escape(matched[i]) << "\"";
                }
                response << "]";
                response << "}";
            }
            response << "]";
            res.set_content(response.str(), "application/json");
        });

        // 文件导入接口（multipart/form-data），将上传文件写入临时路径并生成后台处理任务
        svr.Post("/api/import", [&](const httplib::Request& req, httplib::Response& res) {
            if (!req.is_multipart_form_data()) {
                res.status = 400;
                res.set_content(R"({"error": "Content-Type must be multipart/form-data"})",
                                "application/json");
                return;
            }

            // 收集所有上传字段里的文件
            std::vector<httplib::FormData> all_files;
            all_files.reserve(req.form.files.size());
            for (const auto& kv: req.form.files)
                all_files.push_back(kv.second);

            if (all_files.empty()) {
                res.status = 400;
                res.set_content(R"({"error": "No files uploaded"})", "application/json");
                return;
            }

            std::vector<std::string> ids;
            ids.reserve(all_files.size());

            // 遍历每个文件，落盘 + 生成任务
            for (const auto& file: all_files) {
                std::string id = next_id(seq); // 全局唯一任务 ID
                std::string safe_name = file.filename.empty() ? std::string("unnamed") : file.filename;
                std::string temp_path = std::string("temp_") + id + "_" + safe_name;

                // 将内容写入临时文件（后端异步再解析/索引）
                std::ofstream out(temp_path, std::ios::binary);
                out.write(file.content.data(), (std::streamsize)file.content.size());
                out.close();

                // 创建任务对象（初始状态：Queued）
                Task tk;
                tk.id = id;
                tk.field_key = ""; // 预留字段（可能用于分类或索引字段）
                tk.filename = safe_name;
                tk.temp_path = temp_path;
                tk.status = TaskStatus::Queued;
                tk.message.clear(); {
                    // 写任务表需加锁，防止并发访问
                    std::lock_guard<std::mutex> lk(tasks_mu);
                    tasks.emplace(id, std::move(tk));
                }

                // 推入任务队列（后台线程消费）
                queue.push(id);
                ids.push_back(id);
            }

            // 返回所有接受的任务 ID
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

        // 查询全部任务列表（按创建时间升序）
        svr.Get("/api/tasks", [&](const httplib::Request&, httplib::Response& res) {
            std::vector<Task> snapshot; {
                // 复制一份快照，减少锁持有时间
                std::lock_guard<std::mutex> lk(tasks_mu);
                snapshot.reserve(tasks.size());
                for (const auto& kv: tasks)
                    snapshot.push_back(kv.second);
            }
            // 使用 C++20 ranges 排序
            std::ranges::sort(snapshot, [](const Task& a, const Task& b) {
                return a.created_at < b.created_at;
            });

            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < snapshot.size(); ++i) {
                const auto& t = snapshot[i];
                if (i)
                    oss << ",";
                oss << "{\"id\":\"" << t.id << "\",";
                oss << "\"filename\":\"" << Utils::json_escape(t.filename) << "\",";
                oss << "\"status\":\"" << status_to_string(t.status) << "\",";
                oss << "\"message\":\"" << Utils::json_escape(t.message) << "\"}";
            }
            oss << "]";
            res.set_content(oss.str(), "application/json");
        });

        // 查询单个任务：/api/task?id=...
        svr.Get("/api/task", [&](const httplib::Request& req, httplib::Response& res) {
            // 从查询参数中读取任务 ID，例如：/api/task?id=xxxx
            auto id = req.get_param_value("id");
            if (id.empty()) {
                // 缺少必须的 id 参数，返回 400 Bad Request
                res.status = 400;
                res.set_content(R"({"error": "Query parameter 'id' is required"})", "application/json");
                return;
            }

            Task t; // 用于存储查询到的任务快照（出锁后使用）
            bool found = false; {
                // 访问任务表需要加锁，保证并发安全
                std::lock_guard<std::mutex> lk(tasks_mu);
                auto it = tasks.find(id);
                if (it != tasks.end()) {
                    t = it->second; // 复制一份数据，尽量缩短锁的持有时间
                    found = true;
                }
            }

            if (!found) {
                // 任务不存在，返回 404 Not Found
                res.status = 404;
                res.set_content(R"({"error": "Task not found"})", "application/json");
                return;
            }

            // 手动拼装 JSON 响应（字段已进行转义以避免破坏 JSON 格式）
            std::ostringstream oss;
            oss << "{\"id\":\"" << t.id << "\",";
            oss << "\"filename\":\"" << Utils::json_escape(t.filename) << "\",";
            oss << "\"status\":\"" << status_to_string(t.status) << "\",";
            oss << "\"message\":\"" << Utils::json_escape(t.message) << "\"}";
            res.set_content(oss.str(), "application/json"); // 返回 200，Content-Type 为 JSON
        });
    }
} // namespace wiser::web
