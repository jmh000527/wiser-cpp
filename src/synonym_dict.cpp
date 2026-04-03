/**
 * @file synonym_dict.cpp
 * @brief 同义词词典实现
 */

#include "wiser/synonym_dict.h"
#include "wiser/utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace wiser {

    bool SynonymDict::loadFromFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            spdlog::warn("Cannot open synonym file: {}", path);
            return false;
        }

        std::string line;
        int loaded = 0;
        while (std::getline(file, line)) {
            // 跳过空行和注释
            if (line.empty() || line[0] == '#') continue;

            // 按逗号分割
            std::vector<std::string> words;
            std::istringstream ss(line);
            std::string word;
            while (std::getline(ss, word, ',')) {
                // 去除首尾空格
                size_t start = word.find_first_not_of(" \t\r\n");
                size_t end = word.find_last_not_of(" \t\r\n");
                if (start != std::string::npos) {
                    word = word.substr(start, end - start + 1);
                    // 小写化 ASCII
                    Utils::toLowerAsciiInPlace(word);
                    if (!word.empty()) words.push_back(word);
                }
            }

            if (words.size() < 2) continue; // 至少两个词才构成同义词组

            size_t group_idx = groups_.size();
            groups_.push_back(words);
            for (const auto& w : words) {
                word_to_group_[w] = group_idx;
            }
            ++loaded;
        }

        spdlog::info("Loaded {} synonym groups from {}", loaded, path);
        return true;
    }

    std::vector<std::string> SynonymDict::getSynonyms(const std::string& word) const {
        std::string lower_word = word;
        Utils::toLowerAsciiInPlace(lower_word);

        auto it = word_to_group_.find(lower_word);
        if (it == word_to_group_.end()) return {};

        std::vector<std::string> result;
        const auto& group = groups_[it->second];
        for (const auto& w : group) {
            if (w != lower_word) {
                result.push_back(w);
            }
        }
        return result;
    }

    std::string SynonymDict::expandQuery(const std::string& query) const {
        if (word_to_group_.empty()) return query;

        // 简单分词：按空格分割
        std::istringstream ss(query);
        std::string word;
        std::vector<std::string> parts;
        bool expanded = false;

        while (ss >> word) {
            std::string lower = word;
            Utils::toLowerAsciiInPlace(lower);
            auto synonyms = getSynonyms(lower);
            if (!synonyms.empty()) {
                // 用括号和 OR 连接原词和同义词
                std::string group = "(" + word;
                for (const auto& syn : synonyms) {
                    group += " OR " + syn;
                }
                group += ")";
                parts.push_back(group);
                expanded = true;
            } else {
                parts.push_back(word);
            }
        }

        if (!expanded) return query;

        std::string result;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) result += " ";
            result += parts[i];
        }
        return result;
    }

} // namespace wiser
