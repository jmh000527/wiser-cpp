/**
 * @file log_init.h
 * @brief 统一日志初始化：控制台 + 滚动文件双 sink
 *
 * 功能：
 * - 控制台 sink 带颜色（spdlog stdout_color_sink）
 * - 可选文件 sink（rotating_file_sink，默认 5MB x 3 个文件）
 * - 统一的日志格式
 * - 简洁的一行初始化 API
 */

#pragma once

#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace wiser {

    struct LogConfig {
        spdlog::level::level_enum level = spdlog::level::info;
        std::string log_file;                   // 为空则不创建文件 sink
        size_t max_file_size = 5 * 1024 * 1024; // 5 MB
        int max_files = 3;
    };

    inline void initLogging(const LogConfig& cfg = {}) {
        std::vector<spdlog::sink_ptr> sinks;

        // 控制台 sink（彩色）
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");
        sinks.push_back(console_sink);

        // 文件 sink（可选）
        if (!cfg.log_file.empty()) {
            try {
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    cfg.log_file, cfg.max_file_size, cfg.max_files);
                file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [tid:%t] %v");
                sinks.push_back(file_sink);
            } catch (const spdlog::spdlog_ex&) {
                // 文件 sink 创建失败时降级为仅控制台
                spdlog::warn("Failed to create log file, using console only");
            }
        }

        auto logger = std::make_shared<spdlog::logger>("wiser", sinks.begin(), sinks.end());
        logger->set_level(cfg.level);
        logger->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(logger);
        spdlog::set_level(cfg.level);
    }

} // namespace wiser
