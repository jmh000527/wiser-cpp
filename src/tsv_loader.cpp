#include "wiser/tsv_loader.h"
#include "wiser/wiser_environment.h"
#include "wiser/utils.h"

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
            Utils::printError("Cannot open TSV file: {}\n", file_path);
            return false;
        }
        Utils::printInfo("Loading TSV from: {}\n", file_path);

        // 预扫：统计总的候选记录数（用于进度条），忽略表头与空行
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
            // 有制表符才算一条候选
            if (line.find('\t') != std::string::npos) {
                ++total_lines;
            }
        }
        // 重置文件流，正式处理
        ifs.clear();
        ifs.seekg(0, std::ios::beg);

        // 读取本次运行的上限，用于进度显示
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
            double ratio = static_cast<double>(processed) / static_cast<double>(total);
            if (ratio > 1.0)
                ratio = 1.0;
            int filled = static_cast<int>(ratio * bar_width);
            std::cerr << "\r[INFO] [";
            for (int i = 0; i < bar_width; ++i) {
                std::cerr << (i < filled ? '#' : '-');
            }
            int percent = static_cast<int>(ratio * 100.0);
            std::cerr << "] " << percent << "% (" << processed << "/" << total << ")" << std::flush;
        };

        std::uint64_t processed_ok = 0;

        // 跳过表头（若有）
        if (has_header && std::getline(ifs, line)) {
            // skip header
        }

        while (std::getline(ifs, line)) {
            if (env_->hasReachedIndexLimit()) {
                break;
            }
            if (line.empty())
                continue;

            // 找第一个TAB作为分隔
            std::size_t tab = line.find('\t');
            if (tab == std::string::npos)
                continue;
            std::string title = line.substr(0, tab);
            std::string body = line.substr(tab + 1);
            if (title.empty() || body.empty())
                continue;

            env_->addDocument(title, body);
            ++processed_ok;
            print_progress(processed_ok, total_for_progress);

            if (env_->hasReachedIndexLimit()) {
                std::cerr << std::endl;
                break;
            }
        }

        if (processed_ok > 0) {
            print_progress(processed_ok, total_for_progress);
            std::cerr << std::endl;
        }

        Utils::printInfo("TSV loader done. Lines imported: {}\n", processed_ok);

        // // 新增：末尾自动刷盘（当未达到阈值时避免数据仅留在内存）
        // if (env_ && env_->getIndexBuffer().size() > 0) {
        //     Utils::printInfo("Auto flush remaining {} documents (TSV).", env_->getIndexBuffer().size());
        //     env_->flushIndexBuffer();
        // }

        return true;
    }
} // namespace wiser
