/**
 * @file types.h
 * @brief 基础类型定义与全局常量。
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace wiser {
    /**
     * @brief UTF-32 编码的 Unicode 字符
     */
    using UTF32Char = std::uint32_t;

    /**
     * @brief 文档 ID 类型
     */
    using DocId = std::int32_t;

    /**
     * @brief 词元 ID 类型
     */
    using TokenId = std::int32_t;

    /**
     * @brief 词在文档中的位置类型（以 n-gram 为单位）
     */
    using Position = std::int32_t;

    /**
     * @brief 计数类型（用于文档数、位置数等）
     */
    using Count = std::int32_t;

    /**
     * @brief UTF-8 表示1个 Unicode 字符最多需要的字节数
     */
    constexpr std::int32_t MAX_UTF8_SIZE = 4;

    /**
     * @brief 默认 n-gram 长度 (bi-gram)
     */
    constexpr std::int32_t N_GRAM = 2;

    /**
     * @brief 倒排列表压缩方法枚举
     */
    enum class CompressMethod {
        NONE,  ///< 不压缩
        GOLOMB ///< 使用 Golomb 编码压缩
    };

    // 前向声明
    class PostingsList;
    class Database;
    class SearchEngine;
    class Tokenizer;
    class WikiLoader;
    class Utils;
} // namespace wiser
