#pragma once

/**
 * @file tokenizer.h
 * @brief 文本分词器，将文本转换为 N-gram 词元和倒排索引。
 */

#pragma once

#include "types.h"
#include "postings.h"
#include <string>
#include <vector>
#include <memory>
#include <string_view>

namespace wiser {
    class WiserEnvironment;

    /**
     * @brief 分词器类
     * 
     * 负责将文本转换为词元（Tokens）并构建倒排列表（Postings List）。
     */
    class Tokenizer {
    public:
        /**
         * @brief 构造分词器
         * @param env 环境指针（需在分词器生命周期内有效）
         */
        explicit Tokenizer(WiserEnvironment* env);
        ~Tokenizer() = default;

        // 不可复制，可移动
        Tokenizer(const Tokenizer&) = delete;
        Tokenizer& operator=(const Tokenizer&) = delete;
        Tokenizer(Tokenizer&&) = default;
        Tokenizer& operator=(Tokenizer&&) = default;

        /**
         * @brief 将 UTF-32 文本转换为倒排列表
         * @param document_id 文档 ID
         * @param text UTF-32 文本
         * @param index 输出倒排索引（引用，将在原有基础上追加）
         * @return 生成的 token 数量
         */
        int textToPostingsLists(DocId document_id,
                                const std::vector<UTF32Char>& text,
                                InvertedIndex& index);

        /**
         * @brief 将 UTF-8 文本转换为倒排列表
         * 
         * 内部完成 UTF-8 到 UTF-32 的转换，并进行 ASCII 小写化，与查询侧逻辑保持一致。
         * @param document_id 文档 ID
         * @param utf8_text UTF-8 文本
         * @param index 输出倒排索引
         * @return 生成的 token 数量
         */
        int textToPostingsLists(DocId document_id,
                                std::string_view utf8_text,
                                InvertedIndex& index);

        /**
         * @brief 将单个词元添加到倒排列表
         * @param document_id 文档 ID
         * @param token 词元字符串
         * @param position 词元在文档中的位置
         * @param index 输出倒排索引
         */
        void tokenToPostingsList(DocId document_id,
                                 const std::string& token,
                                 Position position,
                                 InvertedIndex& index);

        /**
         * @brief 输出词元信息（调试用）
         * @param token_id 词元 ID
         */
        void dumpToken(TokenId token_id);

    private:
        WiserEnvironment* env_;

        // 辅助函数
        /**
         * @brief 提取 N-gram 字符串
         * @param text UTF-32 文本
         * @param start 起始下标
         * @param n N 值或长度
         * @return UTF-8 编码的词元
         */
        std::string extractNGram(const std::vector<UTF32Char>& text,
                                 size_t start, std::int32_t n);
    };
} // namespace wiser
