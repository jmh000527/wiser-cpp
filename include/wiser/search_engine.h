#pragma once

#include "types.h"
#include "database.h"
#include "postings.h"
#include <string>
#include <string_view>
#include <memory>
#include <utility>
#include <vector>

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
         * @brief 执行搜索并直接打印 Top-N 概览。
         * @param query UTF-8 查询字符串
         */
        void search(std::string_view query);

        /**
         * @brief 执行搜索并返回按分数降序的 (doc_id, score) 列表。
         * @param query UTF-8 查询字符串
         * @return 若无匹配或 token 解析失败，返回空向量。
         */
        std::vector<std::pair<DocId, double>> searchWithResults(std::string_view query);

        /**
         * @brief 打印查询词元对应的倒排索引（调试用）。
         * @param query UTF-8 查询字符串
         */
        void printInvertedIndexForQuery(std::string_view query);

        /**
         * @brief 打印数据库中所有文档的标题与正文（调试/查看用）。
         */
        void printAllDocumentBodies();

        /**
         * @brief 打印查询结果正文（按得分排序，带 UTF-8 预览）。
         * @param query UTF-8 查询字符串
         */
        void printSearchResultBodies(std::string_view query) const;

    private:
        WiserEnvironment* env_;

        // 辅助函数
        /**
         * @brief 将查询解析为 TokenId 列表，遵循环境中的 N-gram 设定与忽略字符策略。
         */
        [[nodiscard]] std::vector<TokenId> getTokenIds(std::string_view query) const;

        /**
         * @brief 对多个已排序的 doc 列表做交集。
         */
        static std::vector<DocId> intersectPostings(const std::vector<std::vector<DocId>>& postings_lists);

        /**
         * @brief 打印简要结果行。
         */
        void displayResults(const std::vector<std::pair<DocId, double>>& results) const;

        /**
         * @brief 查询执行 + 短语匹配 + TF-IDF 排名的可复用核心。
         */
        [[nodiscard]] std::vector<std::pair<DocId, double>> rankQuery(std::string_view query) const;
    };
} // namespace wiser
