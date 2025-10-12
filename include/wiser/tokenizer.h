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
     * 分词器类 - 负责将文本转换为词元和倒排列表
     */
    class Tokenizer {
    public:
        /**
         * 构造分词器
         * @param env 环境指针
         */
        explicit Tokenizer(WiserEnvironment* env);
        ~Tokenizer() = default;

        // 不可复制，可移动
        Tokenizer(const Tokenizer&) = delete;
        Tokenizer& operator=(const Tokenizer&) = delete;
        Tokenizer(Tokenizer&&) = default;
        Tokenizer& operator=(Tokenizer&&) = default;

        /**
         * 将 UTF-32 文本转换为倒排列表
         * @param document_id 文档ID
         * @param text UTF-32 文本
         * @param index 输出倒排索引
         */
        void textToPostingsLists(DocId document_id,
                                 const std::vector<UTF32Char>& text,
                                 InvertedIndex& index);

        /**
         * 便捷重载：直接传入 UTF-8 文本，内部完成转换
         * @param document_id 文档ID
         * @param utf8_text UTF-8 文本
         * @param index 输出倒排索引
         */
        void textToPostingsLists(DocId document_id,
                                 std::string_view utf8_text,
                                 InvertedIndex& index);

        /**
         * 将单个词元添加到倒排列表
         * @param document_id 文档ID
         * @param token 词元字符串
         * @param position 词元在文档中的位置
         * @param index 输出倒排索引
         */
        void tokenToPostingsList(DocId document_id,
                                 const std::string& token,
                                 Position position,
                                 InvertedIndex& index);

        /**
         * 输出词元信息（调试用）
         * @param token_id 词元ID
         */
        void dumpToken(TokenId token_id);

    private:
        WiserEnvironment* env_;

        // 辅助函数
        /**
         * 提取 N-gram 字符串
         * @param text UTF-32 文本
         * @param start 起始下标
         * @param n N 值或长度
         * @return UTF-8 词元
         */
        std::string extractNGram(const std::vector<UTF32Char>& text,
                                 size_t start, std::int32_t n);
    };
} // namespace wiser
