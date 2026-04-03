/**
 * @file search_engine.h
 * @brief 搜索引擎核心组件，负责执行查询与排序。
 */

#pragma once

#include "types.h"
#include "database.h"
#include "postings.h"
#include "query_parser.h"
#include <string>
#include <string_view>
#include <memory>
#include <utility>
#include <vector>
#include <list>
#include <unordered_map>
#include <mutex>

namespace wiser {
    class WiserEnvironment;

    /**
     * @class SearchEngine
     * @brief 负责执行查询、整合倒排、短语匹配并按 TF-IDF 打分排序的核心组件。
     *
     * 典型流程：
     *  - 将查询转成 N-gram TokenId 序列（忽略标点/空白，ASCII 转小写）
     *  - 读取每个 Token 的持久化倒排 + 内存缓冲倒排，并进行合并
     *  - 可选：短语匹配（位置相邻校验）
     *  - 基于简单 TF(1+log tf) * IDF 的评分，降序返回
     *
     * 线程安全：
     *  - SearchEngine 持有的指针指向 WiserEnvironment；对环境/数据库的并发访问需外部保证
     *  - 本类本身不做锁，适合在受控线程模型中使用
     */
    class SearchEngine {
    public:
        /**
         * @brief 构造搜索引擎
         * @param env 环境指针（须在生命周期内有效）
         */
        explicit SearchEngine(WiserEnvironment* env);
        ~SearchEngine() = default;

        // 不可复制，可移动
        SearchEngine(const SearchEngine&) = delete;
        SearchEngine& operator=(const SearchEngine&) = delete;
        SearchEngine(SearchEngine&&) = default;
        SearchEngine& operator=(SearchEngine&&) = default;

        /**
         * @brief 执行搜索并直接打印 Top-N 概览
         * @param query UTF-8 查询字符串
         */
        void search(std::string_view query) const;

        /**
         * @brief 执行搜索并返回按分数降序的 (doc_id, score) 列表
         * @param query UTF-8 查询字符串
         * @param fuzzy_distance 模糊匹配最大编辑距离（0=精确，默认0）
         * @return 若无匹配或 token 解析失败，返回空向量
         */
        std::vector<std::pair<DocId, double>> searchWithResults(
            std::string_view query, int fuzzy_distance = 0) const;

        /**
         * @brief 拼写纠正建议
         * @param query 原始查询
         * @return 建议的纠正查询（空字符串表示无建议）
         */
        std::string spellCheck(std::string_view query) const;

        /**
         * @brief 打印查询词元对应的倒排索引（调试用）
         * @param query UTF-8 查询字符串
         */
        void printInvertedIndexForQuery(std::string_view query) const;

        /**
         * @brief 打印数据库中所有文档的标题与正文（调试/查看用）
         */
        void printAllDocumentBodies() const;

        /**
         * @brief 打印查询结果正文（按得分排序，带 UTF-8 预览）
         * @param query UTF-8 查询字符串
         */
        void printSearchResultBodies(std::string_view query) const;

        /**
         * @brief 清空查询缓存（索引变更后调用）
         */
        void invalidateCache();

    private:
        WiserEnvironment* env_;

        // LRU 查询结果缓存
        struct CacheEntry {
            std::string key;
            std::vector<std::pair<DocId, double>> results;
        };
        static constexpr size_t kMaxCacheSize = 1024;
        mutable std::mutex cache_mutex_;
        mutable std::list<CacheEntry> cache_list_;
        mutable std::unordered_map<std::string, std::list<CacheEntry>::iterator> cache_map_;

        std::vector<std::pair<DocId, double>> cacheLookup(const std::string& key) const;
        void cacheInsert(const std::string& key, const std::vector<std::pair<DocId, double>>& results) const;

        // 辅助函数
        std::vector<std::pair<DocId, double>> rankQuery(std::string_view query) const;

        /**
         * @brief 将查询解析为 TokenId 列表
         * 
         * 遵循环境中的 N-gram 设定与忽略字符策略。
         * @param query 查询字符串
         * @return TokenId 列表
         */
        std::vector<TokenId> getTokenIds(std::string_view query) const;

        /**
         * @brief 模糊查询：将查询解析为 TokenId 列表，精确匹配失败时尝试模糊匹配
         * @param query 查询字符串
         * @param max_edit_distance 最大编辑距离（0=精确，1-2=模糊）
         * @param fuzzy_penalty 模糊匹配的分数衰减系数
         * @return (TokenId, penalty_weight) 列表
         */
        std::vector<std::pair<TokenId, double>> getTokenIdsFuzzy(
            std::string_view query, int max_edit_distance = 1) const;

        /**
         * @brief 求多个文档 ID 列表的交集（结果仍有序）
         * @param postings_lists 多个倒排列表的文档 ID 集合
         * @return 交集后的文档 ID 列表
         */
        static std::vector<DocId> intersectPostings(const std::vector<std::vector<DocId>>& postings_lists);

        /**
         * @brief 打印搜索结果列表
         * @param results (DocId, Score) 列表
         */
        void displayResults(const std::vector<std::pair<DocId, double>>& results) const;

        // 重构辅助结构与函数
        struct QueryData {
            std::vector<std::vector<DocId>> token_postings;
            std::vector<Count> docs_counts;
            std::vector<std::unordered_map<DocId, Count>> token_tf_maps;
            std::vector<std::unordered_map<DocId, std::vector<Position>>> token_pos_maps;
        };

        QueryData fetchPostings(const std::vector<TokenId>& token_ids) const;
        std::vector<DocId> getCandidateDocs(const QueryData& qd) const;
        std::vector<DocId> filterByPhrase(
            const std::vector<DocId>& candidates, const QueryData& qd, const std::vector<TokenId>& token_ids) const;

        std::vector<std::pair<DocId, double>> calculateScores(
            const std::vector<DocId>& result_docs, const QueryData& qd, const std::vector<TokenId>& token_ids) const;

        // 布尔查询执行
        struct BooleanResult {
            std::vector<DocId> doc_ids;                                       ///< 匹配的文档ID集（有序）
            std::vector<TokenId> all_token_ids;                              ///< 所有涉及的词元ID
            QueryData merged_qd;                                              ///< 合并的倒排数据
        };

        std::vector<std::pair<DocId, double>> rankBooleanQuery(std::string_view query) const;
        BooleanResult executeBooleanTree(const QueryNode* node) const;
        BooleanResult executeTermNode(const std::string& term) const;
        BooleanResult executePhraseNode(const std::string& phrase) const;

        static std::vector<DocId> unionDocIds(const std::vector<DocId>& a, const std::vector<DocId>& b);
        static std::vector<DocId> differenceDocIds(const std::vector<DocId>& a, const std::vector<DocId>& b);
    };
} // namespace wiser
