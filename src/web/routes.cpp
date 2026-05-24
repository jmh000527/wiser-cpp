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
#include <chrono>
#include "wiser/wiser_environment.h"
#include "wiser/search_engine.h"
#include "wiser/utils.h"
#include "wiser/3rdparty/httplib.h"
#include <sqlite3.h>

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
                         std::shared_mutex& index_mutex,
                         std::mutex& tasks_mu,
                         TaskTable& tasks,
                         TaskQueue& queue,
                         std::atomic<uint64_t>& seq,
                         AuthManager& auth) {
        // 静态文件目录挂载（前端页面）
        if (fs::exists("../web")) { svr.set_mount_point("/", "../web"); } else {
            spdlog::warn("Web directory '../web' not found, static files will not be served.");
        }

        // 搜索接口：/api/search?q=...
        // 返回：命中文档列表（含：id/title/body/score/matched_tokens）
        svr.Get("/api/search", [&](const httplib::Request& req, httplib::Response& res) {
            auto t_start = std::chrono::steady_clock::now();
            // 使用共享锁保护搜索读取，允许多个搜索请求并发执行
            std::shared_lock<std::shared_mutex> lock(index_mutex);

            auto query = req.get_param_value("q");
            if (query.empty()) {
                res.status = 400;
                res.set_content(R"({"error": "Query parameter 'q' is required"})", "application/json");
                return;
            }
            if (query.size() > 1000) {
                res.status = 400;
                res.set_content(R"json({"error": "Query too long (max 1000 characters)"})json", "application/json");
                return;
            }

            // 处理运行时参数
            auto phrase_param = req.get_param_value("phrase");
            if (!phrase_param.empty()) {
                env.setPhraseSearchEnabled(phrase_param == "1");
            } else {
                env.setPhraseSearchEnabled(false);
            }

            auto scoring_param = req.get_param_value("scoring");
            if (scoring_param == "tfidf") {
                env.setScoringMethod(wiser::ScoringMethod::TF_IDF);
            } else {
                env.setScoringMethod(wiser::ScoringMethod::BM25);
            }

            // 分页参数
            int page = 1;
            int page_size = 20;
            auto page_param = req.get_param_value("page");
            auto page_size_param = req.get_param_value("page_size");
            if (!page_param.empty()) {
                try { page = std::max(1, std::stoi(page_param)); }
                catch (...) { page = 1; }
            }
            if (!page_size_param.empty()) {
                try { page_size = std::clamp(std::stoi(page_size_param), 1, 100); }
                catch (...) { page_size = 20; }
            }

            const int n = env.getTokenLength();
            auto query_tokens = Utils::tokenizeQueryTokens(query, n);

            // 模糊搜索参数
            int fuzzy_dist = 0;
            auto fuzzy_param = req.get_param_value("fuzzy");
            if (!fuzzy_param.empty()) {
                try { fuzzy_dist = std::clamp(std::stoi(fuzzy_param), 0, 2); }
                catch (...) { fuzzy_dist = 0; }
            }

            std::vector<std::pair<wiser::DocId, double>> all_results =
                search_engine.searchWithResults(query, fuzzy_dist);

            // Filter out deleted documents whose postings still linger
            std::erase_if(all_results, [&env](const auto& p) {
                return env.getDatabase().getDocumentTitle(p.first).empty();
            });

            int total_hits = static_cast<int>(all_results.size());
            int start_idx = (page - 1) * page_size;
            int end_idx = std::min(start_idx + page_size, total_hits);

            auto lowerCopy = [](std::string s) {
                Utils::toLowerAsciiInPlace(s);
                return s;
            };

            auto t_end = std::chrono::steady_clock::now();
            double took_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

            std::ostringstream response;
            response << "{";
            response << "\"total_hits\":" << total_hits << ",";
            response << "\"page\":" << page << ",";
            response << "\"page_size\":" << page_size << ",";
            response << "\"took_ms\":" << std::fixed << std::setprecision(2) << took_ms << ",";
            response << "\"results\":[";
            bool first = true;
            for (int i = start_idx; i < end_idx; ++i) {
                auto doc_id = all_results[i].first;
                auto score = all_results[i].second;
                std::string title = env.getDatabase().getDocumentTitle(doc_id);
                std::string body = env.getDatabase().getDocumentBody(doc_id);
                std::string author = env.getDatabase().getDocumentAuthor(doc_id);

                std::string title_l = lowerCopy(title);
                std::string body_l = lowerCopy(body);
                std::vector<std::string> matched;
                matched.reserve(query_tokens.size());
                for (const auto& tok: query_tokens)
                    if (title_l.find(tok) != std::string::npos || body_l.find(tok) != std::string::npos)
                        matched.push_back(tok);

                // Generate contextual snippet around matched tokens
                int snippet_len = 200;
                auto snippet_param = req.get_param_value("snippet_len");
                if (!snippet_param.empty()) {
                    try { snippet_len = std::clamp(std::stoi(snippet_param), 50, 500); }
                    catch (...) { snippet_len = 200; }
                }
                std::string snippet = Utils::generateSnippet(body, query_tokens,
                                                             static_cast<size_t>(snippet_len));

                if (!first)
                    response << ",";
                first = false;

                response << "{";
                response << "\"id\": " << doc_id << ",";
                response << "\"title\": \"" << Utils::json_escape(title) << "\",";
                response << "\"author\": \"" << Utils::json_escape(author) << "\",";
                response << "\"body\": \"" << Utils::json_escape(body) << "\",";
                response << "\"snippet\": \"" << Utils::json_escape(snippet) << "\",";
                response << "\"score\": " << score << ",";
                response << "\"matched_tokens\": [";
                for (size_t j = 0; j < matched.size(); ++j) {
                    if (j)
                        response << ",";
                    response << "\"" << Utils::json_escape(matched[j]) << "\"";
                }
                response << "]";
                response << "}";
            }
            response << "]";

            // 当无结果时，提供拼写纠正建议
            if (total_hits == 0) {
                std::string suggestion = search_engine.spellCheck(query);
                if (!suggestion.empty()) {
                    response << ",\"did_you_mean\":\"" << Utils::json_escape(suggestion) << "\"";
                }
            }

            response << "}";
            res.set_content(response.str(), "application/json");
        });

        // 删除文档接口
        svr.Delete(R"(/api/documents/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
            auto id_str = req.matches[1].str();
            wiser::DocId doc_id;
            try {
                doc_id = static_cast<wiser::DocId>(std::stoul(id_str));
            } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid document ID"})", "application/json");
                return;
            }
            std::unique_lock<std::shared_mutex> lock(index_mutex);
            bool deleted = env.getDatabase().deleteDocument(doc_id);
            if (deleted) {
                env.getSearchEngine().invalidateCache();
                res.set_content(R"({"deleted":true,"id":)" + id_str + "}", "application/json");
            } else {
                res.status = 404;
                res.set_content(R"({"error":"Document not found"})", "application/json");
            }
        });

        // 搜索建议/自动补全接口
        svr.Get("/api/suggest", [&](const httplib::Request& req, httplib::Response& res) {
            auto q = req.get_param_value("q");
            if (q.empty() || q.size() > 200) {
                res.set_content(R"({"suggestions":[]})", "application/json");
                return;
            }

            int limit = 8;
            auto limit_param = req.get_param_value("limit");
            if (!limit_param.empty()) {
                try { limit = std::clamp(std::stoi(limit_param), 1, 20); }
                catch (...) { limit = 8; }
            }

            std::shared_lock<std::shared_mutex> lock(index_mutex);
            auto token_suggestions = env.getDatabase().suggestTokens(q, limit);
            auto title_suggestions = env.getDatabase().suggestTitles(q, 5);

            std::ostringstream response;
            response << "{\"suggestions\":[";

            // 先输出标题建议（更有价值）
            int count = 0;
            for (const auto& [id, title] : title_suggestions) {
                if (count > 0) response << ",";
                response << "{\"type\":\"title\",\"text\":\""
                         << wiser::Utils::json_escape(title)
                         << "\",\"doc_id\":" << id << "}";
                ++count;
            }

            // 再输出 token 建议
            for (const auto& [token, docs_count] : token_suggestions) {
                if (count >= limit) break;
                if (count > 0) response << ",";
                response << "{\"type\":\"token\",\"text\":\""
                         << wiser::Utils::json_escape(token)
                         << "\",\"docs_count\":" << docs_count << "}";
                ++count;
            }

            response << "]}";
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

        // ================================================================
        // P4-1: RESTful 文档 API
        // ================================================================

        // 获取单个文档
        svr.Get(R"(/api/documents/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
            auto id_str = req.matches[1].str();
            wiser::DocId doc_id;
            try {
                doc_id = static_cast<wiser::DocId>(std::stoul(id_str));
            } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid document ID"})", "application/json");
                return;
            }
            std::shared_lock<std::shared_mutex> lock(index_mutex);
            std::string title = env.getDatabase().getDocumentTitle(doc_id);
            if (title.empty()) {
                res.status = 404;
                res.set_content(R"({"error":"Document not found"})", "application/json");
                return;
            }
            std::string body = env.getDatabase().getDocumentBody(doc_id);
            std::string author = env.getDatabase().getDocumentAuthor(doc_id);
            int token_count = env.getDatabase().getDocumentTokenCount(doc_id);

            std::ostringstream oss;
            oss << "{\"id\":" << doc_id << ",";
            oss << "\"title\":\"" << Utils::json_escape(title) << "\",";
            oss << "\"body\":\"" << Utils::json_escape(body) << "\",";
            oss << "\"author\":\"" << Utils::json_escape(author) << "\",";
            oss << "\"token_count\":" << token_count << "}";
            res.set_content(oss.str(), "application/json");
        });

        // 添加单个文档（JSON body: {"title":"...","body":"..."}）
        svr.Post("/api/documents", [&](const httplib::Request& req, httplib::Response& res) {
            // 简单解析 JSON：查找 title 和 body 字段
            auto extract = [](const std::string& json, const std::string& key) -> std::string {
                std::string search = "\"" + key + "\"";
                auto pos = json.find(search);
                if (pos == std::string::npos) return {};
                pos = json.find(':', pos + search.size());
                if (pos == std::string::npos) return {};
                pos = json.find('"', pos + 1);
                if (pos == std::string::npos) return {};
                ++pos;
                std::string result;
                while (pos < json.size() && json[pos] != '"') {
                    if (json[pos] == '\\' && pos + 1 < json.size()) {
                        ++pos;
                        switch (json[pos]) {
                            case '"': result += '"'; break;
                            case '\\': result += '\\'; break;
                            case 'n': result += '\n'; break;
                            case 't': result += '\t'; break;
                            default: result += json[pos]; break;
                        }
                    } else {
                        result += json[pos];
                    }
                    ++pos;
                }
                return result;
            };

            std::string title = extract(req.body, "title");
            std::string body = extract(req.body, "body");
            std::string author = extract(req.body, "author");

            if (title.empty() || body.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"Both 'title' and 'body' are required"})", "application/json");
                return;
            }

            std::unique_lock<std::shared_mutex> lock(index_mutex);
            env.addDocument(title, body, author);
            env.flushIndexBuffer();
            auto doc_id = env.getDatabase().getDocumentId(title);

            std::ostringstream oss;
            oss << "{\"id\":" << doc_id << ",\"title\":\"" << Utils::json_escape(title) << "\"}";
            res.status = 201;
            res.set_content(oss.str(), "application/json");
        });

        // 手动刷新索引缓冲区
        svr.Post("/api/index/flush", [&](const httplib::Request&, httplib::Response& res) {
            std::unique_lock<std::shared_mutex> lock(index_mutex);
            env.flushIndexBuffer();
            res.set_content(R"({"flushed":true})", "application/json");
        });

        // 重建倒排索引（修复历史索引缺失）
        svr.Post("/api/index/rebuild", [&](const httplib::Request&, httplib::Response& res) {
            std::unique_lock<std::shared_mutex> lock(index_mutex);
            int count = env.rebuildIndex();
            if (count < 0) {
                res.status = 500;
                res.set_content(R"({"error":"rebuild failed"})", "application/json");
                return;
            }
            res.set_content("{\"rebuilt\":" + std::to_string(count) + "}", "application/json");
        });

        // ================================================================
        // P4-4: 在线备份
        // ================================================================
        svr.Post("/api/admin/backup", [&](const httplib::Request&, httplib::Response& res) {
            std::shared_lock<std::shared_mutex> lock(index_mutex);
            std::string db_path = env.getConfig().db_path;
            std::string backup_path = db_path + ".backup";

            // 使用 SQLite Online Backup API
            sqlite3* src_handle = env.getDatabase().getHandle();
            if (!src_handle) {
                res.status = 500;
                res.set_content(R"({"error":"Database not initialized"})", "application/json");
                return;
            }
            sqlite3* dest_db = nullptr;
            int rc = sqlite3_open(backup_path.c_str(), &dest_db);
            if (rc != SQLITE_OK) {
                if (dest_db) sqlite3_close(dest_db);
                res.status = 500;
                res.set_content(R"({"error":"Cannot open backup destination"})", "application/json");
                return;
            }

            sqlite3_backup* backup = sqlite3_backup_init(dest_db, "main",
                src_handle, "main");
            if (!backup) {
                sqlite3_close(dest_db);
                res.status = 500;
                res.set_content(R"({"error":"Cannot initialize backup"})", "application/json");
                return;
            }

            rc = sqlite3_backup_step(backup, -1); // copy all pages
            sqlite3_backup_finish(backup);
            sqlite3_close(dest_db);

            if (rc != SQLITE_DONE) {
                res.status = 500;
                std::string msg = R"({"error":"Backup failed","rc":)" + std::to_string(rc) + "}";
                res.set_content(msg, "application/json");
                return;
            }

            std::ostringstream oss;
            oss << "{\"backed_up\":true,\"path\":\"" << Utils::json_escape(backup_path) << "\"}";
            res.set_content(oss.str(), "application/json");
        });
    }
} // namespace wiser::web
