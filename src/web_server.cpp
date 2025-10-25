#include <httplib.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cctype>
#include <filesystem>

#include "wiser/wiser.h"
#include "wiser/search_engine.h"
#include "wiser/json_loader.h"
#include "wiser/tsv_loader.h"

int main() {
    // 初始化搜索引擎环境
    std::string db_path = "web_server_demo.db";
    if (std::filesystem::exists(db_path)) {
        std::filesystem::remove(db_path);
    }

    wiser::WiserEnvironment env;
    if (!env.initialize(db_path)) {
        std::cerr << "Failed to initialize search engine." << std::endl;
        return 1;
    }

    env.setPhraseSearchEnabled(true);                   // 启用短语搜索
    env.setTokenLength(2);                              // bi-gram
    env.setBufferUpdateThreshold(1024);                 // 设置缓冲区阈值
    env.setCompressMethod(wiser::CompressMethod::NONE); // 设置压缩方法
    env.setMaxIndexCount(-1);                           // 不限制索引数量

    wiser::SearchEngine search_engine(&env);

    // 创建HTTP服务器
    httplib::Server svr;

    // 静态文件服务
    svr.set_mount_point("/", "../web");

    // Helper: escape JSON string
    auto json_escape = [](const std::string& s) {
        std::ostringstream o;
        for (auto c = s.cbegin(); c != s.cend(); ++c) {
            unsigned char ch = static_cast<unsigned char>(*c);
            switch (ch) {
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
                    if (ch < 0x20) {
                        o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)ch << std::dec;
                    } else {
                        o << *c;
                    }
            }
        }
        return o.str();
    };

    // 搜索API
    svr.Get("/api/search", [&](const httplib::Request& req, httplib::Response& res) {
        auto query = req.get_param_value("q");
        if (query.empty()) {
            res.status = 400;
            res.set_content("{\"error\": \"Query parameter 'q' is required\"}", "application/json");
            return;
        }

        auto results = search_engine.searchWithResults(query);
        std::ostringstream response;
        response << "[";
        bool first = true;
        for (const auto& item: results) {
            auto doc_id = item.first;
            auto score = item.second;
            std::string title = env.getDatabase().getDocumentTitle(doc_id);
            std::string body = env.getDatabase().getDocumentBody(doc_id);
            std::string body_snippet = body.substr(0, std::min<size_t>(200, body.size()));
            if (!first)
                response << ",";
            first = false;
            response << "{"
                    << "\"id\": " << doc_id << ","
                    << "\"title\": \"" << json_escape(title) << "\","
                    << "\"body\": \"" << json_escape(body_snippet) << "\","
                    << "\"score\": " << score
                    << "}";
        }
        response << "]";
        res.set_content(response.str(), "application/json");
    });

    // 导入API
    svr.Post("/api/import", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.is_multipart_form_data()) {
            res.status = 400;
            res.set_content("{\"error\": \"Content-Type must be multipart/form-data\"}", "application/json");
            return;
        }

        if (!req.form.has_file("file")) {
            res.status = 400;
            res.set_content("{\"error\": \"File is required\"}", "application/json");
            return;
        }

        auto file = req.form.get_file("file");
        if (file.filename.empty()) {
            res.status = 400;
            res.set_content("{\"error\": \"File is required\"}", "application/json");
            return;
        }

        std::string temp_path = "temp_" + file.filename;
        std::ofstream out(temp_path, std::ios::binary);
        out.write(file.content.c_str(), file.content.size());
        out.close();

        bool success = false;
        auto ends_with = [](const std::string& s, const std::string& ext) {
            if (s.size() < ext.size())
                return false;
            return std::equal(ext.rbegin(), ext.rend(), s.rbegin(), [](char a, char b) {
                return std::tolower(a) == std::tolower(b);
            });
        };

        if (ends_with(file.filename, ".json") || ends_with(file.filename, ".jsonl")) {
            wiser::JsonLoader loader(&env);
            success = loader.loadFromFile(temp_path);
        } else if (ends_with(file.filename, ".tsv")) {
            wiser::TsvLoader loader(&env);
            success = loader.loadFromFile(temp_path);
        } else {
            res.status = 400;
            res.set_content("{\"error\": \"Unsupported file type\"}", "application/json");
            std::remove(temp_path.c_str());
            return;
        }

        std::remove(temp_path.c_str());

        if (success) {
            res.set_content("{\"message\": \"Import successful\"}", "application/json");
        } else {
            res.status = 500;
            res.set_content("{\"error\": \"Import failed\"}", "application/json");
        }
    });

    // 启动服务器
    std::cout << "Starting server on http://localhost:54321" << std::endl;
    svr.listen("0.0.0.0", 54321);

    return 0;
}
