#include "wiser/tokenizer.h"
#include "wiser/wiser_environment.h"
#include "wiser/utils.h"
#include <algorithm>
#include <cctype>
#include <string_view>

namespace wiser {
    Tokenizer::Tokenizer(WiserEnvironment* env)
        : env_(env) {}

    bool isIgnoredChar(UTF32Char ch) {
        // 基本ASCII标点符号和空白字符
        if (ch <= 127) {
            return std::isspace(static_cast<unsigned char>(ch)) || std::ispunct(static_cast<unsigned char>(ch));
        }

        // 中文标点符号
        switch (ch) {
            case 0x3000: // 全角空格
            case 0x3001: // 、
            case 0x3002: // 。
            case 0xFF08: // （
            case 0xFF09: // ）
            case 0xFF01: // ！
            case 0xFF0C: // ，
            case 0xFF1A: // ：
            case 0xFF1B: // ；
            case 0xFF1F: // ？
            case 0xFF3B: // ［
            case 0xFF3D: // ］
            case 0x201C: // "
            case 0x201D: // "
            case 0x2018: // '
            case 0x2019: // '
                return true;
            default:
                return false;
        }
    }

    // 提取N-gram的下一个词元
    struct NGramResult {
        size_t start;
        size_t length;
        bool has_more;
    };

    NGramResult getNextNGram(const std::vector<UTF32Char>& text, size_t& pos, std::int32_t n) {
        size_t len = text.size();

        // 跳过开头的忽略字符
        while (pos < len && isIgnoredChar(text[pos])) {
            ++pos;
        }

        if (pos >= len) {
            return { pos, 0, false };
        }

        size_t start = pos;
        size_t count = 0;

        // 收集最多n个非忽略字符
        while (pos < len && count < static_cast<size_t>(n) && !isIgnoredChar(text[pos])) {
            ++pos;
            ++count;
        }

        return { start, count, pos < len };
    }

    void Tokenizer::textToPostingsLists(DocId document_id,
                                        const std::vector<UTF32Char>& text,
                                        InvertedIndex& index) {
        size_t pos = 0;
        Position position = 0;
        const std::int32_t n = env_->getTokenLength();
        while (pos < text.size()) {
            auto ngram_result = getNextNGram(text, pos, n);
            if (ngram_result.length == 0) break;
            if (ngram_result.length >= static_cast<size_t>(n)) {
                std::string token = extractNGram(text, ngram_result.start,
                                                 static_cast<std::int32_t>(ngram_result.length));
                tokenToPostingsList(document_id, token, position, index);
                ++position;
            }
            pos = ngram_result.start + 1;
        }
    }

    void Tokenizer::textToPostingsLists(DocId document_id,
                                        std::string_view utf8_text,
                                        InvertedIndex& index) {
        std::string s{ utf8_text };
        auto utf32 = Utils::utf8ToUtf32(s);
        textToPostingsLists(document_id, utf32, index);
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

        // 添加到索引
        index.addPosting(token_id, document_id, position);
    }

    void Tokenizer::dumpToken(TokenId token_id) {
        std::string token = env_->getDatabase().getToken(token_id);
        if (!token.empty()) {
            spdlog::info("Token {}: {}", token_id, token);

            // 获取倒排列表信息（现代接口）
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

        // 统一小写（仅对 ASCII 范围字符），避免大小写导致的查询/索引不一致
        for (auto& ch: ngram) {
            if (ch <= 127) {
                ch = static_cast<UTF32Char>(std::tolower(static_cast<unsigned char>(ch)));
            }
        }

        return Utils::utf32ToUtf8(ngram);
    }
} // namespace wiser
