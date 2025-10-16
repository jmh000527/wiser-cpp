#include "wiser/wiser_environment.h"
#include "wiser/utils.h"

#include <stdexcept>
#include <iostream>

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
        , wiki_loader_(this) {}

    bool WiserEnvironment::initialize(const std::string& db_path) {
        db_path_ = db_path;

        // 初始化数据库
        if (!database_.initialize(db_path)) {
            Utils::printError("Failed to initialize database: {}\n", db_path);
            return false;
        }

        // 从数据库加载设置
        std::string token_len_str = database_.getSetting("token_len");
        if (!token_len_str.empty()) {
            token_len_ = static_cast<std::int32_t>(std::stoi(token_len_str));
        }

        std::string compress_str = database_.getSetting("compress_method");
        if (!compress_str.empty()) {
            compress_method_ = static_cast<CompressMethod>(std::stoi(compress_str));
        }

        std::string phrase_search_str = database_.getSetting("enable_phrase_search");
        if (!phrase_search_str.empty()) {
            enable_phrase_search_ = (phrase_search_str == "1");
        }

        std::string threshold_str = database_.getSetting("buffer_update_threshold");
        if (!threshold_str.empty()) {
            buffer_update_threshold_ = static_cast<std::int32_t>(std::stoi(threshold_str));
        }

        std::string indexed_count_str = database_.getSetting("indexed_count");
        if (!indexed_count_str.empty()) {
            indexed_count_ = static_cast<Count>(std::stoi(indexed_count_str));
        }

        Utils::printInfo("Wiser environment initialized successfully.\n");

        return true;
    }

    void WiserEnvironment::shutdown() {
        if (index_buffer_.size() > 0) {
            flushIndexBuffer();
        }

        // 保存设置到数据库
        database_.setSetting("token_len", std::to_string(token_len_));
        database_.setSetting("compress_method", std::to_string(static_cast<int>(compress_method_)));
        database_.setSetting("enable_phrase_search", enable_phrase_search_ ? "1" : "0");
        database_.setSetting("buffer_update_threshold", std::to_string(buffer_update_threshold_));
        database_.setSetting("indexed_count", std::to_string(indexed_count_));

        // 关闭数据库
        database_.close();

        Utils::printInfo("Wiser environment shut down successfully.\n");
    }

    void WiserEnvironment::addDocument(const std::string& title, const std::string& body) {
        // 空标题：视为“结束/分隔”信号，若缓冲中仍有未落盘数据则立即刷盘以确保数据持久化
        if (title.empty()) {
            if (index_buffer_.size() > 0)
                // flushIndexBuffer();
                return; // 不继续处理
        }

        // 已达到运行时设定的最大索引文档数（max_index_count_）则直接忽略后续文档
        if (hasReachedIndexLimit())
            return;

        // 正常文档但正文为空，给出错误日志并忽略（不影响缓冲状态）
        if (body.empty()) {
            Utils::printError("\nDocument body is empty for title: {}\n", title);
            return;
        }

        // 先写入/更新原始文档内容（正排 / 元数据），若失败则不进行倒排构建
        if (!database_.addDocument(title, body)) {
            Utils::printError("\nFailed to add document to database: {}\n", title);
            return;
        }

        // 通过标题获取文档 ID，若异常（<=0）说明写入或检索失败，终止本次处理
        DocId document_id = database_.getDocumentId(title);
        if (document_id <= 0) {
            Utils::printError("\nFailed to get document ID for: {}\n", title);
            return;
        }

        // 生成倒排索引增量：将正文分词并加入内存缓冲 index_buffer_（尚未落盘）
        tokenizer_.textToPostingsLists(document_id, body, index_buffer_);

        // 统计已索引文档数（用于 max_index_count_ 限制以及外部进度显示）
        ++indexed_count_;

        // 再次检查是否刚好达到文档上限；若达到则强制刷一次缓冲确保本批完整落盘
        if (hasReachedIndexLimit()) {
            if (index_buffer_.size() > 0)
                // flushIndexBuffer();
                return;
        }

        // 根据唯一 token 数判断是否触达刷盘阈值：
        //  1) buffer_update_threshold_ > 0 表示启用阈值机制
        //  2) 一旦 index_buffer_ 中的 token 数达到 / 超过阈值立即刷盘
        //     这样可以避免缓冲过大导致内存占用或事务过大
        if (buffer_update_threshold_ > 0 && index_buffer_.size() >= static_cast<size_t>(buffer_update_threshold_)) {
            // Utils::printInfo("\nFlush trigger: buffered tokens={} threshold={}\n", index_buffer_.size(),
            //                  buffer_update_threshold_);
            flushIndexBuffer();
        }

        // 未达到阈值：数据留在内存缓冲，等待后续文档继续累积或外部显式 flush
    }

    void WiserEnvironment::flushIndexBuffer() {
        if (index_buffer_.size() == 0)
            return;

        Utils::printInfo("\nFlushing index buffer with {} token(s).\n", index_buffer_.size());

        // 开始事务
        if (!database_.beginTransaction()) {
            Utils::printError("Failed to begin transaction\n");
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

            if (!database_.commitTransaction()) {
                throw std::runtime_error("Failed to commit transaction");
            }

            Utils::printInfo("Index buffer flushed successfully\n");
        } catch (const std::exception& e) {
            std::cout << std::endl;
            Utils::printError("Error flushing index buffer: {}\n", e.what());
            database_.rollbackTransaction();
        }

        index_buffer_.clear();
    }
} // namespace wiser
