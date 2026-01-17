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
#include "config.h"
#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <optional>
#include <utility>
#include <mutex>

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
     * @brief 数据库类
     * 
     * 封装 SQLite 数据库操作，管理文档、词元和倒排索引的持久化存储。
     * 
     * @note 该类管理 sqlite3* 生命周期，提供一组预编译语句以减少开销。
     * @warning 非线程安全，跨线程使用时需外部序列化调用。
     * @note 事务：更新倒排时建议成对使用 beginTransaction/commitTransaction/rollbackTransaction。
     */
    class Database {
    public:
        /** 
         * @brief 构造函数
         */
        Database();

        /** 
         * @brief 析构函数
         */
        ~Database();

        // 不可复制，可移动
        Database(const Database&) = delete;
        Database& operator=(const Database&) = delete;
        Database(Database&&) noexcept;
        Database& operator=(Database&&) noexcept;

        /**
         * @brief 初始化数据库
         * @param db_path 数据库文件路径
         * @return 初始化成功返回 true，否则返回 false
         */
        [[nodiscard]] bool initialize(std::string_view db_path);

        /**
         * @brief 关闭数据库并释放所有预编译语句
         */
        void close();

        /**
         * @brief 根据标题获取文档 ID
         * @param title 文档标题
         * @return 文档 ID，若不存在则返回 0
         */
        [[nodiscard]] DocId getDocumentId(std::string_view title);

        /**
         * @brief 根据文档 ID 获取标题
         * @param document_id 文档 ID
         * @return 文档标题；若不存在则返回空字符串
         */
        std::string getDocumentTitle(DocId document_id);

        /**
         * @brief 根据文档 ID 获取内容
         * @param document_id 文档 ID
         * @return 文档内容；若不存在则返回空字符串
         */
        std::string getDocumentBody(DocId document_id);

        /**
         * @brief 新增或更新文档
         * @param title 文档标题
         * @param body 文档内容
         * @param token_count 文档包含的标记总数 (用于 ranking)
         * @return 操作成功返回 true，否则返回 false
         */
        [[nodiscard]] bool addDocument(std::string_view title, std::string_view body, int token_count);

        /**
         * @brief 获取文档总数
         * @return 文档数量
         */
        [[nodiscard]] Count getDocumentCount();

        /**
         * @brief 获取所有文档的 token 总数
         * @return token 总数
         */
        [[nodiscard]] long long getTotalTokenCount();

        /**
         * @brief 获取指定文档的 token 数量
         * @param doc_id 文档 ID
         * @return token 数量
         */
        [[nodiscard]] int getDocumentTokenCount(DocId doc_id);

        /**
         * @brief 更新文档的 token 数量
         * @param doc_id 文档 ID
         * @param token_count 新的 token 数量
         * @return 更新成功返回 true
         */
        bool updateDocumentTokenCount(DocId doc_id, int token_count);

        /**
         * @brief 查询词元信息
         * @param token 词元字符串
         * @param insert 若不存在是否插入
         * @return 词元信息（包含 ID 与文档数）；若不存在且 insert 为 false 则返回空
         */
        [[nodiscard]] std::optional<TokenInfo> getTokenInfo(std::string_view token, bool insert = false);

        /**
         * @brief 根据词元 ID 获取词元字符串
         * @param token_id 词元 ID
         * @return 词元字符串，不存在返回空字符串
         */
        std::string getToken(TokenId token_id);

        /**
         * @brief 获取词元的倒排记录
         * @param token_id 词元 ID
         * @return 包含文档数与序列化倒排列表的记录，若不存在返回空
         */
        [[nodiscard]] std::optional<PostingsRecord> getPostings(TokenId token_id);

        /**
         * @brief 更新词元的倒排记录
         * @param token_id 词元 ID
         * @param docs_count 文档数
         * @param postings 序列化后的倒排列表
         * @return 更新成功返回 true，否则返回 false
         */
        [[nodiscard]] bool updatePostings(TokenId token_id, Count docs_count, const std::vector<char>& postings);

        /**
         * @brief 获取当前数据库存储的配置
         * @return 配置对象
         */
        Config getConfig();

        /**
         * @brief 读取配置项
         * @param key 配置键
         * @return 配置值，若不存在返回空字符串
         */
        std::string getSetting(std::string_view key);

        /**
         * @brief 写入配置项
         * @param key 配置键
         * @param value 配置值
         * @return 写入成功返回 true，否则返回 false
         */
        bool setSetting(std::string_view key, std::string_view value);

        /** 
         * @brief 开始事务 
         * @return 成功返回 true
         */
        [[nodiscard]] bool beginTransaction();

        /** 
         * @brief 提交事务 
         * @return 成功返回 true
         */
        [[nodiscard]] bool commitTransaction();

        /** 
         * @brief 回滚事务 
         * @return 成功返回 true
         */
        bool rollbackTransaction();

        /**
         * @brief 获取所有文档的 (title, body)
         * @warning 大数据集下此调用会加载大量数据，请谨慎使用（建议仅用于调试/展示）。
         * @return 文档标题和内容的对组列表
         */
        [[nodiscard]] std::vector<std::pair<std::string, std::string>> getAllDocuments();

        /**
         * @brief LIKE 子串检索
         * 
         * 当查询长度小于 N-gram 最小长度时作为兜底方案。
         * 在 documents.title 或 documents.body 中搜索包含给定子串的文档（大小写敏感性由底层 SQLite 决定）。
         * @param needle 原始查询串
         * @return 命中的文档 ID 列表（按 ID 升序）。
         */
        [[nodiscard]] std::vector<DocId> searchDocumentsLike(std::string_view needle);

        /**
         * @brief 获取所有文档的 ID 和 token 数量
         * @return 映射: DocId -> token_count
         */
        [[nodiscard]] std::vector<std::pair<DocId, int>> getAllDocumentTokenCounts();

    private:
        mutable std::recursive_mutex stmt_mutex_; // Statement protection
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
        sqlite3_stmt* get_total_token_count_stmt_;
        sqlite3_stmt* get_doc_token_count_stmt_; // add this
        sqlite3_stmt* update_doc_token_count_stmt_; // add this
        sqlite3_stmt* get_all_token_counts_stmt_; // add this
        sqlite3_stmt* list_documents_stmt_;
        sqlite3_stmt* like_search_stmt_; // SELECT id FROM documents WHERE title LIKE ? ESCAPE '\\' OR body LIKE ? ESCAPE '\\'
        sqlite3_stmt* begin_stmt_;
        sqlite3_stmt* commit_stmt_;
        sqlite3_stmt* rollback_stmt_;

        // 辅助函数
        bool createTables();
        bool prepareStatements();
        void finalizeStatements();

        // 禁用复制的辅助函数
        void moveFrom(Database&& other) noexcept;
    };
} // namespace wiser
