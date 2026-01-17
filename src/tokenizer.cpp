/**
 * @file tokenizer.cpp
 * @brief 分词器实现：将文本转换为 N-gram 词元并写入倒排索引
 *
 * 主要职责：
 * - 将输入文本（UTF-8/UTF-32）按环境配置的 N 值切分为 N-gram
 * - 对 ASCII 字符进行小写化，与查询侧归一化保持一致
 * - 将 (token_id, doc_id, position) 追加到内存倒排索引中
 */

#include "wiser/tokenizer.h"
#include "wiser/wiser_environment.h"
#include "wiser/utils.h"
#include <algorithm>
#include <cctype>
#include <string_view>

namespace wiser {
    Tokenizer::Tokenizer(WiserEnvironment* env)
        : env_(env) {}

    // N-gram 扫描的返回结果：包含本次 N-gram 起点、长度以及是否仍有后续内容
    struct NGramResult {
        size_t start;
        size_t length;
        bool has_more;
    };

    static NGramResult getNextNGram(const std::vector<UTF32Char>& text, size_t& pos, std::int32_t n) {
        size_t len = text.size();
        // 跳过忽略字符（空白、标点等），定位到下一个可参与分词的位置
        while (pos < len && Utils::isIgnoredChar(text[pos])) {
            ++pos;
        }
        if (pos >= len) {
            return { pos, 0, false };
        }
        size_t start = pos;
        size_t count = 0;
        // 从 start 开始连续读入最多 n 个非忽略字符，构成一个候选 N-gram
        while (pos < len && count < static_cast<size_t>(n) && !Utils::isIgnoredChar(text[pos])) {
            ++pos;
            ++count;
        }
        return { start, count, pos < len };
    }

    int Tokenizer::textToPostingsLists(DocId document_id,
                                        const std::vector<UTF32Char>& text,
                                        InvertedIndex& index) {
        // pos：UTF-32 字符索引；position：token 在文档中的序号（用于短语搜索的相邻验证）
        size_t pos = 0;
        Position position = 0;
        // N 值来自环境配置（Config::token_len）
        const std::int32_t n = env_->getTokenLength();
        while (pos < text.size()) {
            // 读取下一个候选 token 的范围
            auto ngram_result = getNextNGram(text, pos, n);
            if (ngram_result.length == 0)
                break;
            if (ngram_result.length >= static_cast<size_t>(n)) {
                // 将 [start, start+length) 转为 UTF-8 token，并写入倒排索引
                std::string token = extractNGram(text, ngram_result.start,
                                                 static_cast<std::int32_t>(ngram_result.length));
                tokenToPostingsList(document_id, token, position, index);
                ++position;
            }
            // 滑动窗口：从 start+1 继续尝试，形成重叠 N-gram
            pos = ngram_result.start + 1;
        }
        // position 即写入的 token 数量
        return static_cast<int>(position);
    }

    int Tokenizer::textToPostingsLists(DocId document_id,
                                        std::string_view utf8_text,
                                        InvertedIndex& index) {
        // 将 UTF-8 先转换为 UTF-32 再处理，避免按字节切分造成 Unicode 破坏
        std::string s{ utf8_text };
        auto utf32 = Utils::utf8ToUtf32(s);
        return textToPostingsLists(document_id, utf32, index);
    }

    void Tokenizer::tokenToPostingsList(DocId document_id,
                                        const std::string& token,
                                        Position position,
                                        InvertedIndex& index) {
        // 获取或创建词元ID
        auto info = env_->getDatabase().getTokenInfo(token, true);
        if (!info.has_value() || info->id <= 0) {
            spdlog::error("Failed to get token ID for token: {}", token);
            return;
        }
        TokenId token_id = info->id;

        // 将该 token 在该文档出现的位置写入内存倒排索引
        index.addPosting(token_id, document_id, position);
    }

    void Tokenizer::dumpToken(TokenId token_id) {
        // 读取 token 文本并输出基础信息（调试辅助）
        std::string token = env_->getDatabase().getToken(token_id);
        if (!token.empty()) {
            spdlog::info("Token {}: {}", token_id, token);

            // 获取倒排列表信息
            auto rec = env_->getDatabase().getPostings(token_id);
            Count docs_count = rec ? rec->docs_count : 0;
            size_t sz = rec ? rec->postings.size() : 0u;

            spdlog::info("Documents: {}, Postings size: {} bytes", docs_count, sz);
        } else {
            spdlog::error("Token {}: not found", token_id);
        }
    }

    std::string Tokenizer::extractNGram(const std::vector<UTF32Char>& text,
                                        size_t start, std::int32_t length) {
        if (start >= text.size() || length <= 0) {
            return "";
        }
        size_t end = std::min(start + static_cast<size_t>(length), text.size());
        std::vector<UTF32Char> ngram(text.begin() + start, text.begin() + end);
        // ASCII 字符统一小写化，保证大小写不敏感匹配
        for (auto& ch: ngram) {
            if (ch <= 127) {
                ch = static_cast<UTF32Char>(std::tolower(static_cast<unsigned char>(ch)));
            }
        }
        return Utils::utf32ToUtf8(ngram);
    }
} // namespace wiser
