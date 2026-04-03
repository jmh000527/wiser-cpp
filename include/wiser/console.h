/**
 * @file console.h
 * @brief 终端美化工具：ANSI 颜色、Box-drawing 格式化、终端宽度检测
 *
 * 提供统一的终端输出美化能力，包括：
 * - ANSI 256 色与样式控制
 * - Unicode box-drawing 表格渲染
 * - 终端宽度自适应
 * - Windows 终端 VT 模式自动启用
 */

#pragma once

#include <string>
#include <string_view>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace wiser {

    // ─── ANSI escape helpers ────────────────────────────────────
    namespace ansi {
        inline const char* reset     = "\033[0m";
        inline const char* bold      = "\033[1m";
        inline const char* dim       = "\033[2m";
        inline const char* italic    = "\033[3m";
        inline const char* underline = "\033[4m";

        // Foreground colors
        inline const char* black     = "\033[30m";
        inline const char* red       = "\033[31m";
        inline const char* green     = "\033[32m";
        inline const char* yellow    = "\033[33m";
        inline const char* blue      = "\033[34m";
        inline const char* magenta   = "\033[35m";
        inline const char* cyan      = "\033[36m";
        inline const char* white     = "\033[37m";

        // Bright foreground
        inline const char* bright_black   = "\033[90m";
        inline const char* bright_red     = "\033[91m";
        inline const char* bright_green   = "\033[92m";
        inline const char* bright_yellow  = "\033[93m";
        inline const char* bright_blue    = "\033[94m";
        inline const char* bright_magenta = "\033[95m";
        inline const char* bright_cyan    = "\033[96m";
        inline const char* bright_white   = "\033[97m";

        // Background colors
        inline const char* bg_black   = "\033[40m";
        inline const char* bg_blue    = "\033[44m";
        inline const char* bg_cyan    = "\033[46m";
        inline const char* bg_white   = "\033[47m";
    }

    // ─── Console utility class ──────────────────────────────────
    class Console {
    public:
        /// 获取终端宽度（列数），失败时返回 80
        static int terminalWidth() {
#ifdef _WIN32
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &csbi)) {
                return csbi.srWindow.Right - csbi.srWindow.Left + 1;
            }
            return 80;
#else
            struct winsize w{};
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
                return w.ws_col;
            }
            return 80;
#endif
        }

        /// 启用 Windows 控制台的 VT100 (ANSI) 模式
        static void enableAnsi() {
#ifdef _WIN32
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD mode = 0;
                GetConsoleMode(h, &mode);
                SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            }
            h = GetStdHandle(STD_ERROR_HANDLE);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD mode = 0;
                GetConsoleMode(h, &mode);
                SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            }
#endif
        }

        /// 检测终端是否支持颜色（基于 stdout 是否连接 TTY）
        static bool supportsColor() {
#ifdef _WIN32
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD mode = 0;
            return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
#else
            return isatty(STDOUT_FILENO);
#endif
        }

        // ─── Box-drawing 渲染 ───────────────────────────────────

        /// 画一条水平线：─────────────
        static std::string horizontalLine(int width) {
            std::string line;
            line.reserve(width * 3);
            for (int i = 0; i < width; ++i)
                line += "\xe2\x94\x80"; // ─ (U+2500)
            return line;
        }

        /// 顶部边框：┌──────────────────┐
        static std::string topBorder(int width) {
            return "\xe2\x94\x8c" + horizontalLine(width - 2) + "\xe2\x94\x90"; // ┌ ... ┐
        }

        /// 底部边框：└──────────────────┘
        static std::string bottomBorder(int width) {
            return "\xe2\x94\x94" + horizontalLine(width - 2) + "\xe2\x94\x98"; // └ ... ┘
        }

        /// 分隔线：├──────────────────┤
        static std::string separator(int width) {
            return "\xe2\x94\x9c" + horizontalLine(width - 2) + "\xe2\x94\xa4"; // ├ ... ┤
        }

        /// 带色彩的水平线（用于视觉分隔，不需要 box）
        static std::string colorLine(int width, const char* color = ansi::dim) {
            return std::string(color) + horizontalLine(width) + ansi::reset;
        }

        // ─── Score 可视化 ────────────────────────────────────────

        /// 生成得分条：████░░░░░ 72%
        static std::string scoreBar(double score, double max_score, int width = 12) {
            double ratio = max_score > 0.0 ? score / max_score : 0.0;
            if (ratio > 1.0) ratio = 1.0;
            int filled = static_cast<int>(ratio * width);
            std::string bar;
            bar.reserve(width * 4);
            for (int i = 0; i < filled; ++i)
                bar += "\xe2\x96\x88"; // █
            for (int i = filled; i < width; ++i)
                bar += "\xe2\x96\x91"; // ░
            return bar;
        }

        // ─── 格式化辅助 ─────────────────────────────────────────

        /// 按 UTF-8 字符数安全截断
        static std::string truncateUtf8(const std::string& s, size_t max_chars) {
            size_t pos = 0, chars = 0;
            while (pos < s.size() && chars < max_chars) {
                unsigned char c = static_cast<unsigned char>(s[pos]);
                size_t len = 1;
                if ((c & 0x80) == 0)       len = 1;
                else if ((c & 0xE0) == 0xC0) len = 2;
                else if ((c & 0xF0) == 0xE0) len = 3;
                else if ((c & 0xF8) == 0xF0) len = 4;
                if (pos + len > s.size()) break;
                pos += len;
                ++chars;
            }
            if (pos >= s.size()) return s;
            return s.substr(0, pos) + "...";
        }

        /// 归一化空白字符
        static std::string normalizeSpaces(const std::string& s) {
            std::string out;
            out.reserve(s.size());
            bool last_space = false;
            for (char c : s) {
                if (c == '\r' || c == '\n' || c == '\t') c = ' ';
                if (c == ' ') {
                    if (last_space) continue;
                    last_space = true;
                } else {
                    last_space = false;
                }
                out.push_back(c);
            }
            return out;
        }

        /// 格式化字节大小为可读字符串 (e.g., 1.5 MB)
        static std::string formatBytes(uint64_t bytes) {
            const char* units[] = {"B", "KB", "MB", "GB", "TB"};
            double size = static_cast<double>(bytes);
            int unit = 0;
            while (size >= 1024.0 && unit < 4) {
                size /= 1024.0;
                ++unit;
            }
            char buf[32];
            if (unit == 0)
                std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
            else
                std::snprintf(buf, sizeof(buf), "%.1f %s", size, units[unit]);
            return buf;
        }

        /// 格式化持续时间为可读字符串
        static std::string formatDuration(double seconds) {
            if (seconds < 0.001) {
                return std::to_string(static_cast<int>(seconds * 1000000.0)) + "\xc2\xb5s"; // µs
            } else if (seconds < 1.0) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%.1fms", seconds * 1000.0);
                return buf;
            } else if (seconds < 60.0) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%.2fs", seconds);
                return buf;
            } else if (seconds < 3600.0) {
                int m = static_cast<int>(seconds) / 60;
                int s = static_cast<int>(seconds) % 60;
                return std::to_string(m) + "m" + std::to_string(s) + "s";
            } else {
                int h = static_cast<int>(seconds) / 3600;
                int m = (static_cast<int>(seconds) % 3600) / 60;
                return std::to_string(h) + "h" + std::to_string(m) + "m";
            }
        }
    };

} // namespace wiser
