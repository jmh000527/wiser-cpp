#pragma once

/**
 * @file database.h
 * @brief 基于 SQLite 的正排/倒排与配置存储封装。
 *
 * 注意：
 *  - 该类管理 sqlite3* 生命周期，提供一组预编译语句以减少开销；
 *  - 非线程安全，跨线程使用时需外部序列化调用；
 *  - 事务：更新倒排时建议成对使用 begin/commit/rollback。
 */

#include "types.h"
#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <optional>
#include <utility>

struct sqlite3;
struct sqlite3_stmt;

namespace wiser {
    struct TokenInfo {
        TokenId id;
        Count docs_count;
    };

    struct PostingsRecord {
        Count docs_count;
        std::vector<char> postings;
    };

    /**
     * 数据库类 - 封装SQLite数据库操作
     */
    class Database {
    public:
        /** 构造与析构 */
        Database();
        ~Database();

        // 不可复制，可移动
        Database(const Database&) = delete;
        Database& operator=(const Database&) = delete;
        Database(Database&&) noexcept;
        Database& operator=(Database&&) noexcept;

        /**
         * 初始化数据库
         * @param db_path 数据库文件路径
         * @return true 成功，false 失败
         */
        [[nodiscard]] bool initialize(std::string_view db_path);

        /** 关闭数据库 */
        /**
         * @brief 关闭数据库并释放所有预编译语句。
         */
        void close();

        /**
         * 根据标题获取文档ID
         * @param title 文档标题
         * @return 文档ID，0 表示不存在
         */
        [[nodiscard]] DocId getDocumentId(std::string_view title);

        /**
         * 根据文档ID获取标题
         * @param document_id 文档ID
         * @return 文档标题；不存在时返回空字符串
         */
        std::string getDocumentTitle(DocId document_id);

        /**
         * 根据文档ID获取内容
         * @param document_id 文档ID
         * @return 文档内容；不存在时返回空字符串
         */
        std::string getDocumentBody(DocId document_id);

        /**
         * 新增或更新文档
         * @param title 文档标题
         * @param body 文档内容
         * @return true 成功，false 失败
         */
        [[nodiscard]] bool addDocument(std::string_view title, std::string_view body);

        /**
         * 获取文档总数
         * @return 文档数量
         */
        [[nodiscard]] Count getDocumentCount();

        /**
         * 查询词元信息
         * @param token 词元字符串
         * @param insert 若不存在是否插入
         * @return 词元信息（包含id与文档数）；无则为空
         */
        [[nodiscard]] std::optional<TokenInfo> getTokenInfo(std::string_view token, bool insert = false);

        /**
         * 根据词元ID获取词元字符串
         * @param token_id 词元ID
         * @return 词元字符串，不存在返回空
         */
        std::string getToken(TokenId token_id);

        /**
         * 获取词元的倒排记录
         * @param token_id 词元ID
         * @return 包含文档数与序列化倒排列表的记录
         */
        [[nodiscard]] std::optional<PostingsRecord> getPostings(TokenId token_id);

        /**
         * 更新词元的倒排记录
         * @param token_id 词元ID
         * @param docs_count 文档数
         * @param postings 序列化后的倒排列表
         * @return true 成功，false 失败
         */
        [[nodiscard]] bool updatePostings(TokenId token_id, Count docs_count, const std::vector<char>& postings);

        /**
         * 读取设置项
         * @param key 键
         * @return 值，若不存在返回空字符串
         */
        std::string getSetting(std::string_view key);

        /**
         * 写入设置项
         * @param key 键
         * @param value 值
         * @return true 成功，false 失败
         */
        bool setSetting(std::string_view key, std::string_view value);

        /** 开始事务 */
        [[nodiscard]] bool beginTransaction();
        /** 提交事务 */
        [[nodiscard]] bool commitTransaction();
        /** 回滚事务 */
        bool rollbackTransaction();

        /**
         * @brief 获取所有文档的 (title, body)。
         * @warning 大数据集下此调用会加载大量数据，请谨慎使用（仅调试/展示）。
         */
        [[nodiscard]] std::vector<std::pair<std::string, std::string>> getAllDocuments();

    private:
        sqlite3* db_;

        // 预编译语句
        sqlite3_stmt* get_document_id_stmt_;
        sqlite3_stmt* get_document_title_stmt_;
        sqlite3_stmt* get_document_body_stmt_;
        sqlite3_stmt* insert_document_stmt_;
        sqlite3_stmt* update_document_stmt_;
        sqlite3_stmt* get_token_id_stmt_;
        sqlite3_stmt* get_token_stmt_;
        sqlite3_stmt* store_token_stmt_;
        sqlite3_stmt* get_postings_stmt_;
        sqlite3_stmt* update_postings_stmt_;
        sqlite3_stmt* get_settings_stmt_;
        sqlite3_stmt* replace_settings_stmt_;
        sqlite3_stmt* get_document_count_stmt_;
        sqlite3_stmt* begin_stmt_;
        sqlite3_stmt* commit_stmt_;
        sqlite3_stmt* rollback_stmt_;
        sqlite3_stmt* list_documents_stmt_;

        // 辅助函数
        bool createTables();
        bool prepareStatements();
        void finalizeStatements();

        // 禁用复制的辅助函数
        void moveFrom(Database&& other) noexcept;
    };
} // namespace wiser
