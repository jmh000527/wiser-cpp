/**
 * @file synonym_dict.h
 * @brief 同义词词典，用于查询时扩展同义词。
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace wiser {

    /**
     * @class SynonymDict
     * @brief 管理同义词映射，支持查询时同义词扩展。
     *
     * 同义词文件格式（每行一组同义词，逗号分隔）：
     *   电脑,计算机,computer
     *   搜索,检索,查找
     */
    class SynonymDict {
    public:
        SynonymDict() = default;

        /**
         * @brief 从文件加载同义词
         * @param path 同义词文件路径
         * @return 加载成功返回 true
         */
        bool loadFromFile(const std::string& path);

        /**
         * @brief 获取一个词的所有同义词（不含自身）
         * @param word 查询词
         * @return 同义词列表
         */
        [[nodiscard]] std::vector<std::string> getSynonyms(const std::string& word) const;

        /**
         * @brief 扩展查询：对查询中的每个词添加 OR 同义词
         * @param query 原始查询
         * @return 扩展后的查询（如果有同义词则用 OR 连接）
         */
        [[nodiscard]] std::string expandQuery(const std::string& query) const;

        /** @brief 同义词组数量 */
        [[nodiscard]] size_t groupCount() const { return groups_.size(); }

        /** @brief 是否为空 */
        [[nodiscard]] bool empty() const { return word_to_group_.empty(); }

    private:
        // 每组同义词
        std::vector<std::vector<std::string>> groups_;
        // 词 → 所属组索引
        std::unordered_map<std::string, size_t> word_to_group_;
    };

} // namespace wiser
