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
     * 搜索引擎类 - 负责执行搜索查询
     */
    class SearchEngine {
    public:
        /**
         * 构造搜索引擎
         * @param env 环境指针
         */
        explicit SearchEngine(WiserEnvironment* env);
        ~SearchEngine() = default;

        // 不可复制，可移动
        SearchEngine(const SearchEngine&) = delete;
        SearchEngine& operator=(const SearchEngine&) = delete;
        SearchEngine(SearchEngine&&) = default;
        SearchEngine& operator=(SearchEngine&&) = default;

        /**
         * 执行搜索
         * @param query 查询字符串
         */
        void search(std::string_view query);

        /**
         * 打印查询词元对应的倒排索引（调试用）
         * @param query 查询字符串
         */
        void printInvertedIndexForQuery(std::string_view query);

        /**
         * 打印数据库中所有文档的标题与正文（调试/查看用）
         */
        void printAllDocumentBodies();

        /**
         * 打印查询结果的正文（按得分排序）
         * @param query 查询字符串
         */
        void printSearchResultBodies(std::string_view query) const;

    private:
        WiserEnvironment* env_;

        // 辅助函数
        [[nodiscard]] std::vector<TokenId> getTokenIds(std::string_view query) const;
        static std::vector<DocId> intersectPostings(const std::vector<std::vector<DocId>>& postings_lists);
        void displayResults(const std::vector<std::pair<DocId, double>>& results) const;

        // 将查询执行、短语匹配与 TF-IDF 排序封装为可复用逻辑
        [[nodiscard]] std::vector<std::pair<DocId, double>> rankQuery(std::string_view query) const;
    };
} // namespace wiser
