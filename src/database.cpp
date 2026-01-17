/**
 * @file database.cpp
 * @brief SQLite 数据库封装实现
 *
 * 本文件实现 wiser::Database，对 SQLite 进行薄封装，提供：
 * - 文档表、词元表、设置表的创建与访问
 * - 预编译语句的准备/释放，减少重复解析 SQL 的开销
 * - 倒排列表（BLOB）读写接口
 * - 简单的事务控制接口
 *
 * 线程安全策略：
 * - 所有对 sqlite3_stmt* 的操作都通过 stmt_mutex_ 序列化，避免并发复用同一语句对象导致未定义行为
 */

#include "wiser/database.h"
#include "wiser/utils.h"
#include <sqlite3.h>
#include <stdexcept>
#include <cstring>
#include <optional>
#include <string>

namespace wiser {
    /**
     * @brief Database 类构造函数
     * 
     * 初始化所有 SQLite 预处理语句指针为 nullptr。
     * 这些指针将在 initialize() 方法中通过 sqlite3_prepare_v2() 分配。
     * 包括：
     * - 文档相关查询语句（ID、标题、正文获取，文档插入更新）
     * - 词项相关查询语句（ID获取、词项获取、词项存储）
     * - 倒排列表相关查询语句（获取、更新）
     * - 设置相关查询语句（获取、替换）
     * - 统计相关查询语句（文档计数、总词项计数、文档词项计数）
     * - 事务控制语句（开始、提交、回滚）
     * - 搜索相关语句（模糊搜索、文档列表）
     */
    Database::Database()
        : db_(nullptr), get_document_id_stmt_(nullptr), get_document_title_stmt_(nullptr),
          get_document_body_stmt_(nullptr), insert_document_stmt_(nullptr), update_document_stmt_(nullptr),
          get_token_id_stmt_(nullptr), get_token_stmt_(nullptr), store_token_stmt_(nullptr),
          get_postings_stmt_(nullptr), update_postings_stmt_(nullptr), get_settings_stmt_(nullptr),
          replace_settings_stmt_(nullptr), get_document_count_stmt_(nullptr), get_total_token_count_stmt_(nullptr),
          get_doc_token_count_stmt_(nullptr), update_doc_token_count_stmt_(nullptr), get_all_token_counts_stmt_(nullptr), list_documents_stmt_(nullptr), like_search_stmt_(nullptr),
          begin_stmt_(nullptr), commit_stmt_(nullptr), rollback_stmt_(nullptr) {}

    /**
     * @brief Database 类析构函数
     * 
     * 确保数据库连接和所有预处理语句被正确关闭和释放。
     * 调用 close() 方法执行清理工作，防止资源泄漏。
     */
    Database::~Database() {
        close();
    }

    /**
     * @brief Database 类移动构造函数
     * 
     * 使用移动语义从另一个 Database 对象转移资源所有权。
     * 通过 moveFrom() 方法转移数据库连接和预处理语句指针。
     * 
     * @param other 要移动的源 Database 对象（右值引用）
     */
    Database::Database(Database&& other) noexcept {
        moveFrom(std::move(other));
    }

    /**
     * @brief Database 类移动赋值运算符
     * 
     * 使用移动语义从另一个 Database 对象转移资源所有权。
     * 首先检查自赋值，然后关闭当前资源，最后转移新资源。
     * 
     * @param other 要移动的源 Database 对象（右值引用）
     * @return Database& 返回当前对象的引用，支持链式赋值
     */
    Database& Database::operator=(Database&& other) noexcept {
        // 防止自赋值：如果当前对象和源对象是同一个，直接返回
        if (this != &other) {
            // 关闭当前对象持有的所有数据库资源
            close();
            // 从源对象转移资源所有权到当前对象
            moveFrom(std::move(other));
        }
        return *this;
    }

    /**
     * @brief 初始化数据库连接并准备相关资源
     * 
     * 打开指定的 SQLite 数据库文件，创建必要的表结构，
     * 并预处理所有 SQL 查询语句以供后续使用。
     * 
     * @param db_path 数据库文件路径（字符串视图）
     * @return bool 初始化成功返回 true，失败返回 false
     */
    bool Database::initialize(std::string_view db_path) {
        // 如果已经存在数据库连接，先关闭现有连接
        if (db_) {
            close();
        }
        
        // 将字符串视图转换为 std::string（SQLite API 需要 C 风格字符串）
        std::string path(db_path);
        
        // 打开 SQLite 数据库文件
        int rc = sqlite3_open(path.c_str(), &db_);
        
        // 检查数据库打开是否成功
        if (rc != SQLITE_OK) {
            // 记录错误日志，包含 SQLite 返回的具体错误信息
            spdlog::error("Cannot open database: {}", sqlite3_errmsg(db_));
            return false;
        }
        
        // 创建数据库表结构（如果表不存在）
        if (!createTables()) {
            // 表创建失败，关闭数据库连接并返回失败
            close();
            return false;
        }
        
        // 预处理所有 SQL 查询语句
        if (!prepareStatements()) {
            // 语句预处理失败，关闭数据库连接并返回失败
            close();
            return false;
        }
        
        // 所有初始化步骤成功完成
        return true;
    }

    /**
     * @brief 关闭数据库连接并释放所有资源
     * 
     * 此方法执行以下清理操作：
     * 1. 获取语句互斥锁，确保线程安全
     * 2. 最终化所有预处理语句，释放 SQLite 资源
     * 3. 关闭数据库连接，释放文件句柄
     * 4. 将数据库指针重置为 nullptr，防止野指针
     */
    void Database::close() {
        // 获取递归互斥锁，确保多线程环境下的线程安全
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        
        // 最终化所有预处理语句，释放 SQLite 资源
        finalizeStatements();
        
        // 检查数据库连接是否有效，然后关闭连接
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr; // 重置指针，防止野指针
        }
    }

    /**
     * @brief 根据文档标题获取文档ID
     * 
     * 此方法执行以下操作：
     * 1. 获取线程锁确保线程安全
     * 2. 检查预处理语句是否已初始化
     * 3. 重置语句状态并绑定标题参数
     * 4. 执行SQL查询获取结果
     * 5. 返回文档ID或0（未找到）
     * 
     * @param title 文档标题（字符串视图）
     * @return DocId 找到的文档ID，未找到返回0
     */
    DocId Database::getDocumentId(std::string_view title) {
        // 获取递归互斥锁，确保多线程环境下的线程安全
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        
        // 检查获取文档ID的预处理语句是否已初始化
        if (!get_document_id_stmt_)
            return 0; // 语句未初始化，返回0表示未找到
        
        // 重置预处理语句状态，清除之前的绑定参数
        sqlite3_reset(get_document_id_stmt_);
        
        // 绑定标题参数到预处理语句的第一个参数位置
        // SQLITE_STATIC 表示字符串内容在语句执行期间保持不变
        sqlite3_bind_text(get_document_id_stmt_, 1, title.data(), static_cast<int>(title.size()), SQLITE_STATIC);
        
        // 执行SQL查询并获取返回码
        int rc = sqlite3_step(get_document_id_stmt_);
        
        // 检查是否查询到结果行
        if (rc == SQLITE_ROW) {
            // 从结果的第一列（索引0）获取整数类型的文档ID
            return static_cast<DocId>(sqlite3_column_int(get_document_id_stmt_, 0));
        }
        
        // 未找到匹配的文档，返回0
        return 0;
    }

    /**
     * @brief 根据文档ID获取文档标题
     * 
     * 此方法执行以下操作：
     * 1. 获取线程锁确保线程安全
     * 2. 检查预处理语句是否已初始化
     * 3. 重置语句状态并绑定文档ID参数
     * 4. 执行SQL查询获取结果
     * 5. 返回文档标题或空字符串（未找到）
     * 
     * @param document_id 文档ID
     * @return std::string 找到的文档标题，未找到返回空字符串
     */
    std::string Database::getDocumentTitle(DocId document_id) {
        // 获取递归互斥锁，确保多线程环境下的线程安全
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        
        // 检查获取文档标题的预处理语句是否已初始化
        if (!get_document_title_stmt_)
            return ""; // 语句未初始化，返回空字符串
        
        // 重置预处理语句状态，清除之前的绑定参数
        sqlite3_reset(get_document_title_stmt_);
        
        // 绑定文档ID参数到预处理语句的第一个参数位置
        sqlite3_bind_int(get_document_title_stmt_, 1, static_cast<int>(document_id));
        
        // 执行SQL查询并获取返回码
        int rc = sqlite3_step(get_document_title_stmt_);
        
        // 检查是否查询到结果行
        if (rc == SQLITE_ROW) {
            // 从结果的第一列（索引0）获取文本类型的文档标题
            // 使用 reinterpret_cast 将 SQLite 返回的 const unsigned char* 转换为 const char*
            const char* title = reinterpret_cast<const char*>(sqlite3_column_text(get_document_title_stmt_, 0));
            
            // 检查获取的标题指针是否有效，有效则转换为 std::string，否则返回空字符串
            return title ? std::string(title) : std::string();
        }
        
        // 未找到匹配的文档，返回空字符串
        return "";
    }

    /**
     * @brief 根据文档ID获取文档正文内容
     * 
     * 此方法执行以下操作：
     * 1. 获取线程锁确保线程安全
     * 2. 检查预处理语句是否已初始化
     * 3. 重置语句状态并绑定文档ID参数
     * 4. 执行SQL查询获取结果
     * 5. 返回文档正文内容或空字符串（未找到）
     * 
     * @param document_id 文档ID
     * @return std::string 找到的文档正文内容，未找到返回空字符串
     */
    std::string Database::getDocumentBody(DocId document_id) {
        // 获取递归互斥锁，确保多线程环境下的线程安全
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        
        // 检查获取文档正文的预处理语句是否已初始化
        if (!get_document_body_stmt_)
            return ""; // 语句未初始化，返回空字符串
        
        // 重置预处理语句状态，清除之前的绑定参数
        sqlite3_reset(get_document_body_stmt_);
        
        // 绑定文档ID参数到预处理语句的第一个参数位置
        // 使用 sqlite3_bind_int64 绑定64位整数类型的文档ID
        sqlite3_bind_int64(get_document_body_stmt_, 1, document_id);
        // 执行 SQL 查询语句，获取查询结果
        if (sqlite3_step(get_document_body_stmt_) == SQLITE_ROW) {
            // 从查询结果的第一列（索引0）获取文档正文文本
            // 使用 reinterpret_cast 将 SQLite 返回的 const unsigned char* 转换为 const char*
            const char* body_text = reinterpret_cast<const char*>(sqlite3_column_text(get_document_body_stmt_, 0));
            
            // 检查获取的文本指针是否有效（非空指针）
            if (body_text) {
                // 将 C 风格字符串转换为 std::string 并返回给调用者
                return std::string(body_text);
            }
        }
        return "";
    }

    /**
     * @brief 插入文档（如标题已存在则尝试更新正文）
     *
     * 该方法优先执行 INSERT；若失败（通常是 title 的 UNIQUE 约束冲突），则尝试：
     * - 通过 title 查到已有文档 id
     * - 使用 UPDATE 仅更新 body 字段
     *
     * @note 当前数据库的 UPDATE 语句仅更新 body；token_count 的更新需要单独调用 updateDocumentTokenCount()
     *
     * @param title 文档标题（唯一键）
     * @param body 文档正文
     * @param token_count 文档分词后 token 数量
     * @return bool 成功返回 true，失败返回 false
     */
    bool Database::addDocument(std::string_view title, std::string_view body, int token_count) {
        // 获取递归互斥锁，确保预编译语句在多线程下串行使用
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        // 检查插入语句是否已准备完成
        if (!insert_document_stmt_)
            return false;

        // 重置语句状态并绑定参数
        sqlite3_reset(insert_document_stmt_);
        sqlite3_bind_text(insert_document_stmt_, 1, title.data(), static_cast<int>(title.size()), SQLITE_STATIC);
        sqlite3_bind_text(insert_document_stmt_, 2, body.data(), static_cast<int>(body.size()), SQLITE_STATIC);
        sqlite3_bind_int(insert_document_stmt_, 3, token_count);

        // 执行插入
        if (sqlite3_step(insert_document_stmt_) != SQLITE_DONE) {
            // 插入失败时，常见原因是 title 的 UNIQUE 约束冲突：此时尝试改为更新正文
            DocId existing_id = getDocumentId(title);
            if (existing_id > 0) {
                // 仅更新 body：复用 update_document_stmt_（SQL: UPDATE documents SET body = ? WHERE id = ?）
                sqlite3_reset(update_document_stmt_);
                sqlite3_bind_text(update_document_stmt_, 1, body.data(), static_cast<int>(body.size()), SQLITE_STATIC);
                sqlite3_bind_int64(update_document_stmt_, 2, existing_id);
                return sqlite3_step(update_document_stmt_) == SQLITE_DONE;
            }
            return false;
        }

        return true;
    }

    /**
     * @brief 获取文档总数
     * @return Count 文档数量，失败返回 0
     */
    Count Database::getDocumentCount() {
        // 串行化语句复用，避免多线程同时 step/reset 同一个 sqlite3_stmt*
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!get_document_count_stmt_)
            return 0;
        // 重置并执行 COUNT(*) 查询
        sqlite3_reset(get_document_count_stmt_);
        int rc = sqlite3_step(get_document_count_stmt_);
        if (rc == SQLITE_ROW) {
            // COUNT(*) 结果位于第一列
            return static_cast<Count>(sqlite3_column_int(get_document_count_stmt_, 0));
        }
        return 0;
    }

    /**
     * @brief 获取全库 token 总数（SUM(token_count)）
     * @return long long 总 token 数，失败返回 0
     */
    long long Database::getTotalTokenCount() {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!get_total_token_count_stmt_)
            return 0;
        sqlite3_reset(get_total_token_count_stmt_);
        if (sqlite3_step(get_total_token_count_stmt_) == SQLITE_ROW) {
            return sqlite3_column_int64(get_total_token_count_stmt_, 0);
        }
        return 0;
    }

    /**
     * @brief 获取指定文档的 token 数量
     * @param doc_id 文档 ID
     * @return int token 数量，失败返回 0
     */
    int Database::getDocumentTokenCount(DocId doc_id) {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!get_doc_token_count_stmt_)
            return 0;
        sqlite3_reset(get_doc_token_count_stmt_);
        sqlite3_bind_int64(get_doc_token_count_stmt_, 1, doc_id);
        if (sqlite3_step(get_doc_token_count_stmt_) == SQLITE_ROW) {
            return sqlite3_column_int(get_doc_token_count_stmt_, 0);
        }
        return 0;
    }

    /**
     * @brief 更新指定文档的 token 数量
     * @param doc_id 文档 ID
     * @param token_count token 数量
     * @return bool 成功返回 true
     */
    bool Database::updateDocumentTokenCount(DocId doc_id, int token_count) {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!update_doc_token_count_stmt_)
            return false;
        sqlite3_reset(update_doc_token_count_stmt_);
        sqlite3_bind_int(update_doc_token_count_stmt_, 1, token_count);
        sqlite3_bind_int64(update_doc_token_count_stmt_, 2, doc_id);
        return sqlite3_step(update_doc_token_count_stmt_) == SQLITE_DONE;
    }

    std::optional<TokenInfo> Database::getTokenInfo(std::string_view token, bool insert) {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!get_token_id_stmt_)
            return std::nullopt;
        sqlite3_reset(get_token_id_stmt_);
        sqlite3_bind_text(get_token_id_stmt_, 1, token.data(), static_cast<int>(token.size()), SQLITE_STATIC);
        int rc = sqlite3_step(get_token_id_stmt_);
        if (rc == SQLITE_ROW) {
            TokenInfo info{};
            info.id = static_cast<TokenId>(sqlite3_column_int(get_token_id_stmt_, 0));
            info.docs_count = static_cast<Count>(sqlite3_column_int(get_token_id_stmt_, 1));
            return info;
        }
        if (insert && store_token_stmt_) {
            static const unsigned char empty_blob_marker[] = ""; // 非空指针，长度0
            sqlite3_reset(store_token_stmt_);
            sqlite3_bind_text(store_token_stmt_, 1, token.data(), static_cast<int>(token.size()), SQLITE_STATIC);
            sqlite3_bind_blob(store_token_stmt_, 2, empty_blob_marker, 0, SQLITE_STATIC);
            rc = sqlite3_step(store_token_stmt_);
            if (rc == SQLITE_DONE) {
                sqlite3_reset(get_token_id_stmt_);
                sqlite3_bind_text(get_token_id_stmt_, 1, token.data(), static_cast<int>(token.size()), SQLITE_STATIC);
                rc = sqlite3_step(get_token_id_stmt_);
                if (rc == SQLITE_ROW) {
                    TokenInfo info{};
                    info.id = static_cast<TokenId>(sqlite3_column_int(get_token_id_stmt_, 0));
                    info.docs_count = static_cast<Count>(sqlite3_column_int(get_token_id_stmt_, 1));
                    return info;
                }
            }
        }
        return std::nullopt;
    }

    std::string Database::getToken(TokenId token_id) {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!get_token_stmt_)
            return "";
        sqlite3_reset(get_token_stmt_);
        sqlite3_bind_int(get_token_stmt_, 1, static_cast<int>(token_id));
        int rc = sqlite3_step(get_token_stmt_);
        if (rc == SQLITE_ROW) {
            const char* token = reinterpret_cast<const char*>(sqlite3_column_text(get_token_stmt_, 0));
            return token ? std::string(token) : std::string();
        }
        return "";
    }

    std::optional<PostingsRecord> Database::getPostings(TokenId token_id) {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!get_postings_stmt_)
            return std::nullopt;
        sqlite3_reset(get_postings_stmt_);
        sqlite3_bind_int(get_postings_stmt_, 1, static_cast<int>(token_id));
        int rc = sqlite3_step(get_postings_stmt_);
        if (rc == SQLITE_ROW) {
            PostingsRecord rec{};
            rec.docs_count = static_cast<Count>(sqlite3_column_int(get_postings_stmt_, 0));
            const void* blob = sqlite3_column_blob(get_postings_stmt_, 1);
            int blob_size = sqlite3_column_bytes(get_postings_stmt_, 1);
            if (blob && blob_size > 0) {
                const char* data = static_cast<const char*>(blob);
                rec.postings.assign(data, data + blob_size);
            }
            return rec;
        }
        return std::nullopt;
    }

    bool Database::updatePostings(TokenId token_id, Count docs_count, const std::vector<char>& postings) {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!update_postings_stmt_)
            return false;
        sqlite3_reset(update_postings_stmt_);
        sqlite3_bind_int(update_postings_stmt_, 1, static_cast<int>(docs_count));
        if (postings.empty()) {
            static const unsigned char empty_blob_marker[] = "";
            sqlite3_bind_blob(update_postings_stmt_, 2, empty_blob_marker, 0, SQLITE_STATIC);
        } else {
            sqlite3_bind_blob(update_postings_stmt_, 2, postings.data(), static_cast<int>(postings.size()),
                              SQLITE_STATIC);
        }
        sqlite3_bind_int(update_postings_stmt_, 3, static_cast<int>(token_id));
        int rc = sqlite3_step(update_postings_stmt_);
        return rc == SQLITE_DONE;
    }

    Config Database::getConfig() {
        Config config;

        auto val = getSetting("token_len");
        if (!val.empty()) {
            try { config.token_len = std::stoi(val); } catch (...) {}
        }

        val = getSetting("buffer_update_threshold");
        if (!val.empty()) {
            try { config.buffer_update_threshold = std::stoi(val); } catch (...) {}
        }

        val = getSetting("max_index_count");
        if (!val.empty()) {
            try { config.max_index_count = std::stoi(val); } catch (...) {}
        }

        val = getSetting("enable_phrase_search");
        if (!val.empty()) {
            try { config.enable_phrase_search = (std::stoi(val) != 0); } catch (...) {}
        }

        val = getSetting("compress_method");
        if (!val.empty()) {
            try { config.compress_method = static_cast<CompressMethod>(std::stoi(val)); } catch (...) {}
        }

        val = getSetting("scoring_method");
        if (!val.empty()) {
            try { config.scoring_method = static_cast<ScoringMethod>(std::stoi(val)); } catch (...) {}
        }

        val = getSetting("bm25_k1");
        if (!val.empty()) {
            try { config.bm25_k1 = std::stod(val); } catch (...) {}
        }

        val = getSetting("bm25_b");
        if (!val.empty()) {
            try { config.bm25_b = std::stod(val); } catch (...) {}
        }

        return config;
    }

    std::string Database::getSetting(std::string_view key) {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!get_settings_stmt_)
            return "";
        sqlite3_reset(get_settings_stmt_);
        sqlite3_bind_text(get_settings_stmt_, 1, key.data(), static_cast<int>(key.size()), SQLITE_STATIC);
        int rc = sqlite3_step(get_settings_stmt_);
        if (rc == SQLITE_ROW) {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(get_settings_stmt_, 0));
            return value ? std::string(value) : std::string();
        }
        return "";
    }

    bool Database::setSetting(std::string_view key, std::string_view value) {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!replace_settings_stmt_)
            return false;
        sqlite3_reset(replace_settings_stmt_);
        sqlite3_bind_text(replace_settings_stmt_, 1, key.data(), static_cast<int>(key.size()), SQLITE_STATIC);
        sqlite3_bind_text(replace_settings_stmt_, 2, value.data(), static_cast<int>(value.size()), SQLITE_STATIC);
        int rc = sqlite3_step(replace_settings_stmt_);
        return rc == SQLITE_DONE;
    }

    bool Database::beginTransaction() {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!begin_stmt_)
            return false;
        sqlite3_reset(begin_stmt_);
        return sqlite3_step(begin_stmt_) == SQLITE_DONE;
    }

    bool Database::commitTransaction() {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!commit_stmt_)
            return false;
        sqlite3_reset(commit_stmt_);
        return sqlite3_step(commit_stmt_) == SQLITE_DONE;
    }

    bool Database::rollbackTransaction() {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        if (!rollback_stmt_)
            return false;
        sqlite3_reset(rollback_stmt_);
        return sqlite3_step(rollback_stmt_) == SQLITE_DONE;
    }

    bool Database::createTables() {
        // 通过 sqlite3_exec 执行一组 DDL，保证基础表结构存在
        const char* sql_statements[] = {
                    "CREATE TABLE IF NOT EXISTS settings ("
                    "  key   TEXT PRIMARY KEY,"
                    "  value TEXT"
                    ");",

                    "CREATE TABLE IF NOT EXISTS documents ("
                    "  id      INTEGER PRIMARY KEY,"
                    "  title   TEXT NOT NULL,"
                    "  body    TEXT NOT NULL,"
                    "  token_count INTEGER NOT NULL DEFAULT 0"
                    ");",

                    "CREATE TABLE IF NOT EXISTS tokens ("
                    "  id         INTEGER PRIMARY KEY,"
                    "  token      TEXT NOT NULL,"
                    "  docs_count INT NOT NULL,"
                    "  postings   BLOB NOT NULL"
                    ");",

                    "CREATE UNIQUE INDEX IF NOT EXISTS token_index ON tokens(token);",
                    "CREATE UNIQUE INDEX IF NOT EXISTS title_index ON documents(title);"
                };

        for (const char* sql: sql_statements) {
            // sqlite3_exec 在失败时会分配 error_msg，需要 sqlite3_free 释放
            char* error_msg = nullptr;
            int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &error_msg);
            if (rc != SQLITE_OK) {
                spdlog::error("SQL error: {}", error_msg);
                sqlite3_free(error_msg);
                return false;
            }
        }
        return true;
    }

    bool Database::prepareStatements() {
        // 将 SQL 文本与对应的 sqlite3_stmt* 指针成对管理，统一 prepare
        struct StmtDef {
            const char* sql;
            sqlite3_stmt** stmt;
        };
        const StmtDef statements[] = {
                            { "SELECT id FROM documents WHERE title = ?;", &get_document_id_stmt_ },
                            { "SELECT title FROM documents WHERE id = ?;", &get_document_title_stmt_ },
                            { "SELECT body FROM documents WHERE id = ?;", &get_document_body_stmt_ },
                            { "INSERT INTO documents (title, body, token_count) VALUES (?, ?, ?);", &insert_document_stmt_ },
                            { "UPDATE documents SET body = ? WHERE id = ?;", &update_document_stmt_ },
                            { "SELECT id, docs_count FROM tokens WHERE token = ?;", &get_token_id_stmt_ },
                            { "SELECT token FROM tokens WHERE id = ?;", &get_token_stmt_ },
                            { "INSERT OR IGNORE INTO tokens (token, docs_count, postings) VALUES (?, 0, ?);",
                              &store_token_stmt_ },
                            { "SELECT docs_count, postings FROM tokens WHERE id = ?;", &get_postings_stmt_ },
                            { "UPDATE tokens SET docs_count = ?, postings = ? WHERE id = ?;", &update_postings_stmt_ },
                            { "SELECT value FROM settings WHERE key = ?;", &get_settings_stmt_ },
                            { "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?);", &replace_settings_stmt_ },
                            { "SELECT COUNT(*) FROM documents;", &get_document_count_stmt_ },
                            { "SELECT SUM(token_count) FROM documents;", &get_total_token_count_stmt_ },
                            { "SELECT token_count FROM documents WHERE id = ?;", &get_doc_token_count_stmt_ },
                            { "UPDATE documents SET token_count = ? WHERE id = ?;", &update_doc_token_count_stmt_ },
                            { "SELECT id, token_count FROM documents;", &get_all_token_counts_stmt_ },
                            { "SELECT title, body FROM documents ORDER BY id;", &list_documents_stmt_ },
                            { "SELECT id FROM documents WHERE instr(title, ?) > 0 OR instr(body, ?) > 0 ORDER BY id;",
                              &like_search_stmt_ },
                            { "BEGIN;", &begin_stmt_ },
                            { "COMMIT;", &commit_stmt_ },
                            { "ROLLBACK;", &rollback_stmt_ }
                        };

        for (const auto& stmt_info: statements) {
            // -1 表示 SQL 以 '\0' 结尾；sqlite3_prepare_v2 会生成可复用的预编译语句对象
            int rc = sqlite3_prepare_v2(db_, stmt_info.sql, -1, stmt_info.stmt, nullptr);
            if (rc != SQLITE_OK) {
                spdlog::error("Failed to prepare statement: {}", stmt_info.sql);
                return false;
            }
        }
        return true;
    }

    void Database::finalizeStatements() {
        // 统一 finalize 所有已准备的语句；SQLite 要求每个 stmt* 仅 finalize 一次
        sqlite3_stmt* statements[] = {
                    get_document_id_stmt_, get_document_title_stmt_, get_document_body_stmt_, insert_document_stmt_,
                    update_document_stmt_, get_token_id_stmt_, get_token_stmt_,
                    store_token_stmt_, get_postings_stmt_, update_postings_stmt_,
                    get_settings_stmt_, replace_settings_stmt_, get_document_count_stmt_,
                    get_total_token_count_stmt_, get_doc_token_count_stmt_, update_doc_token_count_stmt_,
                    get_all_token_counts_stmt_,
                    list_documents_stmt_, like_search_stmt_,
                    begin_stmt_, commit_stmt_, rollback_stmt_
                };

        for (auto stmt: statements) {
            if (stmt) {
                sqlite3_finalize(stmt);
            }
        }

        // 将指针置空，避免误用已 finalize 的语句
        get_document_id_stmt_ = nullptr;
        get_document_title_stmt_ = nullptr;
        get_document_body_stmt_ = nullptr;
        insert_document_stmt_ = nullptr;
        update_document_stmt_ = nullptr;
        get_token_id_stmt_ = nullptr;
        get_token_stmt_ = nullptr;
        store_token_stmt_ = nullptr;
        get_postings_stmt_ = nullptr;
        update_postings_stmt_ = nullptr;
        get_settings_stmt_ = nullptr;
        replace_settings_stmt_ = nullptr;
        get_document_count_stmt_ = nullptr;
        get_total_token_count_stmt_ = nullptr;
        get_doc_token_count_stmt_ = nullptr;
        update_doc_token_count_stmt_ = nullptr;
        get_all_token_counts_stmt_ = nullptr;
        list_documents_stmt_ = nullptr;
        like_search_stmt_ = nullptr;
        begin_stmt_ = nullptr;
        commit_stmt_ = nullptr;
        rollback_stmt_ = nullptr;
    }

    void Database::moveFrom(Database&& other) noexcept {
        // 逐个转移资源所有权：sqlite3* 连接与所有预编译语句指针
        db_ = other.db_;
        get_document_id_stmt_ = other.get_document_id_stmt_;
        get_document_title_stmt_ = other.get_document_title_stmt_;
        get_document_body_stmt_ = other.get_document_body_stmt_;
        insert_document_stmt_ = other.insert_document_stmt_;
        update_document_stmt_ = other.update_document_stmt_;
        get_token_id_stmt_ = other.get_token_id_stmt_;
        get_token_stmt_ = other.get_token_stmt_;
        store_token_stmt_ = other.store_token_stmt_;
        get_postings_stmt_ = other.get_postings_stmt_;
        update_postings_stmt_ = other.update_postings_stmt_;
        get_settings_stmt_ = other.get_settings_stmt_;
        replace_settings_stmt_ = other.replace_settings_stmt_;
        get_document_count_stmt_ = other.get_document_count_stmt_;
        get_total_token_count_stmt_ = other.get_total_token_count_stmt_;
        get_doc_token_count_stmt_ = other.get_doc_token_count_stmt_;
        update_doc_token_count_stmt_ = other.update_doc_token_count_stmt_;
        get_all_token_counts_stmt_ = other.get_all_token_counts_stmt_;
        list_documents_stmt_ = other.list_documents_stmt_;
        like_search_stmt_ = other.like_search_stmt_;
        begin_stmt_ = other.begin_stmt_;
        commit_stmt_ = other.commit_stmt_;
        rollback_stmt_ = other.rollback_stmt_;

        // 将源对象置为“空”，确保析构时不会重复释放资源
        other.db_ = nullptr;
        other.get_document_id_stmt_ = nullptr;
        other.get_document_title_stmt_ = nullptr;
        other.get_document_body_stmt_ = nullptr;
        other.insert_document_stmt_ = nullptr;
        other.update_document_stmt_ = nullptr;
        other.get_token_id_stmt_ = nullptr;
        other.get_token_stmt_ = nullptr;
        other.store_token_stmt_ = nullptr;
        other.get_postings_stmt_ = nullptr;
        other.update_postings_stmt_ = nullptr;
        other.get_settings_stmt_ = nullptr;
        other.replace_settings_stmt_ = nullptr;
        other.get_document_count_stmt_ = nullptr;
        other.get_total_token_count_stmt_ = nullptr;
        other.get_doc_token_count_stmt_ = nullptr;
        other.update_doc_token_count_stmt_ = nullptr;
        other.get_all_token_counts_stmt_ = nullptr;
        other.list_documents_stmt_ = nullptr;
        other.like_search_stmt_ = nullptr;
        other.begin_stmt_ = nullptr;
        other.commit_stmt_ = nullptr;
        other.rollback_stmt_ = nullptr;
    }

    /**
     * @brief 读取所有文档（按 id 升序）
     *
     * 返回值为 (title, body) 列表，用于简单展示/调试或小数据集场景。
     *
     * @return std::vector<std::pair<std::string, std::string>> 文档标题与正文列表
     */
    std::vector<std::pair<std::string, std::string>> Database::getAllDocuments() {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        std::vector<std::pair<std::string, std::string>> docs;
        if (!list_documents_stmt_)
            return docs;
        // list_documents_stmt_：SELECT title, body FROM documents ORDER BY id;
        sqlite3_reset(list_documents_stmt_);
        int rc;
        while ((rc = sqlite3_step(list_documents_stmt_)) == SQLITE_ROW) {
            // 取出每行的 title/body 列文本
            const char* title = reinterpret_cast<const char*>(sqlite3_column_text(list_documents_stmt_, 0));
            const char* body = reinterpret_cast<const char*>(sqlite3_column_text(list_documents_stmt_, 1));
            docs.emplace_back(title ? title : "", body ? body : "");
        }
        return docs;
    }

    /**
     * @brief 简单模糊检索：在 title/body 中查找子串
     *
     * 底层使用 SQLite 的 instr(title, ?) / instr(body, ?) 判断子串出现位置，
     * 返回命中的文档 id 列表（按 id 升序）。
     *
     * @param needle 需要查找的子串
     * @return std::vector<DocId> 命中文档 id 列表
     */
    std::vector<DocId> Database::searchDocumentsLike(std::string_view needle) {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        std::vector<DocId> ids;
        if (!like_search_stmt_)
            return ids;

        // like_search_stmt_：
        // SELECT id FROM documents WHERE instr(title, ?) > 0 OR instr(body, ?) > 0 ORDER BY id;
        sqlite3_reset(like_search_stmt_);
        // SQLITE_TRANSIENT：SQLite 会复制一份参数内容，适合临时字符串视图
        sqlite3_bind_text(like_search_stmt_, 1, needle.data(), static_cast<int>(needle.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(like_search_stmt_, 2, needle.data(), static_cast<int>(needle.size()), SQLITE_TRANSIENT);
        int rc;
        while ((rc = sqlite3_step(like_search_stmt_)) == SQLITE_ROW) {
            // 每行只返回一个 id
            ids.push_back(static_cast<DocId>(sqlite3_column_int(like_search_stmt_, 0)));
        }
        return ids;
    }

    /**
     * @brief 读取所有文档的 token_count（按 id）
     * @return std::vector<std::pair<DocId, int>> (doc_id, token_count) 列表
     */
    std::vector<std::pair<DocId, int>> Database::getAllDocumentTokenCounts() {
        std::lock_guard<std::recursive_mutex> lock(stmt_mutex_);
        std::vector<std::pair<DocId, int>> results;
        if (!get_all_token_counts_stmt_)
            return results;
        // get_all_token_counts_stmt_：SELECT id, token_count FROM documents;
        sqlite3_reset(get_all_token_counts_stmt_);
        int rc;
        while ((rc = sqlite3_step(get_all_token_counts_stmt_)) == SQLITE_ROW) {
            DocId id = static_cast<DocId>(sqlite3_column_int(get_all_token_counts_stmt_, 0));
            int count = sqlite3_column_int(get_all_token_counts_stmt_, 1);
            results.emplace_back(id, count);
        }
        return results;
    }
} // namespace wiser
