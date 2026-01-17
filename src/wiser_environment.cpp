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
        // 检查内存索引缓冲区是否还有未写入的数据
        if (index_buffer_.size() > 0) {
            // 如果有未写入的数据，先刷新到数据库
            flushIndexBuffer();
        }

        // 保存当前配置设置到数据库
        // 保存分词器token长度配置
        database_.setSetting("token_len", std::to_string(config_.token_len));
        // 保存压缩方法配置
        database_.setSetting("compress_method", std::to_string(static_cast<int>(config_.compress_method)));
        // 保存已索引文档数量
        database_.setSetting("indexed_count", std::to_string(indexed_count_));
        // 保存评分方法配置
        database_.setSetting("scoring_method", std::to_string(static_cast<int>(config_.scoring_method)));

        // 关闭数据库连接，释放资源
        database_.close();

        // 记录关闭成功日志
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
            spdlog::error("Document body is empty for title: {}", title);
            return;
        }

        // 先写入/更新原始文档内容（正排 / 元数据），若失败则不进行倒排构建
        // 初始 token_count 设为 0，后续 tokenizer 处理完后再更新
        if (!database_.addDocument(title, body, 0)) {
            spdlog::error("Failed to add document to database: {}", title);
            return;
        }

        // 通过标题获取文档 ID，若异常（<=0）说明写入或检索失败，终止本次处理
        DocId document_id = database_.getDocumentId(title);
        if (document_id <= 0) {
            spdlog::error("Failed to get document ID for: {}", title);
            return;
        }

        // 生成倒排索引增量：将正文分词并加入内存缓冲 index_buffer_（尚落盘）
        int term_count = tokenizer_.textToPostingsLists(document_id, body, index_buffer_);

        // 更新文档的 token 总数
        database_.updateDocumentTokenCount(document_id, term_count);
        // 更新缓存
        {
            std::unique_lock<std::shared_mutex> lock(cache_mutex_);
            if (doc_lengths_cache_.find(document_id) == doc_lengths_cache_.end()) {
                 // 新文档
                 total_tokens_ += term_count;
            } else {
                 // 更新文档，diff (通常 addDocument 在 wiser 中是新增，但逻辑上支持更新)
                 total_tokens_ += (term_count - doc_lengths_cache_[document_id]);
            }
            doc_lengths_cache_[document_id] = term_count;
        }

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
        if (config_.buffer_update_threshold > 0 && index_buffer_.size() >= static_cast<size_t>(config_.buffer_update_threshold)) {
            flushIndexBuffer();
        }

        // 未达到阈值：数据留在内存缓冲，等待后续文档继续累积或外部显式 flush
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
        // 检查缓冲区是否为空，避免不必要的数据库操作
        if (index_buffer_.size() == 0)
            return;

        // 记录调试信息，显示要刷新的token数量
        spdlog::debug("Flushing index buffer with {} token(s).", index_buffer_.size());

        // 开始数据库事务，确保数据一致性
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
    }
} // namespace wiser
