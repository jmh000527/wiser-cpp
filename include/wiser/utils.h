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
     * @brief 缓冲区类
     * 
     * 用于数据序列化和压缩，支持按字节或按位追加数据。
     */
    class Buffer {
    public:
        /** 
         * @brief 构造缓冲区
         * 
         * 初始时缓冲区为空，位追加位置为 0。
         */
        Buffer();
        ~Buffer() = default;

        // 不可复制，可移动
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        Buffer(Buffer&&) = default;
        Buffer& operator=(Buffer&&) = default;

        /** 
         * @brief 获取底层数据指针（只读）
         * @return 指向缓冲区数据的常量指针
         */
        [[nodiscard]] const char* data() const {
            return buffer_.data();
        }

        /** 
         * @brief 获取当前字节大小
         * @return 缓冲区中的字节数
         */
        [[nodiscard]] size_t size() const {
            return buffer_.size();
        }

        /** 
         * @brief 清空缓冲区
         * 
         * 清除所有数据并将位位置重置为 0。
         */
        void clear();

        /**
         * @brief 追加原始数据块
         * @param data 指向数据的指针
         * @param size 数据块的字节大小
         */
        void append(const void* data, size_t size);

        /**
         * @brief 以位为单位追加数据（高位在前）
         * @param bit 要追加的位（0 或 1）
         */
        void appendBit(std::int32_t bit);

        /** 
         * @brief 获取可写缓冲区
         * @return 缓冲区的引用（用于兼容 C API 等场景）
         */
        std::vector<char>& getBuffer() {
            return buffer_;
        }

        /** 
         * @brief 获取只读缓冲区
         * @return 缓冲区的常量引用
         */
        [[nodiscard]] const std::vector<char>& getBuffer() const {
            return buffer_;
        }

    private:
        std::vector<char> buffer_;
        std::int32_t bit_position_ = 0; ///< 当前位位置（0-7）
    };

    /**
     * @brief 工具函数类
     * 
     * 提供字符编码转换、字符串处理、分词辅助等静态工具函数。
     */
    class Utils {
    public:
        /**
         * @brief 将 UTF-32 序列转换为 UTF-8 字符串
         * @param utf32_str UTF-32 字符数组
         * @return 转换后的 UTF-8 字符串
         */
        static std::string utf32ToUtf8(const std::vector<UTF32Char>& utf32_str);

        /**
         * @brief 将 UTF-8 字符串转换为 UTF-32 序列
         * @param utf8_str UTF-8 字符串
         * @return 转换后的 UTF-32 字符数组
         */
        static std::vector<UTF32Char> utf8ToUtf32(const std::string& utf8_str);

        /**
         * @brief 计算 UTF-32 序列转换为 UTF-8 所需的字节数
         * @param utf32_str UTF-32 字符数组
         * @return 预计需要的字节数
         */
        static std::int32_t calculateUtf8Size(const std::vector<UTF32Char>& utf32_str);

        /**
         * @brief 打印距离上次调用的时间差（毫秒）
         * 
         * 内部使用静态时间点与 spdlog 输出，便于进行粗略的性能观测。
         */
        static void printTimeDiff();

        // ---- 通用字符与字符串工具 ----

        /** 
         * @brief 判断是否为应忽略的分隔标点字符（UTF-32）
         * 
         * 逻辑与分词器中的处理保持一致。
         * @param ch UTF-32 字符
         * @return 如果是应忽略的字符则返回 true，否则返回 false
         */
        static bool isIgnoredChar(UTF32Char ch);

        /** 
         * @brief 将 ASCII 字符转换为小写（就地修改）
         * 
         * 非 ASCII 字符保持不变。
         * @param[in,out] s 要修改的字符串
         */
        static void toLowerAsciiInPlace(std::string& s);

        /** 
         * @brief 返回转换为 ASCII 小写后的字符串副本
         * 
         * 非 ASCII 字符保持不变。
         * @param s 输入字符串
         * @return 转换后的新字符串
         */
        static std::string toLowerAsciiCopy(std::string s) {
            toLowerAsciiInPlace(s);
            return s;
        }

        /** 
         * @brief 判断字符串 s 是否以 ext 结尾（忽略 ASCII 大小写）
         * @param s 待检查的字符串
         * @param ext 后缀字符串
         * @return 如果 s 以 ext 结尾则返回 true
         */
        static bool endsWithIgnoreCase(const std::string& s, const std::string& ext);

        /**
         * @brief 对查询字符串进行分词处理
         * 
         * 用于搜索高亮等场景。处理逻辑包括：
         * - 忽略 Utils::isIgnoredChar 返回 true 的字符
         * - 对 ASCII 范围字符进行小写归一化
         * - 生成长度为 n 的 n-gram，并按出现顺序去重
         * 
         * @param q 查询字符串
         * @param n n-gram 的 n 值
         * @return 分词后的字符串列表
         */
        static std::vector<std::string> tokenizeQueryTokens(const std::string& q, int n);

        /**
         * @brief 对字符串进行 JSON 转义
         * @param s 原始字符串
         * @return 转义后的 JSON 字符串
         */
        static std::string json_escape(const std::string& s);

    private:
        Utils() = default;
    };
} // namespace wiser
