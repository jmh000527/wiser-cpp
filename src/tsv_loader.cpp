/**
 * @file tsv_loader.cpp
 * @brief TSV 数据加载器实现
 *
 * 输入格式约定：
 * - 每行一条记录：title[TAB]body
 * - 可选表头（has_header=true 时跳过首行）
 *
 * 该加载器会在导入过程中输出进度条，并遵循环境配置的索引上限。
 */

#include "wiser/tsv_loader.h"
#include "wiser/wiser_environment.h"
#include "wiser/utils.h"
#include "wiser/progress_bar.h"

#include <fstream>
#include <string>
#include <iostream>

namespace wiser {
    bool TsvLoader::loadFromFile(const std::string& file_path, bool has_header) {
        if (!env_) {
            return false;
        }

        std::ifstream ifs(file_path);
        if (!ifs.is_open()) {
            spdlog::error("Cannot open TSV file: {}", file_path);
            return false;
        }
        spdlog::info("Loading TSV from: {}", file_path);

        // 预扫：统计总的候选记录数
        std::string line;
        std::uint64_t total_lines = 0;
        bool header_skipped = false;
        while (std::getline(ifs, line)) {
            if (has_header && !header_skipped) {
                header_skipped = true;
                continue;
            }
            if (line.empty())
                continue;
            if (line.find('\t') != std::string::npos) {
                ++total_lines;
            }
        }
        ifs.clear();
        ifs.seekg(0, std::ios::beg);

        const int max_limit = env_ ? env_->getMaxIndexCount() : -1;
        const std::uint64_t total_for_progress = (max_limit >= 0 && static_cast<std::uint64_t>(max_limit) < total_lines)
                                                     ? static_cast<std::uint64_t>(max_limit)
                                                     : total_lines;

        ProgressBar progress(total_for_progress, "Indexing TSV");
        std::uint64_t processed_ok = 0;

        if (has_header && std::getline(ifs, line)) {
            // skip header
        }

        while (std::getline(ifs, line)) {
            if (env_->hasReachedIndexLimit()) break;
            if (line.empty()) continue;

            std::size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            std::string title = line.substr(0, tab);
            std::string body = line.substr(tab + 1);
            if (title.empty() || body.empty()) continue;

            env_->addDocument(title, body);
            ++processed_ok;
            progress.update(processed_ok);

            if (env_->hasReachedIndexLimit()) break;
        }

        if (processed_ok > 0) progress.finish();

        spdlog::info("TSV loader done. Lines imported: {}", processed_ok);
        return true;
    }
} // namespace wiser
