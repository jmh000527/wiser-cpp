#include "wiser/wiki_loader.h"
#include "wiser/wiser_environment.h"
#include "wiser/utils.h"
#include <fstream>
#include <regex>
#include <iostream>

namespace wiser {
    WikiLoader::WikiLoader(WiserEnvironment* env)
        : env_(env) {}

    bool WikiLoader::loadFromFile(const std::string& file_path) {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            Utils::printError("Cannot open file: {}", file_path);
            return false;
        }

        Utils::printInfo("Loading Wikipedia data from: {}", file_path);

        // 预扫一遍统计总页数，用于显示百分比进度
        std::string line;
        int total_pages = 0;
        while (std::getline(file, line)) {
            if (line.find("<page>") != std::string::npos) {
                ++total_pages;
            }
        }
        // 重置文件流，开始正式解析
        file.clear();
        file.seekg(0, std::ios::beg);

        // 读取本次运行的上限，用于进度显示
        const int max_limit = env_ ? env_->getMaxIndexCount() : -1;
        const int total_for_progress = (max_limit >= 0 && max_limit < total_pages) ? max_limit : total_pages;

        auto print_progress = [&](int processed, int total) {
            if (total <= 0) {
                std::cerr << "\rProcessed: " << processed << std::flush;
                return;
            }
            const int bar_width = 50;
            double ratio = static_cast<double>(processed) / static_cast<double>(total);
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

        std::string current_title;
        std::string current_content;
        bool in_page = false;
        bool in_title = false;
        bool in_text = false;
        int processed_pages = 0;

        while (std::getline(file, line)) {
            // 达到索引上限则提前退出
            if (env_ && env_->hasReachedIndexLimit()) {
                break;
            }

            // 简化的XML解析（仅处理基本的Wikipedia XML格式）
            if (line.find("<page>") != std::string::npos) {
                in_page = true;
                current_title.clear();
                current_content.clear();
            } else if (line.find("</page>") != std::string::npos && in_page) {
                in_page = false;

                if (!current_title.empty() && !current_content.empty() &&
                    isValidPage(current_title, current_content)) {
                    std::string cleaned_content = cleanWikiText(current_content);

                    if (processPage(current_title, cleaned_content)) {
                        ++processed_pages;
                        print_progress(processed_pages, total_for_progress);
                        // 每成功处理一个页面后再次检查上限
                        if (env_ && env_->hasReachedIndexLimit()) {
                            std::cerr << std::endl;
                            break;
                        }
                    }
                }
            } else if (line.find("<title>") != std::string::npos && in_page) {
                in_title = true;
                // 提取标题
                size_t start = line.find("<title>") + 7;
                size_t end = line.find("</title>");
                if (end != std::string::npos) {
                    current_title = line.substr(start, end - start);
                    in_title = false;
                } else {
                    current_title = line.substr(start);
                }
            } else if (line.find("</title>") != std::string::npos && in_title) {
                in_title = false;
                size_t end = line.find("</title>");
                current_title += line.substr(0, end);
            } else if (in_title) {
                current_title += line;
            } else if (line.find("<text") != std::string::npos && in_page) {
                in_text = true;
                // 查找>符号，开始提取内容
                size_t start = line.find('>');
                if (start != std::string::npos) {
                    current_content = line.substr(start + 1);
                }
            } else if (line.find("</text>") != std::string::npos && in_text) {
                in_text = false;
                size_t end = line.find("</text>");
                current_content += line.substr(0, end);
            } else if (in_text) {
                current_content += line + "\n";
            }
        }

        // 完成后换行，避免光标停留在同一行
        if (processed_pages > 0) {
            print_progress(processed_pages, total_for_progress);
            std::cerr << std::endl;
        }

        Utils::printInfo("Completed loading. Processed {} pages total.", processed_pages);
        return true;
    }

    bool WikiLoader::processPage(const std::string& title, const std::string& content) {
        try {
            env_->addDocument(title, content);
            return true;
        } catch (const std::exception& e) {
            Utils::printError("Failed to process page '{}': {}", title, e.what());
            return false;
        }
    }

    std::string WikiLoader::cleanWikiText(const std::string& raw_text) {
        std::string cleaned = raw_text;

        // 移除Wiki标记
        std::vector<std::pair<std::regex, std::string>> replacements = {
                    // 移除内部链接 [[link|text]] -> text 或 [[link]] -> link
                    { std::regex(R"(\[\[([^\]|]+)\|([^\]]+)\]\])"), "$2" },
                    { std::regex(R"(\[\[([^\]]+)\]\])"), "$1" },

                    // 移除外部链接 [url text] -> text
                    { std::regex(R"(\[http[^\s]+ ([^\]]+)\])"), "$1" },
                    { std::regex(R"(\[http[^\s]+\])"), "" },

                    // 移除文件引用
                    { std::regex(R"(\[\[File:[^\]]+\]\])"), "" },
                    { std::regex(R"(\[\[Image:[^\]]+\]\])"), "" },

                    // 移除模板 {{template}}
                    { std::regex(R"(\{\{[^}]*(\}\}))"), "" },

                    // 移除粗体和斜体标记
                    { std::regex(R"('''([^']+)''')"), "$1" },
                    { std::regex(R"(''([^']+)'')"), "$1" },

                    // 移除HTML标签
                    { std::regex(R"(<[^>]+>)"), "" },

                    // 移除引用标记
                    { std::regex(R"(<ref[^>]*>[^<]*</ref>)"), "" },
                    { std::regex(R"(<ref[^>]*/>)"), "" },

                    // 清理多余的空白字符
                    { std::regex(R"(\s+)"), " " },
                    { std::regex(R"(^\s+|\s+$)"), "" }
                };

        for (const auto& replacement: replacements) {
            cleaned = std::regex_replace(cleaned, replacement.first, replacement.second);
        }

        return cleaned;
    }

    bool WikiLoader::isValidPage(const std::string& title, const std::string& content) {
        // 过滤掉不需要的页面
        if (title.empty() || content.empty()) {
            return false;
        }

        // 跳过重定向页面
        if (content.find("#REDIRECT") != std::string::npos ||
            content.find("#redirect") != std::string::npos) {
            return false;
        }

        // 跳过消歧义页面
        if (title.find("(disambiguation)") != std::string::npos) {
            return false;
        }

        // 跳过系统页面
        if (title.find("Wikipedia:") == 0 ||
            title.find("Help:") == 0 ||
            title.find("Category:") == 0 ||
            title.find("Template:") == 0 ||
            title.find("File:") == 0 ||
            title.find("Image:") == 0) {
            return false;
        }

        // 确保内容有足够长度
        if (content.length() < 100) {
            return false;
        }

        return true;
    }
} // namespace wiser
