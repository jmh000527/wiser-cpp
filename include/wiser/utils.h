#pragma once

#include <cstdio>
#include <sstream>
#include <string_view>

#include "types.h"
#include <string>
#include <vector>
#include <memory>
#include <format>
#include <utility>

namespace wiser {
    /**
     * 缓冲区类 - 用于数据序列化和压缩
     */
    class Buffer {
    public:
        /** 构造缓冲区 */
        Buffer();
        ~Buffer() = default;

        // 不可复制，可移动
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        Buffer(Buffer&&) = default;
        Buffer& operator=(Buffer&&) = default;

        /** 获取底层数据指针（只读） */
        [[nodiscard]] const char* data() const {
            return buffer_.data();
        }

        /** 获取当前字节大小 */
        [[nodiscard]] size_t size() const {
            return buffer_.size();
        }

        /** 清空缓冲区 */
        void clear();

        /**
         * 追加原始数据块
         * @param data 指向数据的指针
         * @param size 字节大小
         */
        void append(const void* data, size_t size);

        /**
         * 以位为单位追加（高位在前）
         * @param bit 0或1
         */
        void appendBit(std::int32_t bit);

        /** 获取可写缓冲区（用于兼容C API） */
        std::vector<char>& getBuffer() {
            return buffer_;
        }

        /** 获取只读缓冲区 */
        [[nodiscard]] const std::vector<char>& getBuffer() const {
            return buffer_;
        }

    private:
        std::vector<char> buffer_;
        std::int32_t bit_position_ = 0; // 当前位位置
    };

    /**
     * 工具函数类
     */
    class Utils {
    public:
        /**
         * 将 UTF-32 转为 UTF-8
         * @param utf32_str UTF-32 字符数组
         * @return UTF-8 字符串
         */
        static std::string utf32ToUtf8(const std::vector<UTF32Char>& utf32_str);

        /**
         * 将 UTF-8 转为 UTF-32
         * @param utf8_str UTF-8 字符串
         * @return UTF-32 字符数组
         */
        static std::vector<UTF32Char> utf8ToUtf32(const std::string& utf8_str);

        /**
         * 计算 UTF-32 序列转为 UTF-8 所需字节数
         * @param utf32_str UTF-32 字符数组
         * @return 预计字节数
         */
        static std::int32_t calculateUtf8Size(const std::vector<UTF32Char>& utf32_str);

        /**
         * 打印错误日志（到stderr），现代C++可变模板与 {} 占位符顺序替换
         * 例如：Utils::printError("Failed: {} {}", code, msg);
         */
        template<class... Args>
        static void printError(std::format_string<Args...> fmt, Args&&... args) {
            std::string s = std::format(fmt, std::forward<Args>(args)...);
            if (!s.empty() && s[0] == '\n') {
                std::fputc('\n', stderr);
                s = s.substr(1);
            }
            std::fputs("[ERROR] ", stderr);
            std::fputs(s.c_str(), stderr);
            std::fflush(stderr);
        }

        /**
         * 打印普通信息（到stdout），现代C++可变模板与 {} 占位符顺序替换
         * 例如：Utils::printInfo("Loaded: {} items", count);
         */
        template<class... Args>
        static void printInfo(std::format_string<Args...> fmt, Args&&... args) {
            std::string s = std::format(fmt, std::forward<Args>(args)...);
            if (!s.empty() && s[0] == '\n') {
                std::fputc('\n', stdout);
                s = s.substr(1);
            }
            std::fputs("[INFO] ", stdout);
            std::fputs(s.c_str(), stdout);
            std::fflush(stdout);
        }

        /**
         * 打印距离上次调用的时间差（毫秒）
         */
        static void printTimeDiff();

    private:
        Utils() = default;
    };
} // namespace wiser
