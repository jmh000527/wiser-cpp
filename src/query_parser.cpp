/**
 * @file query_parser.cpp
 * @brief 布尔查询解析器实现
 *
 * 递归下降解析器，将查询字符串转换为表达式树（AST）。
 * 支持 AND / OR / NOT 操作符、引号短语和括号分组。
 */

#include "wiser/query_parser.h"
#include <cctype>
#include <algorithm>

namespace wiser {

    // ========== QueryNode 工厂方法 ==========

    std::unique_ptr<QueryNode> QueryNode::makeTerm(std::string text) {
        auto node = std::make_unique<QueryNode>();
        node->type = QueryNodeType::TERM;
        node->value = std::move(text);
        return node;
    }

    std::unique_ptr<QueryNode> QueryNode::makePhrase(std::string text) {
        auto node = std::make_unique<QueryNode>();
        node->type = QueryNodeType::PHRASE;
        node->value = std::move(text);
        return node;
    }

    std::unique_ptr<QueryNode> QueryNode::makeAnd(
        std::unique_ptr<QueryNode> l, std::unique_ptr<QueryNode> r) {
        auto node = std::make_unique<QueryNode>();
        node->type = QueryNodeType::AND_OP;
        node->left = std::move(l);
        node->right = std::move(r);
        return node;
    }

    std::unique_ptr<QueryNode> QueryNode::makeOr(
        std::unique_ptr<QueryNode> l, std::unique_ptr<QueryNode> r) {
        auto node = std::make_unique<QueryNode>();
        node->type = QueryNodeType::OR_OP;
        node->left = std::move(l);
        node->right = std::move(r);
        return node;
    }

    std::unique_ptr<QueryNode> QueryNode::makeNot(std::unique_ptr<QueryNode> op) {
        auto node = std::make_unique<QueryNode>();
        node->type = QueryNodeType::NOT_OP;
        node->operand = std::move(op);
        return node;
    }

    // ========== 词法分析器 ==========

    std::vector<Token> QueryParser::tokenize(std::string_view input) {
        std::vector<Token> result;
        size_t i = 0;

        while (i < input.size()) {
            // 跳过空白
            if (std::isspace(static_cast<unsigned char>(input[i]))) {
                ++i;
                continue;
            }

            // 括号
            if (input[i] == '(') {
                result.push_back({TokenType::LPAREN, "("});
                ++i;
                continue;
            }
            if (input[i] == ')') {
                result.push_back({TokenType::RPAREN, ")"});
                ++i;
                continue;
            }

            // 引号短语
            if (input[i] == '"' || input[i] == '\xe2') {
                // 处理 ASCII 双引号和 UTF-8 中文引号 \xe2\x80\x9c / \xe2\x80\x9d
                char quote_char = input[i];
                size_t start;
                if (quote_char == '\xe2' && i + 2 < input.size() &&
                    input[i + 1] == '\x80' &&
                    (input[i + 2] == '\x9c' || input[i + 2] == '\x9d')) {
                    i += 3; // 跳过 UTF-8 左引号
                    start = i;
                    // 搜索匹配的右引号或 ASCII 引号
                    while (i < input.size()) {
                        if (input[i] == '"') { ++i; break; }
                        if (input[i] == '\xe2' && i + 2 < input.size() &&
                            input[i + 1] == '\x80' &&
                            (input[i + 2] == '\x9c' || input[i + 2] == '\x9d')) {
                            i += 3;
                            break;
                        }
                        ++i;
                    }
                } else {
                    ++i; // 跳过开引号
                    start = i;
                    while (i < input.size() && input[i] != '"')
                        ++i;
                    if (i < input.size())
                        ++i; // 跳过闭引号
                }
                std::string phrase(input.substr(start, i - start));
                // 去掉尾部的引号字符
                while (!phrase.empty() && (phrase.back() == '"' ||
                       static_cast<unsigned char>(phrase.back()) > 0x7f)) {
                    // 简单处理：去除尾部引号标记
                    if (phrase.back() == '"') {
                        phrase.pop_back();
                        break;
                    }
                    break;
                }
                if (!phrase.empty()) {
                    result.push_back({TokenType::PHRASE, phrase});
                }
                continue;
            }

            // 单词（包含多字节 UTF-8 字符）
            size_t start = i;
            while (i < input.size() &&
                   !std::isspace(static_cast<unsigned char>(input[i])) &&
                   input[i] != '(' && input[i] != ')' && input[i] != '"') {
                // 处理多字节 UTF-8 字符
                unsigned char c = static_cast<unsigned char>(input[i]);
                if (c < 0x80) {
                    ++i;
                } else if (c < 0xC0) {
                    ++i; // 续字节
                } else if (c < 0xE0) {
                    i += 2;
                } else if (c < 0xF0) {
                    i += 3;
                } else {
                    i += 4;
                }
                i = std::min(i, input.size());
            }

            std::string word(input.substr(start, i - start));
            if (word.empty())
                continue;

            // 检查是否为关键字（大小写不敏感）
            std::string upper = word;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            if (upper == "AND") {
                result.push_back({TokenType::AND_KW, word});
            } else if (upper == "OR") {
                result.push_back({TokenType::OR_KW, word});
            } else if (upper == "NOT") {
                result.push_back({TokenType::NOT_KW, word});
            } else {
                result.push_back({TokenType::WORD, word});
            }
        }

        result.push_back({TokenType::END_OF_INPUT, ""});
        return result;
    }

    // ========== 解析器辅助 ==========

    const Token& QueryParser::peek() const {
        return tokens_[pos_];
    }

    Token QueryParser::advance() {
        if (!tokens_.empty() && pos_ + 1 < tokens_.size()) {
            return tokens_[pos_++];
        }
        return tokens_.empty() ? Token{TokenType::END_OF_INPUT, ""} : tokens_.back();
    }

    bool QueryParser::match(TokenType type) {
        if (peek().type == type) {
            advance();
            return true;
        }
        return false;
    }

    // ========== 递归下降解析 ==========

    std::unique_ptr<QueryNode> QueryParser::parse(std::string_view query) {
        tokens_ = tokenize(query);
        pos_ = 0;

        if (tokens_.size() <= 1) // 只有 END_OF_INPUT
            return nullptr;

        auto result = parseOrExpr();

        return result;
    }

    // or_expr → and_expr ("OR" and_expr)*
    std::unique_ptr<QueryNode> QueryParser::parseOrExpr() {
        auto left = parseAndExpr();
        if (!left)
            return nullptr;

        while (peek().type == TokenType::OR_KW) {
            advance(); // consume OR
            auto right = parseAndExpr();
            if (!right)
                break;
            left = QueryNode::makeOr(std::move(left), std::move(right));
        }
        return left;
    }

    // and_expr → not_expr (("AND")? not_expr)*
    // 隐式 AND：两个相邻词项之间无操作符时默认为 AND
    std::unique_ptr<QueryNode> QueryParser::parseAndExpr() {
        auto left = parseNotExpr();
        if (!left)
            return nullptr;

        while (true) {
            // 显式 AND
            if (peek().type == TokenType::AND_KW) {
                advance(); // consume AND
                auto right = parseNotExpr();
                if (!right)
                    break;
                left = QueryNode::makeAnd(std::move(left), std::move(right));
                continue;
            }

            // 隐式 AND：下一个 token 是 WORD/PHRASE/NOT/LPAREN 则视为隐式 AND
            auto next = peek().type;
            if (next == TokenType::WORD || next == TokenType::PHRASE ||
                next == TokenType::NOT_KW || next == TokenType::LPAREN) {
                auto right = parseNotExpr();
                if (!right)
                    break;
                left = QueryNode::makeAnd(std::move(left), std::move(right));
                continue;
            }

            break;
        }
        return left;
    }

    // not_expr → "NOT"? primary
    std::unique_ptr<QueryNode> QueryParser::parseNotExpr() {
        if (peek().type == TokenType::NOT_KW) {
            advance(); // consume NOT
            auto operand = parsePrimary();
            if (!operand)
                return nullptr;
            return QueryNode::makeNot(std::move(operand));
        }
        return parsePrimary();
    }

    // primary → PHRASE | WORD | "(" or_expr ")"
    std::unique_ptr<QueryNode> QueryParser::parsePrimary() {
        if (peek().type == TokenType::PHRASE) {
            auto tok = advance();
            return QueryNode::makePhrase(tok.value);
        }

        if (peek().type == TokenType::WORD) {
            auto tok = advance();
            return QueryNode::makeTerm(tok.value);
        }

        if (peek().type == TokenType::LPAREN) {
            advance(); // consume (
            auto expr = parseOrExpr();
            match(TokenType::RPAREN); // consume ) (tolerant if missing)
            return expr;
        }

        return nullptr;
    }

    // ========== 快速检测 ==========

    bool QueryParser::isBooleanQuery(std::string_view query) {
        // 快速检测查询中是否包含布尔操作符或引号
        size_t i = 0;
        while (i < query.size()) {
            if (query[i] == '"')
                return true;
            if (query[i] == '(' || query[i] == ')')
                return true;

            // 检测 AND / OR / NOT 关键字（需前后为空白/边界）
            if (i == 0 || std::isspace(static_cast<unsigned char>(query[i - 1]))) {
                auto remaining = query.substr(i);
                auto endsWord = [&](size_t len) {
                    return i + len >= query.size() ||
                           std::isspace(static_cast<unsigned char>(query[i + len]));
                };

                if (remaining.size() >= 3 &&
                    (remaining[0] == 'A' || remaining[0] == 'a') &&
                    (remaining[1] == 'N' || remaining[1] == 'n') &&
                    (remaining[2] == 'D' || remaining[2] == 'd') &&
                    endsWord(3)) {
                    return true;
                }
                if (remaining.size() >= 2 &&
                    (remaining[0] == 'O' || remaining[0] == 'o') &&
                    (remaining[1] == 'R' || remaining[1] == 'r') &&
                    endsWord(2)) {
                    return true;
                }
                if (remaining.size() >= 3 &&
                    (remaining[0] == 'N' || remaining[0] == 'n') &&
                    (remaining[1] == 'O' || remaining[1] == 'o') &&
                    (remaining[2] == 'T' || remaining[2] == 't') &&
                    endsWord(3)) {
                    return true;
                }
            }
            ++i;
        }
        return false;
    }

} // namespace wiser
