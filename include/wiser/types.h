#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace wiser {

    // 基本类型定义
    using UTF32Char = std::uint32_t;   // UTF-32 编码的 Unicode 字符
    using DocId     = std::int32_t;    // 文档ID
    using TokenId   = std::int32_t;    // 词元ID
    using Position  = std::int32_t;    // 词在文档中的位置（以 n-gram 为单位）
    using Count     = std::int32_t;    // 计数类型（文档数、位置数等）

    constexpr std::int32_t MAX_UTF8_SIZE = 4;  // UTF-8 表示1个Unicode字符最多需要的字节数
    constexpr std::int32_t N_GRAM = 2;         // 默认 bi-gram

    // 压缩方法枚举
    enum class CompressMethod {
        NONE,   // 不压缩
        GOLOMB  // 使用Golomb编码压缩
    };

    // 前向声明
    class PostingsList;
    class Database;
    class SearchEngine;
    class Tokenizer;
    class WikiLoader;
    class Utils;

} // namespace wiser