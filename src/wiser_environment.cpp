/**
 * @file wiser_environment.cpp
 * @brief Wiser搜索引擎环境核心实现文件
 * 
 * 此文件实现了Wiser搜索引擎的核心环境类，负责：
 * - 搜索引擎的初始化和配置管理
 * - 数据库连接和文档数据管理
 * - 搜索组件和分词器的协调
 * - 统计信息和缓存管理
 */

#include "wiser/wiser_environment.h"
#include "wiser/utils.h"

#include <sqlite3.h>
#include <stdexcept>
#include <iostream>

namespace wiser {
    /**
     * @brief WiserEnvironment 构造函数
     * 
     * 初始化搜索引擎环境的核心组件：
     * - indexed_count_: 已索引文档计数器，初始化为0
     * - search_engine_: 搜索引擎核心组件，传入当前环境指针
     * - tokenizer_: 分词器组件，传入当前环境指针
     * - wiki_loader_: Wikipedia文档加载器，传入当前环境指针
     * 
     * 注意：配置默认值已在 config.h 的结构体定义中设置，此处无需重复设置
     */
    WiserEnvironment::WiserEnvironment()
        : indexed_count_(0)           // 已索引文档数量，初始为0
        , search_engine_(this)        // 初始化搜索引擎组件，传入当前环境指针
        , tokenizer_(this)            // 初始化分词器组件，传入当前环境指针
        , wiki_loader_(this) {        // 初始化Wikipedia加载器，传入当前环境指针
            // Config defaults are already set in struct definition in config.h
            // No need to reset them here unless we use constants from WiserEnvironment which are now redundant
        }

    /**
     * @brief 初始化搜索引擎环境
     * 
     * 此方法执行搜索引擎环境的完整初始化流程：
     * 1. 设置数据库路径配置
     * 2. 初始化数据库连接
     * 3. 加载文档长度缓存数据
     * 4. 从数据库加载配置设置
     * 
     * @param db_path 数据库文件路径
     * @return bool 初始化成功返回true，失败返回false
     */
    bool WiserEnvironment::initialize(const std::string& db_path) {
        // 设置数据库路径到配置对象中
        config_.db_path = db_path;

        // 初始化数据库连接
        // 调用 Database 类的 initialize 方法建立数据库连接
        if (!database_.initialize(db_path)) {
            // 数据库初始化失败，记录错误日志并返回失败
            spdlog::error("Failed to initialize database: {}", db_path);
            return false;
        }

        // 加载文档长度缓存
        // 使用代码块限制 counts 变量的作用域
        {
            // 从数据库获取所有文档的token数量统计
            auto counts = database_.getAllDocumentTokenCounts();
            
            // 清空现有的文档长度缓存
            doc_lengths_cache_.clear();
            
            // 预分配缓存空间，提高性能
            doc_lengths_cache_.reserve(counts.size());
            
            // 重置总token计数器
            total_tokens_ = 0;
            
            // 遍历所有文档的token统计结果
            for (const auto& p : counts) {
                // 将文档ID和token数量存入缓存
                doc_lengths_cache_[p.first] = p.second;
                
                // 累加总token数量
                total_tokens_ += p.second;
            }
            
            // 标记文档长度缓存已加载
            doc_lengths_loaded_ = true;
            
            // 记录信息日志，显示加载的文档数量和总token数
            spdlog::info("Loaded {} document lengths into cache. Total tokens: {}", counts.size(), total_tokens_);
        }

        // 从数据库加载配置
        // 从数据库获取存储的配置信息
        auto db_config = database_.getConfig();
        
        // 如果数据库中的token长度配置有效（大于0），则更新当前配置
        if (db_config.token_len > 0) {
            config_.token_len = db_config.token_len;
        }
        
        // 如果数据库中的缓冲区更新阈值配置有效（大于0），则更新当前配置
        if (db_config.buffer_update_threshold > 0) {
            config_.buffer_update_threshold = db_config.buffer_update_threshold;
        }
        
        // 如果数据库中的短语搜索配置已设置，则更新当前配置
        if (db_config.enable_phrase_search) {
            config_.enable_phrase_search = db_config.enable_phrase_search;
        }

        // 标记已初始化，使 set* 立刻持久化
        initialized_ = true;

        // // 若库中没有对应设置项（即新库），将当前内存默认值写入，确保下一次能加载
        // database_.setSetting("token_len", std::to_string(token_len_));
        // database_.setSetting("compress_method", std::to_string(static_cast<int>(compress_method_)));
        // database_.setSetting("enable_phrase_search", enable_phrase_search_ ? "1" : "0");
        // database_.setSetting("indexed_count", std::to_string(indexed_count_));

        // 记录初始化成功日志
        spdlog::info("Wiser environment initialized successfully.");

        // 初始化成功，返回true
        return true;
    }

    /**
     * @brief 关闭搜索引擎环境
     * 
     * 执行优雅的关闭流程：
     * 1. 检查并刷新内存中的索引缓冲区
     * 2. 保存当前配置到数据库
     * 3. 关闭数据库连接
     * 4. 记录关闭日志
     */
    void WiserEnvironment::shutdown() {
        // 刷新剩余缓冲数据（内部会获取 buffer_mutex_）
        flushIndexBuffer();

        // 保存配置到数据库
        database_.setSetting("token_len", std::to_string(config_.token_len));
        database_.setSetting("compress_method", std::to_string(static_cast<int>(config_.compress_method)));
        database_.setSetting("indexed_count", std::to_string(indexed_count_.load()));
        database_.setSetting("scoring_method", std::to_string(static_cast<int>(config_.scoring_method)));

        database_.close();
        spdlog::info("Wiser environment shut down successfully.");
    }

    /**
     * @brief 获取指定文档的token数量
     * 
     * 从文档长度缓存中查询指定文档ID对应的token数量
     * 
     * @param doc_id 文档ID
     * @return int 文档的token数量，如果文档不存在或缓存未加载返回0
     */
    int WiserEnvironment::getDocumentTokenCount(DocId doc_id) const {
        // 检查文档长度缓存是否已加载
        if (!doc_lengths_loaded_) {
            return 0;  // 缓存未加载，返回0
        }
        
        // 获取共享读锁，允许多线程并发读取
        std::shared_lock<std::shared_mutex> lock(cache_mutex_);
        
        // 在缓存中查找指定文档ID
        auto it = doc_lengths_cache_.find(doc_id);
        
        // 如果找到文档，返回其token数量
        if (it != doc_lengths_cache_.end()) {
            return it->second;
        }
        
        // 文档不存在于缓存中，返回0
        return 0;
    }

    /**
     * @brief 添加文档到搜索引擎
     * 
     * 处理文档添加的完整流程：
     * 1. 验证文档有效性
     * 2. 写入文档元数据到数据库
     * 3. 分词并构建倒排索引
     * 4. 更新缓存和统计信息
     * 5. 根据阈值决定是否刷新缓冲区
     * 
     * @param title 文档标题
     * @param body 文档正文内容
     */
    void WiserEnvironment::addDocument(const std::string& title, const std::string& body, const std::string& author) {
        if (title.empty()) return;
        if (hasReachedIndexLimit()) return;

        if (body.empty()) {
            spdlog::error("Document body is empty for title: {}", title);
            return;
        }

        // 写入文档元数据（Database 内部有 stmt_mutex_ 保护）
        if (!database_.addDocument(title, body, 0, author)) {
            spdlog::error("Failed to add document to database: {}", title);
            return;
        }

        DocId document_id = database_.getDocumentId(title);
        if (document_id <= 0) {
            spdlog::error("Failed to get document ID for: {}", title);
            return;
        }

        // 锁定缓冲区：分词写入 + 阈值检查 + 可能的刷盘
        int term_count;
        {
            std::lock_guard<std::mutex> buf_lock(buffer_mutex_);

            int title_term_count = tokenizer_.textToPostingsLists(document_id, title, index_buffer_);
            int body_term_count = tokenizer_.textToPostingsLists(document_id, body, index_buffer_);
            term_count = title_term_count + body_term_count;

            // 阈值刷盘（在同一把锁内，避免 TOCTOU 竞态）
            if (config_.buffer_update_threshold > 0 &&
                index_buffer_.size() >= static_cast<size_t>(config_.buffer_update_threshold)) {
                flushBufferImpl();
            }
        }

        // 更新文档 token 总数
        database_.updateDocumentTokenCount(document_id, term_count);

        // 更新文档长度缓存（cache_mutex_ 保护）
        {
            std::unique_lock<std::shared_mutex> lock(cache_mutex_);
            if (doc_lengths_cache_.find(document_id) == doc_lengths_cache_.end()) {
                total_tokens_ += term_count;
            } else {
                total_tokens_ += (term_count - doc_lengths_cache_[document_id]);
            }
            doc_lengths_cache_[document_id] = term_count;
        }

        // 原子自增已索引文档数
        ++indexed_count_;
    }

    int WiserEnvironment::rebuildIndex() {
        spdlog::info("rebuild_index | start");

        // 1. 取出所有文档
        auto docs = database_.getAllDocuments(); // vector<pair<title, body>>
        if (docs.empty()) {
            spdlog::info("rebuild_index | no documents to reindex");
            return 0;
        }

        // 2. 清空 tokens 表
        char* errmsg = nullptr;
        int rc = sqlite3_exec(database_.getHandle(), "DELETE FROM tokens", nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            spdlog::error("rebuild_index | failed to clear tokens: {}", errmsg ? errmsg : "unknown");
            sqlite3_free(errmsg);
            return -1;
        }

        // 3. 清空内存状态
        {
            std::lock_guard<std::mutex> buf_lock(buffer_mutex_);
            index_buffer_.clear();
        }
        {
            std::unique_lock<std::shared_mutex> lock(cache_mutex_);
            doc_lengths_cache_.clear();
            total_tokens_ = 0;
        }
        indexed_count_.store(0, std::memory_order_relaxed);

        // 4. 逐文档重新分词、建索引
        int count = 0;
        for (auto& [title, body] : docs) {
            if (title.empty() || body.empty()) continue;

            DocId document_id = database_.getDocumentId(title);
            if (document_id <= 0) continue;

            int term_count;
            {
                std::lock_guard<std::mutex> buf_lock(buffer_mutex_);

                int title_terms = tokenizer_.textToPostingsLists(document_id, title, index_buffer_);
                int body_terms  = tokenizer_.textToPostingsLists(document_id, body,  index_buffer_);
                term_count = title_terms + body_terms;

                if (config_.buffer_update_threshold > 0 &&
                    index_buffer_.size() >= static_cast<size_t>(config_.buffer_update_threshold)) {
                    flushBufferImpl();
                }
            }

            database_.updateDocumentTokenCount(document_id, term_count);

            {
                std::unique_lock<std::shared_mutex> lock(cache_mutex_);
                doc_lengths_cache_[document_id] = term_count;
                total_tokens_ += term_count;
            }

            ++indexed_count_;
            ++count;
        }

        // 5. 最终刷盘
        flushIndexBuffer();
        search_engine_.invalidateCache();

        spdlog::info("rebuild_index | done | documents={}", count);
        return count;
    }

    /**
     * @brief 刷新索引缓冲区到数据库
     * 
     * 将内存中的倒排索引缓冲区内容写入数据库：
     * 1. 检查缓冲区是否为空
     * 2. 开始数据库事务
     * 3. 合并或插入倒排索引数据
     * 4. 提交事务或回滚错误
     * 5. 清空缓冲区
     */
    void WiserEnvironment::flushIndexBuffer() {
        std::lock_guard<std::mutex> buf_lock(buffer_mutex_);
        flushBufferImpl();
    }

    void WiserEnvironment::flushBufferImpl() {
        // 调用方须已持有 buffer_mutex_
        if (index_buffer_.size() == 0)
            return;

        spdlog::debug("Flushing index buffer with {} token(s).", index_buffer_.size());

        if (!database_.beginTransaction()) {
            spdlog::error("Failed to begin transaction");
            return;
        }

        try {
            // 遍历缓冲区中的所有token和对应的倒排列表
            for (auto& [token_id, postings_list]: index_buffer_) {
                // 从数据库获取该token现有的倒排列表
                auto rec = database_.getPostings(token_id);

                if (rec.has_value() && !rec->postings.empty()) {
                    // 情况1：数据库中已存在该token的倒排列表
                    PostingsList existing_list;
                    // 反序列化时使用当前配置的压缩方法
                    existing_list.deserialize(rec->postings, config_.compress_method);
                    existing_list.merge(std::move(*postings_list));  // 合并内存中的新数据

                    // 重新序列化合并后的列表
                    auto serialized = existing_list.serialize(config_.compress_method);
                    Count new_docs_count = existing_list.getDocumentsCount();

                    // 更新数据库中的倒排列表
                    if (!database_.updatePostings(token_id, new_docs_count, serialized)) {
                        throw std::runtime_error("Failed to update postings for token " + std::to_string(token_id));
                    }
                } else {
                    // 情况2：数据库中不存在该token的倒排列表，直接插入
                    auto serialized = postings_list->serialize(config_.compress_method);
                    Count docs_count = postings_list->getDocumentsCount();

                    // 插入新的倒排列表到数据库
                    if (!database_.updatePostings(token_id, docs_count, serialized)) {
                        throw std::runtime_error("Failed to insert postings for token " + std::to_string(token_id));
                    }
                }
            }

            // 提交事务，确保所有更改持久化
            if (!database_.commitTransaction()) {
                throw std::runtime_error("Failed to commit transaction");
            }

            // 记录成功刷新的调试信息
            spdlog::debug("Index buffer flushed successfully");
        } catch (const std::exception& e) {
            // 处理异常：记录错误日志并回滚事务
            spdlog::error("Error flushing index buffer: {}", e.what());
            database_.rollbackTransaction();
        }

        // 清空内存缓冲区，准备接收新的索引数据
        index_buffer_.clear();

        // 索引变更后使查询缓存失效
        search_engine_.invalidateCache();
    }
} // namespace wiser
