#pragma once

/**
 * @file wiser_environment.h
 * @brief Wiser 搜索引擎的环境与配置入口，聚合数据库、分词器、搜索引擎等核心组件。
 *
 * 职责：
 *  - 管理运行时配置（N-gram、压缩方式、短语搜索开关、缓冲阈值等）
 *  - 提供统一的组件访问（Database/Tokenizer/SearchEngine/Loaders）
 *  - 管理内存中的倒排缓冲区，并在阈值或显式调用时刷盘合并
 *
 * 线程模型：
 *  - 本类不内置锁，若在多线程环境下并发写入/查询，需要在更高层进行串行化或加锁保护。
 */

#include "types.h"
#include "database.h"
#include "postings.h"
#include "search_engine.h"
#include "tokenizer.h"
#include "wiki_loader.h"
#include "utils.h"
#include "config.h" // Include Config
#include <string>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <shared_mutex>

namespace wiser {
    /**
     * @brief Wiser 搜索引擎环境类
     * 
     * 管理整个搜索引擎的配置和组件。
     */
    class WiserEnvironment {
    public:
        /** 
         * @brief 构造环境 
         */
        WiserEnvironment();
        ~WiserEnvironment() = default;

        // 不可复制，可移动
        WiserEnvironment(const WiserEnvironment&) = delete;
        WiserEnvironment& operator=(const WiserEnvironment&) = delete;
        WiserEnvironment(WiserEnvironment&&) = default;
        WiserEnvironment& operator=(WiserEnvironment&&) = default;

        /**
         * @brief 初始化环境
         * @param db_path 数据库路径
         * @return 初始化成功返回 true，否则返回 false
         */
        bool initialize(const std::string& db_path);

        /**
         * @brief 关闭环境
         * 
         * 保存设置并关闭数据库。
         */
        void shutdown();

        // 配置访问器
        /**
         * @brief 获取当前配置（只读）
         * @return 配置对象的常量引用
         */
        const Config& getConfig() const {
            return config_;
        }

        /**
         * @brief 获取当前配置（可修改）
         * @return 配置对象的引用
         */
        Config& getConfig() {
            return config_;
        }

        /** 
         * @brief 获取数据库路径 
         * @return 数据库路径字符串
         */
        const std::string& getDatabasePath() const {
            return config_.db_path;
        }

        /** 
         * @brief 获取分词 N 值 
         * @return N-gram 的 N 值
         */
        std::int32_t getTokenLength() const {
            return config_.token_len;
        }

        /** 
         * @brief 获取压缩方式 
         * @return 压缩方法枚举值
         */
        CompressMethod getCompressMethod() const {
            return config_.compress_method;
        }

        /** 
         * @brief 是否启用短语搜索 
         * @return 若启用返回 true
         */
        bool isPhraseSearchEnabled() const {
            return config_.enable_phrase_search;
        }

        /** 
         * @brief 获取缓冲区刷新阈值 
         * @return 缓冲区 token 数量阈值
         */
        std::int32_t getBufferUpdateThreshold() const {
            return config_.buffer_update_threshold;
        }

        /** 
         * @brief 获取已索引文档数 
         * @return 本次运行中已处理的文档数量
         */
        Count getIndexedCount() const {
            return indexed_count_;
        }

        /** 
         * @brief 获取本次运行的最大索引文档数 
         * @return 最大文档数（-1 表示不限制）
         */
        std::int32_t getMaxIndexCount() const {
            return config_.max_index_count;
        }

        /** 
         * @brief 是否已达到本次运行的索引上限 
         * @return 若已达到或超过上限返回 true
         */
        bool hasReachedIndexLimit() const {
            return (config_.max_index_count >= 0) && (indexed_count_ >= config_.max_index_count);
        }

        // 配置设置器 (delegating to Config but also syncing with DB if initialized)
        // Note: For simplicity, setters here might just update config.
        // Syncing logic should be centralized or called explicitly if needed,
        // but current implementation writes to DB on set.

        /** 
         * @brief 设置分词 N 值 
         * 
         * 必须在索引构建前设置。如果数据库已有数据，此操作可能会警告或被忽略。
         * 
         * @param len N-gram 长度
         */
        void setTokenLength(std::int32_t len) {
            config_.token_len = len;
            // 仅在初始化且数据库可能为空或需要强制更新时写入
            if (initialized_) {
                // 通常只在创建新数据库时设置及写入一次
                database_.setSetting("token_len", std::to_string(config_.token_len));
            }
        }

        /** 
         * @brief 设置压缩方式 
         * 
         * 必须在索引构建前设置。如果数据库已有数据，此操作可能会警告或被忽略。
         * 
         * @param method 压缩方法枚举值
         */
        void setCompressMethod(CompressMethod method) {
            config_.compress_method = method;
            // 仅在初始化且数据库可能为空或需要强制更新时写入
            if (initialized_) {
                 // 通常只在创建新数据库时设置及写入一次
                database_.setSetting("compress_method", std::to_string(static_cast<int>(config_.compress_method)));
            }
        }

        /** 
         * @brief 启用/禁用短语搜索 (Runtime only)
         * 
         * 这是一个运行时查询标志，不需要持久化到数据库。
         * 
         * @param enabled 是否启用
         */
        void setPhraseSearchEnabled(bool enabled) {
            config_.enable_phrase_search = enabled;
            // Runtime parameter, no need to persist
        }

        /** 
         * @brief 设置评分方法 (Runtime only)
         * 
         * 这是一个运行时查询参数，不需要持久化到数据库。
         * 
         * @param method 评分方法枚举值
         */
        void setScoringMethod(ScoringMethod method) {
            config_.scoring_method = method;
            // Runtime parameter, no need to persist
        }

        /** 
         * @brief 设置缓冲区刷新阈值 (Runtime only)
         * 
         * @param threshold 缓冲区 token 数量阈值
         */
        void setBufferUpdateThreshold(std::int32_t threshold) {
            config_.buffer_update_threshold = threshold;
            // Runtime parameter, no need to persist
        }

        /** 
         * @brief 设置本次运行的最大索引文档数 (Runtime only)
         * 
         * @param max_count 最大文档数（-1 表示不限制）
         */
        void setMaxIndexCount(std::int32_t max_count) {
            config_.max_index_count = max_count;
            // Runtime parameter, no need to persist
        }

        // 组件访问器
        /** 
         * @brief 获取数据库组件（可写） 
         * @return 数据库引用
         */
        Database& getDatabase() {
            return database_;
        }

        /** 
         * @brief 获取数据库组件（只读） 
         * @return 数据库常量引用
         */
        const Database& getDatabase() const {
            return database_;
        }

        /** 
         * @brief 获取搜索引擎组件 
         * @return 搜索引擎引用
         */
        SearchEngine& getSearchEngine() {
            return search_engine_;
        }

        /** 
         * @brief 获取文档 token 数量 (优化：内存缓存) 
         * @param doc_id 文档 ID
         * @return Token 数量
         */
        int getDocumentTokenCount(DocId doc_id) const;

        /** 
         * @brief 获取全局 token 总数 
         * @return 所有文档的 Token 总数
         */
        long long getTotalTokenCount() const {
            return total_tokens_;
        }

        /** 
         * @brief 获取分词器组件 
         * @return 分词器引用
         */
        Tokenizer& getTokenizer() {
            return tokenizer_;
        }

        /** 
         * @brief 获取维基加载器组件 
         * @return 维基加载器引用
         */
        WikiLoader& getWikiLoader() {
            return wiki_loader_;
        }

        // 索引缓冲区管理

        /**
         * @brief 获取内存倒排缓冲区引用
         *
         * 返回的是可变引用，允许调用方直接追加/合并倒排数据；
         * 若存在并发访问，请确保外部同步。
         * @return 倒排索引缓冲区引用
         */
        InvertedIndex& getIndexBuffer() {
            return index_buffer_;
        }

        /** 
         * @brief 递增已索引文档计数 
         */
        void incrementIndexedCount() {
            ++indexed_count_;
        }

        /**
         * @brief 获取当前已索引文档计数
         * @param[out] count 输出参数，存储计数值
         */
        void getMaxIndexedCount(Count& count) const {
            count = indexed_count_;
        }

        /**
         * @brief 刷新内存倒排缓冲区
         *
         * 将内存中的倒排数据与持久化倒排进行合并并提交到数据库。
         * 
         * 执行流程：
         * 1. 开启事务
         * 2. 逐 token 合并/更新
         * 3. 提交事务
         * 4. 清空缓冲
         * 
         * 失败时会回滚事务，并记录错误日志。
         *
         * @note 本方法可能较慢，建议在批量导入后或达到阈值时调用。
         * @warning 非线程安全，外部需确保调用时没有并发写入同一缓冲。
         */
        void flushIndexBuffer();

        /**
         * @brief 添加文档到索引
         *
         * 过程：
         *  1. 写入/更新文档标题与正文；
         *  2. 通过标题查回文档 ID；
         *  3. 使用分词器将正文转换为倒排增量，写入 index_buffer_；
         *  4. 自增 indexed_count_，若达到 max_index_count_ 或触达阈值可触发 flush。
         *
         * 异常与边界：
         *  - 空标题：用于触发“分批结束”，不再处理并可由调用者决定是否 flush。
         *  - 空正文：记录错误并忽略本次添加。
         *  - 失败（数据库/查ID失败等）：记录错误并终止本次添加。
         *
         * @warning 非线程安全；如需并发导入，请在更高层对 addDocument/flush 做串行化。
         *
         * @param title 文档标题（UTF-8）
         * @param body 文档正文（UTF-8）
         */
        void addDocument(const std::string& title, const std::string& body);

    private:
        // 配置
        Config config_;

        Count indexed_count_;
        bool initialized_ = false; // 初始化后才会将 set* 写入数据库

        // 组件
        Database database_;
        SearchEngine search_engine_;
        Tokenizer tokenizer_;
        WikiLoader wiki_loader_;

        // 索引缓冲区
        InvertedIndex index_buffer_;

        // 文档长度缓存 (doc_id -> token_count)
        mutable std::unordered_map<DocId, int> doc_lengths_cache_;
        mutable std::shared_mutex cache_mutex_; // Protects doc_lengths_cache_ and total_tokens_
        mutable bool doc_lengths_loaded_ = false;
        long long total_tokens_ = 0;
    };
} // namespace wiser
