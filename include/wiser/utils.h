#pragma once

#include <cstdio>
#include <sstream>
#include <string_view>

#include "types.h"
#include <string>
#include <vector>
#include <memory>

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
        static void printError(std::string_view fmt, const Args&... args) {
            std::string s = format(fmt, args...);
            std::fputs("[ERROR] ", stderr);
            std::fputs(s.c_str(), stderr);
            std::fputc('\n', stderr);
            std::fflush(stderr);
        }

        /**
         * 打印普通信息（到stdout），现代C++可变模板与 {} 占位符顺序替换
         * 例如：Utils::printInfo("Loaded: {} items", count);
         */
        template<class... Args>
        static void printInfo(std::string_view fmt, const Args&... args) {
            std::string s = format(fmt, args...);
            std::fputs("[INFO] ", stdout);
            std::fputs(s.c_str(), stdout);
            std::fputc('\n', stdout);
            std::fflush(stdout);
        }

        /**
         * 打印距离上次调用的时间差（毫秒）
         */
        static void printTimeDiff();

    private:
        Utils() = default;

        // 轻量格式化：顺序将 {} 替换为参数的流式字符串表示
        static void appendUntilPlaceholder(std::string& out, std::string_view& fmt) {
            size_t pos = fmt.find("{}");
            if (pos == std::string_view::npos) {
                out.append(fmt);
                fmt = std::string_view();
            } else {
                out.append(fmt.substr(0, pos));
                fmt.remove_prefix(pos + 2);
            }
        }

        static void formatInto(std::string& out, std::string_view& fmt) {
            // 无更多参数：附加剩余格式串
            out.append(fmt);
            fmt = std::string_view();
        }

        template<class T, class... Rest>
        static void formatInto(std::string& out, std::string_view& fmt, const T& value, const Rest&... rest) {
            appendUntilPlaceholder(out, fmt);
            std::ostringstream oss;
            oss << value;
            out.append(oss.str());
            formatInto(out, fmt, rest...);
        }

        template<class... Args>
        static std::string format(std::string_view fmt, const Args&... args) {
            std::string out;
            out.reserve(fmt.size() + sizeof...(Args) * 8);
            formatInto(out, fmt, args...);
            return out;
        }
    };
} // namespace wiser
