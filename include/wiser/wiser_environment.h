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
#include <string>
#include <memory>
#include <cstdint>

namespace wiser {
    /**
     * Wiser搜索引擎环境类 - 管理整个搜索引擎的配置和组件
     */
    class WiserEnvironment {
    public:
        /** 构造环境 */
        WiserEnvironment();
        ~WiserEnvironment() = default;

        // 不可复制，可移动
        WiserEnvironment(const WiserEnvironment&) = delete;
        WiserEnvironment& operator=(const WiserEnvironment&) = delete;
        WiserEnvironment(WiserEnvironment&&) = default;
        WiserEnvironment& operator=(WiserEnvironment&&) = default;

        /**
         * 初始化环境
         * @param db_path 数据库路径
         * @return true成功，false失败
         */
        bool initialize(const std::string& db_path);

        /**
         * 关闭环境（保存设置并关闭数据库）
         */
        void shutdown();

        // 配置访问器
        /** 获取数据库路径 */
        const std::string& getDatabasePath() const {
            return db_path_;
        }

        /** 获取分词 N 值 */
        std::int32_t getTokenLength() const {
            return token_len_;
        }

        /** 获取压缩方式 */
        CompressMethod getCompressMethod() const {
            return compress_method_;
        }

        /** 是否启用短语搜索 */
        bool isPhraseSearchEnabled() const {
            return enable_phrase_search_;
        }

        /** 获取缓冲区刷新阈值 */
        std::int32_t getBufferUpdateThreshold() const {
            return buffer_update_threshold_;
        }

        /** 获取已索引文档数 */
        Count getIndexedCount() const {
            return indexed_count_;
        }

        /** 获取本次运行的最大索引文档数（-1 表示不限制） */
        std::int32_t getMaxIndexCount() const {
            return max_index_count_;
        }

        /** 是否已达到本次运行的索引上限 */
        bool hasReachedIndexLimit() const {
            return (max_index_count_ >= 0) && (indexed_count_ >= max_index_count_);
        }

        // 配置设置器
        /** 设置分词 N 值 */
        void setTokenLength(std::int32_t len) {
            token_len_ = len;
            if (initialized_) {
                database_.setSetting("token_len", std::to_string(token_len_));
            }
        }

        /** 设置压缩方式 */
        void setCompressMethod(CompressMethod method) {
            compress_method_ = method;
            if (initialized_) {
                database_.setSetting("compress_method", std::to_string(static_cast<int>(compress_method_)));
            }
        }

        /** 启用/禁用短语搜索 */
        void setPhraseSearchEnabled(bool enabled) {
            enable_phrase_search_ = enabled;
        }

        /** 设置缓冲区刷新阈值 */
        void setBufferUpdateThreshold(std::int32_t threshold) {
            buffer_update_threshold_ = threshold;
        }

        /** 设置本次运行的最大索引文档数（-1 表示不限制） */
        void setMaxIndexCount(std::int32_t max_count) {
            max_index_count_ = max_count;
            if (initialized_) {
                database_.setSetting("max_index_count", std::to_string(max_index_count_));
            }
        }

        // 组件访问器
        /** 获取数据库组件（可写） */
        Database& getDatabase() {
            return database_;
        }

        /** 获取数据库组件（只读） */
        const Database& getDatabase() const {
            return database_;
        }

        /** 获取搜索引擎组件 */
        SearchEngine& getSearchEngine() {
            return search_engine_;
        }

        /** 获取分词器组件 */
        Tokenizer& getTokenizer() {
            return tokenizer_;
        }

        /** 获取维基加载器组件 */
        WikiLoader& getWikiLoader() {
            return wiki_loader_;
        }

        // 索引缓冲区管理
        /** 获取倒排索引缓冲区 */
        /**
         * @brief 获取内存倒排缓冲区引用。
         *
         * 返回的是可变引用，允许调用方直接追加/合并倒排数据；
         * 若存在并发访问，请确保外部同步。
         */
        InvertedIndex& getIndexBuffer() {
            return index_buffer_;
        }

        /** 递增已索引文档计数 */
        void incrementIndexedCount() {
            ++indexed_count_;
        }

        void getMaxIndexedCount(Count& count) const {
            count = indexed_count_;
        }

        /** 刷新缓冲区（合并并落库） */
        /**
         * @brief 刷新内存倒排缓冲区，将其与持久化倒排进行合并并提交到数据库。
         *
         * 执行流程：开启事务 -> 逐 token 合并/更新 -> 提交事务 -> 清空缓缓冲。
         * 失败时会回滚事务，记录错误日志。
         *
         * 注意：
         *  - 本方法可能较慢，建议在批量导入后或达到阈值时调用。
         *  - 非线程安全，外部需确保调用时没有并发写入同一缓冲。
         */
        void flushIndexBuffer();

        /**
         * @brief 添加文档到索引（写正排并将倒排增量写入内存缓冲）。
         *
         * 过程：
         *  1) 写入/更新文档标题与正文；
         *  2) 通过标题查回文档ID；
         *  3) 使用分词器将正文转换为倒排增量，写入 index_buffer_；
         *  4) 自增 indexed_count_，若达到 max_index_count_ 或触达阈值可触发 flush。
         *
         * 异常与边界：
         *  - 空标题：用于触发“分批结束”，不再处理并可由调用者决定是否 flush。
         *  - 空正文：记录错误并忽略本次添加。
         *  - 失败（数据库/查ID失败等）：记录错误并终止本次添加。
         *
         * 线程：非线程安全；如需并发导入，请在更高层对 addDocument/flush 做串行化。
         *
         * @param title 文档标题（UTF-8）
         * @param body 文档正文（UTF-8）
         */
        void addDocument(const std::string& title, const std::string& body);

    private:
        // 配置
        std::string db_path_;
        std::int32_t token_len_;
        CompressMethod compress_method_;
        bool enable_phrase_search_;
        std::int32_t buffer_update_threshold_;
        Count indexed_count_;
        std::int32_t max_index_count_; // -1 = 不限制
        bool initialized_ = false; // 初始化后才会将 set* 写入数据库

        // 组件
        Database database_;
        SearchEngine search_engine_;
        Tokenizer tokenizer_;
        WikiLoader wiki_loader_;

        // 索引缓冲区
        InvertedIndex index_buffer_;

        // 默认配置常量
        static constexpr std::int32_t DEFAULT_TOKEN_LEN = N_GRAM;
        static constexpr CompressMethod DEFAULT_COMPRESS_METHOD = CompressMethod::NONE;
        static constexpr bool DEFAULT_ENABLE_PHRASE_SEARCH = false; // 默认关闭短语搜索
        static constexpr std::int32_t DEFAULT_BUFFER_UPDATE_THRESHOLD = 2048;
    };
} // namespace wiser
