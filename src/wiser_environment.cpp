#include "wiser/wiser_environment.h"
#include "wiser/utils.h"

#include <stdexcept>

namespace wiser {
    WiserEnvironment::WiserEnvironment()
        : token_len_(DEFAULT_TOKEN_LEN)
        , compress_method_(DEFAULT_COMPRESS_METHOD)
        , enable_phrase_search_(DEFAULT_ENABLE_PHRASE_SEARCH)
        , buffer_update_threshold_(DEFAULT_BUFFER_UPDATE_THRESHOLD)
        , indexed_count_(0)
        , max_index_count_(-1)
        , search_engine_(this)
        , tokenizer_(this)
        , wiki_loader_(this)
        , buffer_count_(0) {}

    bool WiserEnvironment::initialize(const std::string& db_path) {
        db_path_ = db_path;

        // 初始化数据库
        if (!database_.initialize(db_path)) {
            Utils::printError("Failed to initialize database: {}", db_path);
            return false;
        }

        // 从数据库加载设置
        std::string token_len_str = database_.getSetting("token_len");
        if (!token_len_str.empty()) { token_len_ = static_cast<std::int32_t>(std::stoi(token_len_str)); }

        std::string compress_str = database_.getSetting("compress_method");
        if (!compress_str.empty()) { compress_method_ = static_cast<CompressMethod>(std::stoi(compress_str)); }

        std::string phrase_search_str = database_.getSetting("enable_phrase_search");
        if (!phrase_search_str.empty()) { enable_phrase_search_ = (phrase_search_str == "1"); }

        std::string threshold_str = database_.getSetting("buffer_update_threshold");
        if (!threshold_str.empty()) { buffer_update_threshold_ = static_cast<std::int32_t>(std::stoi(threshold_str)); }

        std::string indexed_count_str = database_.getSetting("indexed_count");
        if (!indexed_count_str.empty()) { indexed_count_ = static_cast<Count>(std::stoi(indexed_count_str)); }

        Utils::printInfo("Wiser environment initialized successfully.");

        return true;
    }

    void WiserEnvironment::shutdown() {
        // 刷新缓冲区
        if (buffer_count_ > 0) { flushIndexBuffer(); }

        // 保存设置到数据库
        database_.setSetting("token_len", std::to_string(token_len_));
        database_.setSetting("compress_method", std::to_string(static_cast<int>(compress_method_)));
        database_.setSetting("enable_phrase_search", enable_phrase_search_ ? "1" : "0");
        database_.setSetting("buffer_update_threshold", std::to_string(buffer_update_threshold_));
        database_.setSetting("indexed_count", std::to_string(indexed_count_));

        // 关闭数据库
        database_.close();

        Utils::printInfo("Wiser environment shut down successfully.");
    }

    void WiserEnvironment::addDocument(const std::string& title, const std::string& body) {
        if (title.empty()) {
            if (buffer_count_ > 0) { flushIndexBuffer(); }
            return;
        }

        if (hasReachedIndexLimit()) { return; }
        if (body.empty()) {
            Utils::printError("Document body is empty for title: {}", title);
            return;
        }

        // 添加或更新文档
        if (!database_.addDocument(title, body)) {
            Utils::printError("Failed to add document to database: {}", title);
            return;
        }

        // 获取文档ID
        DocId document_id = database_.getDocumentId(title);
        if (document_id <= 0) {
            Utils::printError("Failed to get document ID for: {}", title);
            return;
        }

        // 创建倒排列表
        tokenizer_.textToPostingsLists(document_id, body, index_buffer_);

        ++buffer_count_;
        ++indexed_count_;

        // 达到上限时，立刻刷新缓冲区，方便稳定落库
        if (hasReachedIndexLimit()) {
            if (buffer_count_ > 0) { flushIndexBuffer(); }
            return;
        }

        // 检查是否需要刷新缓冲区
        if (buffer_count_ >= buffer_update_threshold_) { flushIndexBuffer(); }
    }

    void WiserEnvironment::flushIndexBuffer() {
        if (index_buffer_.size() == 0) { return; }

        Utils::printTimeDiff();
        Utils::printInfo("Flushing index buffer with {} tokens", index_buffer_.size());

        // 开始事务
        if (!database_.beginTransaction()) {
            Utils::printError("Failed to begin transaction");
            return;
        }

        try {
            for (auto& [token_id, postings_list]: index_buffer_) {
                auto rec = database_.getPostings(token_id);

                if (rec.has_value() && !rec->postings.empty()) {
                    PostingsList existing_list;
                    existing_list.deserialize(rec->postings);
                    existing_list.merge(std::move(*postings_list));

                    auto serialized = existing_list.serialize();
                    Count new_docs_count = existing_list.getDocumentsCount();

                    if (!database_.updatePostings(token_id, new_docs_count, serialized)) {
                        throw std::runtime_error("Failed to update postings for token " + std::to_string(token_id));
                    }
                } else {
                    auto serialized = postings_list->serialize();
                    Count docs_count = postings_list->getDocumentsCount();

                    if (!database_.updatePostings(token_id, docs_count, serialized)) {
                        throw std::runtime_error("Failed to insert postings for token " + std::to_string(token_id));
                    }
                }
            }

            if (!database_.commitTransaction()) { throw std::runtime_error("Failed to commit transaction"); }

            Utils::printInfo("Index buffer flushed successfully");
        } catch (const std::exception& e) {
            Utils::printError("Error flushing index buffer: {}", e.what());
            database_.rollbackTransaction();
        }

        index_buffer_.clear();
        buffer_count_ = 0;

        Utils::printTimeDiff();
    }
} // namespace wiser
