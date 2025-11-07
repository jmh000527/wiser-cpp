#pragma once

/**
 * @file utils.h
 * @brief 工具与低层数据缓冲封装，提供UTF-8/UTF-32转换等辅助函数。
 */

#include <cstdio>
#include <sstream>
#include <string_view>

#include "types.h"
#include <string>
#include <vector>
#include <memory>
#include <utility>
#include <unordered_set>

// 使用 spdlog 作为统一日志库
#include <spdlog/spdlog.h>

namespace wiser {
    /**
     * 缓冲区类 - 用于数据序列化和压缩
     */
    class Buffer {
    public:
        /** 构造缓冲区（位追加初始为bit_position_=0） */
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
         * @brief 打印距离上次调用的时间差（毫秒）。
         * 内部使用静态时间点与spdlog输出，便于性能粗略观测。
         */
        static void printTimeDiff();

        // ---- 通用字符与字符串工具（由原先各处lambda/匿名函数上移） ----
        /** 判断是否为应忽略的分隔标点字符（UTF-32），与分词逻辑保持一致 */
        static bool isIgnoredChar(UTF32Char ch);

        /** 将 ASCII 字符小写（就地修改），非 ASCII 保持不变 */
        static void toLowerAsciiInPlace(std::string& s);

        /** 返回转换为 ASCII 小写后的副本（非 ASCII 保持不变） */
        static std::string toLowerAsciiCopy(std::string s) {
            toLowerAsciiInPlace(s);
            return s;
        }

        /** 判断 s 是否以 ext 结尾（忽略 ASCII 大小写） */
        static bool endsWithIgnoreCase(const std::string& s, const std::string& ext);

        /**
         * 基于 UTF-32 分割与 n-gram 的查询分词，用于高亮等场景。
         * - 忽略 Utils::isIgnoredChar 返回 true 的码点
         * - 对 ASCII 范围小写归一
         * - 生成长度为 n 的 n-gram，按出现顺序去重
         */
        static std::vector<std::string> tokenizeQueryTokens(const std::string& q, int n);

        static std::string json_escape(const std::string& s);

    private:
        Utils() = default;
    };
} // namespace wiser
