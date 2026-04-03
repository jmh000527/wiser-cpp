/**
 * @file progress_bar.h
 * @brief 统一进度条组件：支持速率、ETA、已用时间
 *
 * 功能：
 * - Unicode 平滑进度条（8 级子块）
 * - 实时速率显示（docs/sec）
 * - 预估剩余时间（ETA）
 * - 已用时间（elapsed）
 * - 自动限频刷新（避免过度输出导致卡顿）
 * - 已知总量和未知总量两种模式
 */

#pragma once

#include <string>
#include <chrono>
#include <iostream>
#include <cstdint>
#include <cstdio>

namespace wiser {

    class ProgressBar {
    public:
        /// @param total 总条目数（0 表示未知）
        /// @param label 前置标签（如 "Indexing"）
        /// @param bar_width 进度条字符宽度
        /// @param out 输出流（默认 stderr）
        explicit ProgressBar(uint64_t total = 0,
                             std::string label = "Progress",
                             int bar_width = 30,
                             std::ostream& out = std::cerr)
            : total_(total)
            , label_(std::move(label))
            , bar_width_(bar_width)
            , out_(out)
            , start_(std::chrono::steady_clock::now())
            , last_draw_(start_) {}

        /// 更新进度（设置当前已完成数）
        void update(uint64_t current) {
            current_ = current;
            auto now = std::chrono::steady_clock::now();
            // 限频：至少间隔 50ms 或百分比变化才重绘
            auto elapsed_since_draw = std::chrono::duration<double>(now - last_draw_).count();
            if (elapsed_since_draw < 0.05 && !isComplete()) {
                return;
            }
            last_draw_ = now;
            draw();
        }

        /// 自增并更新
        void tick() { update(current_ + 1); }

        /// 完成（强制绘制 100%，换行）
        void finish() {
            current_ = total_ > 0 ? total_ : current_;
            draw();
            out_ << std::endl;
            finished_ = true;
        }

        /// 是否已完成
        bool isComplete() const {
            return total_ > 0 && current_ >= total_;
        }

        /// 获取已用秒数
        double elapsed() const {
            return std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start_).count();
        }

    private:
        void draw() {
            double elapsed_sec = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start_).count();
            double speed = elapsed_sec > 0.01 ? static_cast<double>(current_) / elapsed_sec : 0.0;

            char buf[256];

            if (total_ > 0) {
                // 已知总量模式
                double ratio = static_cast<double>(current_) / static_cast<double>(total_);
                if (ratio > 1.0) ratio = 1.0;
                int percent = static_cast<int>(ratio * 100.0);

                // Unicode 平滑进度条（8 级子块）
                // █ = full, 空 = empty, 以及 ▏▎▍▌▋▊▉ 中间态
                double filled_exact = ratio * bar_width_;
                int filled_full = static_cast<int>(filled_exact);
                int frac = static_cast<int>((filled_exact - filled_full) * 8.0);

                // 构建进度条
                std::string bar;
                bar.reserve(bar_width_ * 4);
                for (int i = 0; i < filled_full && i < bar_width_; ++i)
                    bar += "\xe2\x96\x88"; // █

                if (filled_full < bar_width_ && frac > 0) {
                    // ▏(1/8) ▎(2/8) ▍(3/8) ▌(4/8) ▋(5/8) ▊(6/8) ▉(7/8)
                    static const char* sub_blocks[] = {
                        " ",
                        "\xe2\x96\x8f", // ▏
                        "\xe2\x96\x8e", // ▎
                        "\xe2\x96\x8d", // ▍
                        "\xe2\x96\x8c", // ▌
                        "\xe2\x96\x8b", // ▋
                        "\xe2\x96\x8a", // ▊
                        "\xe2\x96\x89", // ▉
                    };
                    bar += sub_blocks[frac];
                    for (int i = filled_full + 1; i < bar_width_; ++i)
                        bar += " ";
                } else {
                    for (int i = filled_full; i < bar_width_; ++i)
                        bar += " ";
                }

                // ETA
                std::string eta_str;
                if (current_ > 0 && current_ < total_) {
                    double remaining = (elapsed_sec / static_cast<double>(current_))
                                       * static_cast<double>(total_ - current_);
                    eta_str = formatTime(remaining);
                } else if (current_ >= total_) {
                    eta_str = "0s";
                } else {
                    eta_str = "--:--";
                }

                std::snprintf(buf, sizeof(buf),
                    "\r\033[36m%s\033[0m \033[90m[\033[0m\033[32m%s\033[0m\033[90m]\033[0m %3d%% \033[90m%llu/%llu\033[0m \033[33m%.0f/s\033[0m \033[90mETA\033[0m %s \033[90m[%s]\033[0m",
                    label_.c_str(),
                    bar.c_str(),
                    percent,
                    static_cast<unsigned long long>(current_),
                    static_cast<unsigned long long>(total_),
                    speed,
                    eta_str.c_str(),
                    formatTime(elapsed_sec).c_str());
            } else {
                // 未知总量模式：旋转动画 + 计数
                static const char* spinner[] = {"\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8", "\xe2\xa0\xbc", "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\xa7", "\xe2\xa0\x87", "\xe2\xa0\x8f"};
                int idx = static_cast<int>(elapsed_sec * 10) % 10;
                std::snprintf(buf, sizeof(buf),
                    "\r\033[36m%s\033[0m %s \033[33m%llu\033[0m processed \033[90m%.0f/s [%s]\033[0m",
                    label_.c_str(),
                    spinner[idx],
                    static_cast<unsigned long long>(current_),
                    speed,
                    formatTime(elapsed_sec).c_str());
            }

            out_ << buf << std::flush;
        }

        static std::string formatTime(double seconds) {
            if (seconds < 1.0) {
                return "<1s";
            } else if (seconds < 60.0) {
                char b[8];
                std::snprintf(b, sizeof(b), "%.0fs", seconds);
                return b;
            } else if (seconds < 3600.0) {
                int m = static_cast<int>(seconds) / 60;
                int s = static_cast<int>(seconds) % 60;
                char b[16];
                std::snprintf(b, sizeof(b), "%dm%02ds", m, s);
                return b;
            } else {
                int h = static_cast<int>(seconds) / 3600;
                int m = (static_cast<int>(seconds) % 3600) / 60;
                char b[16];
                std::snprintf(b, sizeof(b), "%dh%02dm", h, m);
                return b;
            }
        }

        uint64_t total_;
        uint64_t current_ = 0;
        std::string label_;
        int bar_width_;
        std::ostream& out_;
        std::chrono::steady_clock::time_point start_;
        std::chrono::steady_clock::time_point last_draw_;
        bool finished_ = false;
    };

} // namespace wiser
