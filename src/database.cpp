#include "wiser/database.h"
#include "wiser/utils.h"
#include <sqlite3.h>
#include <stdexcept>
#include <cstring>
#include <optional>
#include <string>

namespace wiser {
    Database::Database()
        : db_(nullptr)
        , get_document_id_stmt_(nullptr)
        , get_document_title_stmt_(nullptr)
        , get_document_body_stmt_(nullptr)
        , insert_document_stmt_(nullptr)
        , update_document_stmt_(nullptr)
        , get_token_id_stmt_(nullptr)
        , get_token_stmt_(nullptr)
        , store_token_stmt_(nullptr)
        , get_postings_stmt_(nullptr)
        , update_postings_stmt_(nullptr)
        , get_settings_stmt_(nullptr)
        , replace_settings_stmt_(nullptr)
        , get_document_count_stmt_(nullptr)
        , begin_stmt_(nullptr)
        , commit_stmt_(nullptr)
        , rollback_stmt_(nullptr)
        , list_documents_stmt_(nullptr)
        , like_search_stmt_(nullptr) {}

    Database::~Database() {
        close();
    }

    Database::Database(Database&& other) noexcept {
        moveFrom(std::move(other));
    }

    Database& Database::operator=(Database&& other) noexcept {
        if (this != &other) {
            close();
            moveFrom(std::move(other));
        }
        return *this;
    }

    bool Database::initialize(std::string_view db_path) {
        if (db_) {
            close();
        }
        std::string path(db_path);
        int rc = sqlite3_open(path.c_str(), &db_);
        if (rc != SQLITE_OK) {
            spdlog::error("Cannot open database: {}", sqlite3_errmsg(db_));
            return false;
        }
        if (!createTables()) {
            close();
            return false;
        }
        if (!prepareStatements()) {
            close();
            return false;
        }
        return true;
    }

    void Database::close() {
        finalizeStatements();
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    DocId Database::getDocumentId(std::string_view title) {
        if (!get_document_id_stmt_)
            return 0;
        sqlite3_reset(get_document_id_stmt_);
        sqlite3_bind_text(get_document_id_stmt_, 1, title.data(), static_cast<int>(title.size()), SQLITE_STATIC);
        int rc = sqlite3_step(get_document_id_stmt_);
        if (rc == SQLITE_ROW) {
            return static_cast<DocId>(sqlite3_column_int(get_document_id_stmt_, 0));
        }
        return 0;
    }

    std::string Database::getDocumentTitle(DocId document_id) {
        if (!get_document_title_stmt_)
            return "";
        sqlite3_reset(get_document_title_stmt_);
        sqlite3_bind_int(get_document_title_stmt_, 1, static_cast<int>(document_id));
        int rc = sqlite3_step(get_document_title_stmt_);
        if (rc == SQLITE_ROW) {
            const char* title = reinterpret_cast<const char*>(sqlite3_column_text(get_document_title_stmt_, 0));
            return title ? std::string(title) : std::string();
        }
        return "";
    }

    std::string Database::getDocumentBody(DocId document_id) {
        if (!get_document_body_stmt_)
            return "";
        sqlite3_reset(get_document_body_stmt_);
        sqlite3_bind_int(get_document_body_stmt_, 1, static_cast<int>(document_id));
        int rc = sqlite3_step(get_document_body_stmt_);
        if (rc == SQLITE_ROW) {
            const char* body = reinterpret_cast<const char*>(sqlite3_column_text(get_document_body_stmt_, 0));
            return body ? std::string(body) : std::string();
        }
        return "";
    }

    bool Database::addDocument(std::string_view title, std::string_view body) {
        DocId document_id = getDocumentId(title);
        sqlite3_stmt* stmt;
        if (document_id > 0) {
            // 更新现有文档
            stmt = update_document_stmt_;
            sqlite3_reset(stmt);
            sqlite3_bind_text(stmt, 1, body.data(), static_cast<int>(body.size()), SQLITE_STATIC);
            sqlite3_bind_int(stmt, 2, static_cast<int>(document_id));
        } else {
            // 插入新文档
            stmt = insert_document_stmt_;
            sqlite3_reset(stmt);
            sqlite3_bind_text(stmt, 1, title.data(), static_cast<int>(title.size()), SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, body.data(), static_cast<int>(body.size()), SQLITE_STATIC);
        }
        int rc = sqlite3_step(stmt);
        return rc == SQLITE_DONE;
    }

    Count Database::getDocumentCount() {
        if (!get_document_count_stmt_)
            return 0;
        sqlite3_reset(get_document_count_stmt_);
        int rc = sqlite3_step(get_document_count_stmt_);
        if (rc == SQLITE_ROW) {
            return static_cast<Count>(sqlite3_column_int(get_document_count_stmt_, 0));
        }
        return 0;
    }

    std::optional<TokenInfo> Database::getTokenInfo(std::string_view token, bool insert) {
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

    std::string Database::getSetting(std::string_view key) {
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
        if (!replace_settings_stmt_)
            return false;
        sqlite3_reset(replace_settings_stmt_);
        sqlite3_bind_text(replace_settings_stmt_, 1, key.data(), static_cast<int>(key.size()), SQLITE_STATIC);
        sqlite3_bind_text(replace_settings_stmt_, 2, value.data(), static_cast<int>(value.size()), SQLITE_STATIC);
        int rc = sqlite3_step(replace_settings_stmt_);
        return rc == SQLITE_DONE;
    }

    bool Database::beginTransaction() {
        if (!begin_stmt_)
            return false;
        sqlite3_reset(begin_stmt_);
        return sqlite3_step(begin_stmt_) == SQLITE_DONE;
    }

    bool Database::commitTransaction() {
        if (!commit_stmt_)
            return false;
        sqlite3_reset(commit_stmt_);
        return sqlite3_step(commit_stmt_) == SQLITE_DONE;
    }

    bool Database::rollbackTransaction() {
        if (!rollback_stmt_)
            return false;
        sqlite3_reset(rollback_stmt_);
        return sqlite3_step(rollback_stmt_) == SQLITE_DONE;
    }

    bool Database::createTables() {
        const char* sql_statements[] = {
                    "CREATE TABLE IF NOT EXISTS settings ("
                    "  key   TEXT PRIMARY KEY,"
                    "  value TEXT"
                    ");",

                    "CREATE TABLE IF NOT EXISTS documents ("
                    "  id      INTEGER PRIMARY KEY,"
                    "  title   TEXT NOT NULL,"
                    "  body    TEXT NOT NULL"
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
        const struct {
            const char* sql;
            sqlite3_stmt** stmt;
        } statements[] = {
                            { "SELECT id FROM documents WHERE title = ?;", &get_document_id_stmt_ },
                            { "SELECT title FROM documents WHERE id = ?;", &get_document_title_stmt_ },
                            { "SELECT body FROM documents WHERE id = ?;", &get_document_body_stmt_ },
                            { "INSERT INTO documents (title, body) VALUES (?, ?);", &insert_document_stmt_ },
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
                            { "SELECT title, body FROM documents ORDER BY id;", &list_documents_stmt_ },
                            { "SELECT id FROM documents WHERE instr(title, ?) > 0 OR instr(body, ?) > 0 ORDER BY id;", &like_search_stmt_ },
                            { "BEGIN;", &begin_stmt_ },
                            { "COMMIT;", &commit_stmt_ },
                            { "ROLLBACK;", &rollback_stmt_ }
                        };

        for (const auto& stmt_info: statements) {
            int rc = sqlite3_prepare_v2(db_, stmt_info.sql, -1, stmt_info.stmt, nullptr);
            if (rc != SQLITE_OK) {
                spdlog::error("Failed to prepare statement: {}", stmt_info.sql);
                return false;
            }
        }
        return true;
    }

    void Database::finalizeStatements() {
        sqlite3_stmt* statements[] = {
                    get_document_id_stmt_, get_document_title_stmt_, get_document_body_stmt_, insert_document_stmt_,
                    update_document_stmt_, get_token_id_stmt_, get_token_stmt_,
                    store_token_stmt_, get_postings_stmt_, update_postings_stmt_,
                    get_settings_stmt_, replace_settings_stmt_, get_document_count_stmt_,
                    list_documents_stmt_, like_search_stmt_,
                    begin_stmt_, commit_stmt_, rollback_stmt_
                };

        for (auto stmt: statements) {
            if (stmt) {
                sqlite3_finalize(stmt);
            }
        }

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
        list_documents_stmt_ = nullptr;
        like_search_stmt_ = nullptr;
        begin_stmt_ = nullptr;
        commit_stmt_ = nullptr;
        rollback_stmt_ = nullptr;
    }

    void Database::moveFrom(Database&& other) noexcept {
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
        begin_stmt_ = other.begin_stmt_;
        commit_stmt_ = other.commit_stmt_;
        rollback_stmt_ = other.rollback_stmt_;
        list_documents_stmt_ = other.list_documents_stmt_;
        like_search_stmt_ = other.like_search_stmt_;

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
        other.begin_stmt_ = nullptr;
        other.commit_stmt_ = nullptr;
        other.rollback_stmt_ = nullptr;
        other.list_documents_stmt_ = nullptr;
        other.like_search_stmt_ = nullptr;
    }

    std::vector<std::pair<std::string, std::string>> Database::getAllDocuments() {
        std::vector<std::pair<std::string, std::string>> docs;
        if (!list_documents_stmt_)
            return docs;
        sqlite3_reset(list_documents_stmt_);
        int rc;
        while ((rc = sqlite3_step(list_documents_stmt_)) == SQLITE_ROW) {
            const char* title = reinterpret_cast<const char*>(sqlite3_column_text(list_documents_stmt_, 0));
            const char* body = reinterpret_cast<const char*>(sqlite3_column_text(list_documents_stmt_, 1));
            docs.emplace_back(title ? title : "", body ? body : "");
        }
        return docs;
    }

    std::vector<DocId> Database::searchDocumentsLike(std::string_view needle) {
        std::vector<DocId> ids;
        if (!like_search_stmt_) return ids;

        sqlite3_reset(like_search_stmt_);
        sqlite3_bind_text(like_search_stmt_, 1, needle.data(), static_cast<int>(needle.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(like_search_stmt_, 2, needle.data(), static_cast<int>(needle.size()), SQLITE_TRANSIENT);
        int rc;
        while ((rc = sqlite3_step(like_search_stmt_)) == SQLITE_ROW) {
            ids.push_back(static_cast<DocId>(sqlite3_column_int(like_search_stmt_, 0)));
        }
        return ids;
    }
} // namespace wiser
