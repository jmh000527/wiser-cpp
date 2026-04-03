/**
 * @file query_parser.h
 * @brief 布尔查询解析器，支持 AND/OR/NOT、引号短语和括号分组
 *
 * 语法（递归下降）:
 *   query     → or_expr
 *   or_expr   → and_expr ("OR" and_expr)*
 *   and_expr  → not_expr (("AND")? not_expr)*
 *   not_expr  → "NOT"? primary
 *   primary   → PHRASE | TERM | "(" query ")"
 *
 * 隐式 AND：相邻的两个词项默认视为 AND 关系。
 * 引号短语 "hello world" 作为短语匹配整体处理。
 */

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace wiser {

    /**
     * @brief 查询表达式节点类型
     */
    enum class QueryNodeType {
        TERM,       ///< 单个搜索词（会被分解为 N-gram）
        PHRASE,     ///< 引号短语，要求位置相邻
        AND_OP,     ///< 左右子树的交集
        OR_OP,      ///< 左右子树的并集
        NOT_OP,     ///< 排除操作数匹配的文档（一元）
    };

    /**
     * @brief 查询表达式树节点
     */
    struct QueryNode {
        QueryNodeType type;
        std::string value;                     ///< TERM/PHRASE 的文本内容
        std::unique_ptr<QueryNode> left;       ///< AND/OR 左子节点
        std::unique_ptr<QueryNode> right;      ///< AND/OR 右子节点
        std::unique_ptr<QueryNode> operand;    ///< NOT 的操作数

        static std::unique_ptr<QueryNode> makeTerm(std::string text);
        static std::unique_ptr<QueryNode> makePhrase(std::string text);
        static std::unique_ptr<QueryNode> makeAnd(std::unique_ptr<QueryNode> l, std::unique_ptr<QueryNode> r);
        static std::unique_ptr<QueryNode> makeOr(std::unique_ptr<QueryNode> l, std::unique_ptr<QueryNode> r);
        static std::unique_ptr<QueryNode> makeNot(std::unique_ptr<QueryNode> op);
    };

    /**
     * @brief 词法分析 Token 类型
     */
    enum class TokenType {
        WORD,         ///< 普通单词
        PHRASE,       ///< 引号短语
        AND_KW,       ///< AND 关键字
        OR_KW,        ///< OR 关键字
        NOT_KW,       ///< NOT 关键字
        LPAREN,       ///< 左括号 (
        RPAREN,       ///< 右括号 )
        END_OF_INPUT, ///< 输入结束
    };

    struct Token {
        TokenType type;
        std::string value;
    };

    /**
     * @class QueryParser
     * @brief 将查询字符串解析为布尔表达式树
     *
     * 使用示例:
     * @code
     *   QueryParser parser;
     *   auto tree = parser.parse("(hello OR world) AND NOT spam");
     *   // tree 是 AND(OR(TERM("hello"), TERM("world")), NOT(TERM("spam")))
     * @endcode
     */
    class QueryParser {
    public:
        /**
         * @brief 解析查询字符串
         * @param query UTF-8 查询字符串
         * @return 表达式树根节点，解析失败返回 nullptr
         */
        std::unique_ptr<QueryNode> parse(std::string_view query);

        /**
         * @brief 判断查询是否包含布尔操作符
         *
         * 如果查询只是简单的词/短语（无 AND/OR/NOT/括号），
         * 则可走现有的快速路径而不必构建表达式树。
         */
        static bool isBooleanQuery(std::string_view query);

    private:
        std::vector<Token> tokens_;
        size_t pos_ = 0;

        std::vector<Token> tokenize(std::string_view input);
        const Token& peek() const;
        Token advance();
        bool match(TokenType type);

        std::unique_ptr<QueryNode> parseOrExpr();
        std::unique_ptr<QueryNode> parseAndExpr();
        std::unique_ptr<QueryNode> parseNotExpr();
        std::unique_ptr<QueryNode> parsePrimary();
    };

} // namespace wiser
