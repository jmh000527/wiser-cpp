#include "wiser/json_loader.h"
#include "wiser/wiser_environment.h"
#include "wiser/utils.h"

#include <fstream>
#include <string>
#include <string_view>
#include <cctype>
#include <iostream>

namespace wiser {
    static inline void trimLeft(std::string_view& sv) {
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
            sv.remove_prefix(1);
    }

    bool JsonLoader::extractStringField(const std::string& json_obj, const std::string& key, std::string& out) {
        // 朴素提取：在对象文本里查找 "key" 后的冒号，然后读取一个 JSON 字符串，支持常见转义
        // 假设对象是扁平的：{ "title": "...", "body": "..." }
        const std::string needle = '"' + key + '"';
        std::size_t p = json_obj.find(needle);
        if (p == std::string::npos)
            return false;
        p += needle.size();
        // 跳过空白与冒号
        while (p < json_obj.size() && std::isspace(static_cast<unsigned char>(json_obj[p])))
            ++p;
        if (p >= json_obj.size() || json_obj[p] != ':')
            return false;
        ++p;
        while (p < json_obj.size() && std::isspace(static_cast<unsigned char>(json_obj[p])))
            ++p;
        if (p >= json_obj.size() || json_obj[p] != '"')
            return false;
        ++p; // 进入字符串

        std::string value;
        value.reserve(128);
        bool esc = false;
        for (; p < json_obj.size(); ++p) {
            char c = json_obj[p];
            if (esc) {
                switch (c) {
                    case 'n':
                        value.push_back('\n');
                        break;
                    case 'r':
                        value.push_back('\r');
                        break;
                    case 't':
                        value.push_back('\t');
                        break;
                    case '"':
                        value.push_back('"');
                        break;
                    case '\\':
                        value.push_back('\\');
                        break;
                    case 'b':
                        value.push_back('\b');
                        break;
                    case 'f':
                        value.push_back('\f');
                        break;
                    case 'u':
                        // 简化：不解析 \uXXXX，按原样保留（UTF-8 输入通常不包含 \u）
                        value.append("\\u");
                        break;
                    default:
                        value.push_back(c);
                        break;
                }
                esc = false;
            } else if (c == '\\') {
                esc = true;
            } else if (c == '"') {
                // 结束
                ++p;
                break;
            } else {
                value.push_back(c);
            }
        }
        out.swap(value);
        return true;
    }

    bool JsonLoader::parseObjectToTitleBody(const std::string& json_obj, std::string& title, std::string& body) {
        std::string t, b;
        bool ok_t = extractStringField(json_obj, "title", t);
        bool ok_b = extractStringField(json_obj, "body", b);
        if (!ok_t || !ok_b)
            return false;
        title.swap(t);
        body.swap(b);
        return true;
    }

    bool JsonLoader::loadFromJsonLines(const std::string& file_path) {
        if (!env_)
            return false;
        std::ifstream ifs(file_path);
        if (!ifs.is_open()) {
            Utils::printError("Cannot open JSON Lines file: {}", file_path);
            return false;
        }
        Utils::printInfo("Loading JSON Lines from: {}", file_path);

        // 预扫统计：非空且以 '{' 开头的行视作一个对象
        std::string line;
        std::uint64_t total_lines = 0;
        while (std::getline(ifs, line)) {
            std::string_view sv(line);
            trimLeft(sv);
            if (!sv.empty() && sv.front() == '{') {
                ++total_lines;
            }
        }
        // 重置文件流
        ifs.clear();
        ifs.seekg(0, std::ios::beg);

        const int max_limit = env_ ? env_->getMaxIndexCount() : -1;
        const std::uint64_t total_for_progress = (max_limit >= 0 && static_cast<std::uint64_t>(max_limit) < total_lines)
                                                     ? static_cast<std::uint64_t>(max_limit)
                                                     : total_lines;

        auto print_progress = [&](std::uint64_t processed, std::uint64_t total) {
            if (total == 0) {
                std::cerr << "\rProcessed: " << processed << std::flush;
                return;
            }
            const int bar_width = 50;
            double ratio = total ? static_cast<double>(processed) / static_cast<double>(total) : 0.0;
            if (ratio > 1.0)
                ratio = 1.0;
            int filled = static_cast<int>(ratio * bar_width);
            std::cerr << "\r[";
            for (int i = 0; i < bar_width; ++i) {
                std::cerr << (i < filled ? '#' : '-');
            }
            int percent = static_cast<int>(ratio * 100.0);
            std::cerr << "] " << percent << "% (" << processed << "/" << total << ")" << std::flush;
        };

        std::uint64_t processed = 0, ok = 0;
        while (std::getline(ifs, line)) {
            // 跳过空行
            std::string_view sv(line);
            trimLeft(sv);
            if (sv.empty())
                continue;
            if (sv.front() != '{')
                continue; // 仅处理对象行

            std::string title, body;
            if (parseObjectToTitleBody(std::string(sv), title, body)) {
                if (!title.empty() && !body.empty() && !env_->hasReachedIndexLimit()) {
                    env_->addDocument(title, body);
                    ++ok;
                    print_progress(ok, total_for_progress);
                    if (env_->hasReachedIndexLimit()) {
                        std::cerr << std::endl;
                        break;
                    }
                }
            }
            ++processed;
        }
        if (ok > 0) {
            print_progress(ok, total_for_progress);
            std::cerr << std::endl;
        }
        Utils::printInfo("JSONL done. Lines processed: {}, imported: {}", processed, ok);
        return true;
    }

    bool JsonLoader::loadFromArrayFile(const std::string& file_path) {
        if (!env_)
            return false;
        std::ifstream ifs(file_path, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) {
            Utils::printError("Cannot open JSON array file: {}", file_path);
            return false;
        }
        std::string data;
        ifs.seekg(0, std::ios::end);
        auto len = ifs.tellg();
        if (len > 0) {
            data.resize(static_cast<size_t>(len));
            ifs.seekg(0, std::ios::beg);
            ifs.read(data.data(), len);
        }
        ifs.close();

        if (data.empty())
            return true;

        // 一次扫描统计对象数量（不解析字段），用于进度条
        std::uint64_t total_objs = 0; {
            std::size_t i = 0, n = data.size();
            while (i < n && std::isspace(static_cast<unsigned char>(data[i])))
                ++i;
            if (i < n && data[i] == '[') {
                ++i;
            }
            bool in_str = false, esc = false;
            int brace = 0;
            for (; i < n; ++i) {
                char c = data[i];
                if (in_str) {
                    if (esc) {
                        esc = false;
                    } else if (c == '\\') {
                        esc = true;
                    } else
                        if (c == '"') {
                            in_str = false;
                        }
                } else {
                    if (c == '"')
                        in_str = true;
                    else if (c == '{') {
                        if (brace == 0)
                            ++total_objs;
                        ++brace;
                    } else if (c == '}') {
                        if (brace > 0)
                            --brace;
                    } else
                        if (c == ']') {
                            break;
                        }
                }
            }
        }

        const int max_limit = env_ ? env_->getMaxIndexCount() : -1;
        const std::uint64_t total_for_progress = (max_limit >= 0 && static_cast<std::uint64_t>(max_limit) < total_objs)
                                                     ? static_cast<std::uint64_t>(max_limit)
                                                     : total_objs;

        auto print_progress = [&](std::uint64_t processed, std::uint64_t total) {
            if (total == 0) {
                std::cerr << "\rProcessed: " << processed << std::flush;
                return;
            }
            const int bar_width = 50;
            double ratio = total ? static_cast<double>(processed) / static_cast<double>(total) : 0.0;
            if (ratio > 1.0)
                ratio = 1.0;
            int filled = static_cast<int>(ratio * bar_width);
            std::cerr << "\r[";
            for (int i = 0; i < bar_width; ++i) {
                std::cerr << (i < filled ? '#' : '-');
            }
            int percent = static_cast<int>(ratio * 100.0);
            std::cerr << "] " << percent << "% (" << processed << "/" << total << ")" << std::flush;
        };

        // 朴素解析：遍历顶层数组，提取每个对象的文本。需要区分字符串内的字符与对象括号平衡。
        std::uint64_t processed = 0, ok = 0;
        std::size_t i = 0, n = data.size();
        // 跳过空白
        while (i < n && std::isspace(static_cast<unsigned char>(data[i])))
            ++i;
        if (i >= n || data[i] != '[') {
            Utils::printError("Not a JSON array file: {}", file_path);
            return false;
        }
        ++i;
        while (i < n) {
            while (i < n && std::isspace(static_cast<unsigned char>(data[i])))
                ++i;
            if (i < n && data[i] == ']') {
                ++i;
                break;
            }
            if (i >= n)
                break;
            if (data[i] == ',') {
                ++i;
                continue;
            }
            if (data[i] != '{') {
                ++i;
                continue;
            }

            // 抽取一个对象
            std::size_t start = i;
            int brace = 0;
            bool in_str = false, esc = false;
            for (; i < n; ++i) {
                char c = data[i];
                if (in_str) {
                    if (esc) {
                        esc = false;
                    } else if (c == '\\') {
                        esc = true;
                    } else
                        if (c == '"') {
                            in_str = false;
                        }
                } else {
                    if (c == '"')
                        in_str = true;
                    else if (c == '{')
                        ++brace;
                    else
                        if (c == '}') {
                            --brace;
                            if (brace == 0) {
                                ++i;
                                break;
                            }
                        }
                }
            }
            if (brace != 0)
                break; // 非法
            std::string obj = data.substr(start, i - start);

            std::string title, body;
            if (parseObjectToTitleBody(obj, title, body)) {
                if (!title.empty() && !body.empty() && !env_->hasReachedIndexLimit()) {
                    env_->addDocument(title, body);
                    ++ok;
                    print_progress(ok, total_for_progress);
                    if (env_->hasReachedIndexLimit()) {
                        std::cerr << std::endl;
                        break;
                    }
                }
            }
            ++processed;
        }

        if (ok > 0) {
            print_progress(ok, total_for_progress);
            std::cerr << std::endl;
        }

        Utils::printInfo("JSON array done. Objects processed: {}, imported: {}", processed, ok);
        return true;
    }

    bool JsonLoader::loadFromFile(const std::string& file_path) {
        // 根据首个非空字符判断格式
        std::ifstream ifs(file_path, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) {
            Utils::printError("Cannot open JSON file: {}", file_path);
            return false;
        }
        char ch = 0;
        while (ifs.good()) {
            ch = static_cast<char>(ifs.get());
            if (!ifs.good())
                break;
            if (!std::isspace(static_cast<unsigned char>(ch)))
                break;
        }
        ifs.close();

        if (ch == '[') {
            return loadFromArrayFile(file_path);
        } else {
            return loadFromJsonLines(file_path);
        }
    }
} // namespace wiser
