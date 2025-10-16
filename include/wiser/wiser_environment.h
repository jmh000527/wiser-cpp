#pragma once

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
        }

        /** 设置压缩方式 */
        void setCompressMethod(CompressMethod method) {
            compress_method_ = method;
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
        void flushIndexBuffer();

        /**
         * 添加文档到索引
         * @param title 文档标题
         * @param body 文档内容
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
        static constexpr bool DEFAULT_ENABLE_PHRASE_SEARCH = true;
        static constexpr std::int32_t DEFAULT_BUFFER_UPDATE_THRESHOLD = 2048;
    };
} // namespace wiser
