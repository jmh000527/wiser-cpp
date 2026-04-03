/**
 * @file search_engine.cpp
 * @brief Wiser搜索引擎核心实现文件
 * 
 * 此文件实现了搜索引擎的核心功能：
 * - 查询处理与倒排索引检索
 * - 搜索结果评分与排序
 * - 短语搜索和位置感知搜索
 * - 性能统计和日志记录
 */

#include "wiser/search_engine.h"
#include "wiser/wiser_environment.h"
#include "wiser/tokenizer.h"
#include "wiser/utils.h"
#include "wiser/query_parser.h"
#include "wiser/console.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include <set>
#include <string>
#include <string_view>
#include <ranges>
#include <format>
#include <spdlog/spdlog.h>
#include <chrono>
#include "wiser/config.h"

namespace wiser {
    // 内部结构体，不需要放在头文件中污染 SearchEngine 类定义
    /**
     * @brief 搜索结果内部实现结构体
     * 
     * 用于存储搜索结果的文档ID和评分，仅在内部使用
     */
    struct SearchResultImpl {
        DocId document_id;    // 文档ID
        double score;         // 文档评分

        /**
         * @brief 构造函数
         * @param id 文档ID
         * @param s 评分分数
         */
        SearchResultImpl(DocId id, double s)
            : document_id(id), score(s) {}
    };

    // ================================================================
    // LRU 查询缓存
    // ================================================================

    std::vector<std::pair<DocId, double>> SearchEngine::cacheLookup(const std::string& key) const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) return {};
        // 移到列表头部（最近使用）
        cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
        return it->second->results;
    }

    void SearchEngine::cacheInsert(const std::string& key, const std::vector<std::pair<DocId, double>>& results) const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            it->second->results = results;
            cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
            return;
        }
        // 淘汰最旧的缓存项
        if (cache_map_.size() >= kMaxCacheSize) {
            auto& oldest = cache_list_.back();
            cache_map_.erase(oldest.key);
            cache_list_.pop_back();
        }
        cache_list_.push_front({key, results});
        cache_map_[key] = cache_list_.begin();
    }

    void SearchEngine::invalidateCache() {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_list_.clear();
        cache_map_.clear();
    }

    /**
     * @brief SearchEngine 构造函数
     * 
     * 初始化搜索引擎，关联到指定的Wiser环境
     * 
     * @param env Wiser环境指针
     */
    SearchEngine::SearchEngine(WiserEnvironment* env)
        : env_(env) {}

    std::string SearchEngine::spellCheck(std::string_view query) const {
        // 对查询进行 N-gram 分词，检查每个 token 是否存在于索引中
        auto token_ids = getTokenIds(query);
        if (token_ids.empty()) return {};

        // 查询字符串的所有 N-gram tokens
        int n = env_->getTokenLength();
        auto tokens = Utils::tokenizeQueryTokens(std::string(query), n);
        if (tokens.empty()) return {};

        bool any_correction = false;
        std::string corrected = std::string(query);

        for (const auto& tok : tokens) {
            auto info = env_->getDatabase().getTokenInfo(tok, false);
            if (info.has_value()) continue; // token exists

            // Token not found — find closest match
            auto matches = env_->getDatabase().findSimilarTokens(tok, 2, 1);
            if (!matches.empty() && matches[0].distance <= 2) {
                // Replace the first occurrence of the misspelled token in corrected query
                auto pos = corrected.find(tok);
                if (pos != std::string::npos) {
                    corrected.replace(pos, tok.size(), matches[0].token);
                    any_correction = true;
                }
            }
        }

        return any_correction ? corrected : std::string{};
    }

    /**
     * @brief 获取倒排索引数据
     * 
     * 根据token ID列表从数据库和内存缓冲区获取倒排索引数据
     * 包括文档列表、词频映射和位置映射
     * 
     * @param token_ids token ID列表
     * @return QueryData 查询数据结构，包含所有token的倒排信息
     */
    SearchEngine::QueryData SearchEngine::fetchPostings(const std::vector<TokenId>& token_ids) const {
        QueryData qd;
        // 预分配空间以提高性能
        qd.token_postings.reserve(token_ids.size());
        qd.docs_counts.reserve(token_ids.size());
        qd.token_tf_maps.reserve(token_ids.size());
        qd.token_pos_maps.reserve(token_ids.size());

        // 遍历所有token ID
        for (TokenId token_id: token_ids) {
            // 从数据库获取持久化的倒排索引记录
            auto rec = env_->getDatabase().getPostings(token_id);
            // 从内存缓冲区获取未持久化的倒排索引记录
            auto mem_postings_list = env_->getIndexBuffer().getPostingsList(token_id);
            
            // 如果数据库中有该token的记录
            if (rec.has_value()) {
                // 反序列化倒排列表
                PostingsList postings_list;
                // 使用环境配置的压缩方式
                postings_list.deserialize(rec->postings, env_->getConfig().compress_method);

                // 提取文档ID列表与TF/位置映射（过滤无效ID）
                std::vector<DocId> doc_ids;
                std::unordered_map<DocId, Count> tf_map;
                std::unordered_map<DocId, std::vector<Position>> pos_map;

                // 先处理持久化的倒排索引
                for (const auto& item: postings_list.getItems()) {
                    DocId did = item->getDocumentId();
                    if (did <= 0) {
                        continue;  // 跳过无效文档ID
                    }
                    const auto& positions = item->getPositions();
                    doc_ids.push_back(did);
                    tf_map[did] = static_cast<Count>(positions.size());  // 记录词频
                    pos_map[did] = positions; // 假定为升序
                }

                // 再合并内存缓冲区的倒排索引（若有）
                if (mem_postings_list) {
                    for (const auto& item: mem_postings_list->getItems()) {
                        DocId did = item->getDocumentId();
                        if (did <= 0) {
                            continue;  // 跳过无效文档ID
                        }
                        const auto& positions = item->getPositions();
                        if (!tf_map.contains(did)) {
                            // 新文档 - 内存缓冲区中有但数据库中还没有
                            doc_ids.push_back(did);
                            tf_map[did] = static_cast<Count>(positions.size());
                            pos_map[did] = positions; // 假定为升序
                        } else {
                            // 已有文档，合并词频和位置信息
                            tf_map[did] += static_cast<Count>(positions.size());
                            auto& existing_positions = pos_map[did];
                            existing_positions.insert(existing_positions.end(), positions.begin(), positions.end());
                            std::ranges::sort(existing_positions); // 保持升序
                        }
                    }
                }
                std::ranges::sort(doc_ids); // 显式排序，保证交集稳定

                // 将处理好的数据添加到查询数据结构中
                qd.docs_counts.push_back(static_cast<Count>(doc_ids.size()));
                qd.token_postings.push_back(std::move(doc_ids));
                qd.token_tf_maps.push_back(std::move(tf_map));
                qd.token_pos_maps.push_back(std::move(pos_map));
            } else if (mem_postings_list) {
                // 数据库中没有该token的记录，但内存缓冲区中有
                std::vector<DocId> doc_ids;
                std::unordered_map<DocId, Count> tf_map;
                std::unordered_map<DocId, std::vector<Position>> pos_map;

                for (const auto& item: mem_postings_list->getItems()) {
                    DocId did = item->getDocumentId();
                    if (did <= 0) continue;
                    const auto& positions = item->getPositions();
                    doc_ids.push_back(did);
                    tf_map[did] = static_cast<Count>(positions.size());
                    pos_map[did] = positions;
                }
                std::ranges::sort(doc_ids);

                qd.docs_counts.push_back(static_cast<Count>(doc_ids.size()));
                qd.token_postings.push_back(std::move(doc_ids));
                qd.token_tf_maps.push_back(std::move(tf_map));
                qd.token_pos_maps.push_back(std::move(pos_map));
            } else {
                // 数据库和内存缓冲区都没有该token的记录
                qd.token_postings.emplace_back();
                qd.docs_counts.push_back(0);
                qd.token_tf_maps.emplace_back();
                qd.token_pos_maps.emplace_back();
            }
        }
        return qd;
    }

    /**
     * @brief 获取候选文档列表
     * 
     * 通过倒排列表交集运算获取包含所有查询词的候选文档
     * 并过滤掉无效的文档ID
     * 
     * @param qd 查询数据结构，包含所有token的倒排信息
     * @return std::vector<DocId> 候选文档ID列表
     */
    std::vector<DocId> SearchEngine::getCandidateDocs(const QueryData& qd) const {
        // 对多个token的倒排列表进行交集运算，获取同时包含所有查询词的文档
        std::vector<DocId> candidate_docs = intersectPostings(qd.token_postings);
        
        // 过滤掉无效的文档ID（小于等于0的ID）
        candidate_docs.erase(std::ranges::remove_if(candidate_docs, [](DocId d) {
                                 return d <= 0;  // 过滤无效文档ID
                             }).begin(),
                             candidate_docs.end());
        
        return candidate_docs;
    }

    /**
     * @brief 通过短语匹配过滤候选文档
     * 
     * 对候选文档进行短语匹配过滤，只保留满足短语顺序要求的文档
     * 使用双指针算法进行位置序列匹配
     * 
     * @param candidates 候选文档ID列表
     * @param qd 查询数据结构，包含位置映射信息
     * @param token_ids token ID列表
     * @return std::vector<DocId> 通过短语匹配过滤后的文档ID列表
     */
    std::vector<DocId> SearchEngine::filterByPhrase(const std::vector<DocId>& candidates, const QueryData& qd, const std::vector<TokenId>& token_ids) const {
        std::vector<DocId> result_docs;
        const bool phrase_enabled = env_->isPhraseSearchEnabled();
        
        // 只有在启用短语搜索且查询词数量大于1时才进行短语匹配
        if (phrase_enabled && token_ids.size() > 1 && !qd.token_pos_maps.empty()) {
            result_docs.reserve(candidates.size());
            
            // 遍历所有候选文档
            for (DocId doc_id: candidates) {
                bool ok = true;  // 标记当前文档是否满足短语匹配
                
                // 初始为第一个词的位置集合
                std::vector<Position> current_positions; {
                    auto it0 = qd.token_pos_maps[0].find(doc_id);
                    if (it0 == qd.token_pos_maps[0].end()) {
                        ok = false;  // 第一个词在文档中不存在
                    } else {
                        current_positions = it0->second;  // 获取第一个词的所有位置
                    }
                }
                
                // 逐词推进：保留满足 pos_{i+1} = pos_i + 1 的位置链
                for (size_t i = 1; ok && i < token_ids.size(); ++i) {
                    auto iti = qd.token_pos_maps[i].find(doc_id);
                    if (iti == qd.token_pos_maps[i].end()) {
                        ok = false;  // 当前词在文档中不存在
                        break;
                    }
                    const auto& next_positions_vec = iti->second; // 升序排列的位置向量

                    std::vector<Position> advanced;
                    advanced.reserve(current_positions.size());

                    // 双指针匹配算法：寻找满足 pos_{i+1} = pos_i + 1 的位置对
                    size_t p = 0, q = 0;
                    while (p < current_positions.size() && q < next_positions_vec.size()) {
                        Position need = static_cast<Position>(current_positions[p] + 1);  // 需要的位置 = 当前位置 + 1
                        Position got = next_positions_vec[q];  // 实际存在的位置
                        
                        if (got == need) {
                            // 找到匹配的位置对
                            advanced.push_back(need);
                            ++p;
                            ++q;
                        } else if (got < need) {
                            // 实际位置小于需要的位置，移动右指针
                            ++q;
                        } else {
                            // 实际位置大于需要的位置，移动左指针
                            ++p;
                        }
                    }

                    if (advanced.empty()) {
                        // 没有找到任何匹配的位置对
                        ok = false;
                        break;
                    }
                    current_positions = std::move(advanced);  // 更新当前位置链
                }
                if (ok) {
                    // 文档满足所有短语匹配条件
                    result_docs.push_back(doc_id);
                }
            }
        } else {
            // 未启用短语搜索或查询词数量不足，返回所有候选文档
            result_docs = candidates;
        }
        return result_docs;
    }

    /**
     * @brief 计算搜索结果评分
     * 
     * 根据BM25或TF-IDF算法计算搜索结果的评分
     * 
     * @param result_docs 结果文档ID列表
     * @param qd 查询数据结构
     * @param token_ids token ID列表
     * @return std::vector<std::pair<DocId, double>> 文档ID和评分对的列表
     */
    std::vector<std::pair<DocId, double>> SearchEngine::calculateScores(const std::vector<DocId>& result_docs, const QueryData& qd, const std::vector<TokenId>& token_ids) const {

        // 获取文档集合统计信息
        Count total_docs = env_->getDatabase().getDocumentCount();  // 总文档数
        long long total_tokens = env_->getTotalTokenCount();        // 总token数
        double avgdl = total_docs > 0 ? static_cast<double>(total_tokens) / static_cast<double>(total_docs) : 1.0;  // 平均文档长度
        if (avgdl <= 0.0) avgdl = 1.0;  // 防止除以零

        // BM25算法的可调参数
        const double k1 = env_->getConfig().bm25_k1;  // BM25 k1参数，控制词频饱和度
        const double b = env_->getConfig().bm25_b;    // BM25 b参数，控制文档长度归一化

        // 计算每个查询词的IDF（逆文档频率）
        std::vector<double> idfs;
        idfs.reserve(qd.docs_counts.size());
        for (auto df: qd.docs_counts) {
            double idf = 0.0;
            if (env_->getConfig().scoring_method == ScoringMethod::BM25) {
                // BM25 IDF公式: log( (N - df + 0.5) / (df + 0.5) + 1 )
                double numerator = static_cast<double>(total_docs) - static_cast<double>(df) + 0.5;  // N - df + 0.5
                double denominator = static_cast<double>(df) + 0.5;  // df + 0.5
                idf = std::log(numerator / denominator + 1.0);  // log((N-df+0.5)/(df+0.5) + 1)
            } else {
                // TF-IDF IDF公式 (标准): log( (N + 1) / (df + 1) ) + 1
                idf = std::log((1.0 + static_cast<double>(total_docs)) / (
                                      1.0 + static_cast<double>(std::max<Count>(0, df)))) + 1.0;
            }
            // 确保IDF值为非负且有限
            if (idf < 0) idf = 0;
            if (!std::isfinite(idf)) idf = 0.0;
            idfs.push_back(idf);
        }

        // 准备存储评分结果的向量
        std::vector<SearchResultImpl> scored;
        scored.reserve(result_docs.size());

        // 准备循环常量：确定使用BM25还是TF-IDF算法
        const bool use_bm25 = (env_->getConfig().scoring_method == ScoringMethod::BM25);

        // 遍历所有结果文档，计算每个文档的评分
        for (auto doc_id: result_docs) {
            int doc_len = 0;      // 文档长度（token数量）
            double score = 0.0;   // 文档总评分

            // 如果是BM25算法，需要获取文档长度
            if (use_bm25) {
                doc_len = env_->getDocumentTokenCount(doc_id); // Only needed for BM25
            }

            // 遍历所有查询词，累加每个词的贡献分数
            for (size_t i = 0; i < token_ids.size(); ++i) {
                // 查找当前文档在当前查询词中的词频
                auto it = qd.token_tf_maps[i].find(doc_id);
                if (it == qd.token_tf_maps[i].end())
                    continue;  // 文档不包含当前查询词，跳过
                
                // 获取原始词频并确保非负
                Count raw_tf = std::max<Count>(0, it->second);
                if (raw_tf == 0)
                    continue;  // 词频为0，跳过

                // 根据算法类型计算当前查询词的分数贡献
                if (use_bm25) {
                    // BM25算法：tf * (k1 + 1) / (tf + k1 * (1 - b + b * (doc_len / avgdl))) * idf
                    double tf = static_cast<double>(raw_tf);  // 转换为浮点数
                    double numerator = tf * (k1 + 1.0);       // 分子部分：tf * (k1 + 1)
                    double denominator = tf + k1 * (1.0 - b + b * (static_cast<double>(doc_len) / avgdl));  // 分母部分
                    score += idfs[i] * (numerator / denominator);  // 累加当前词的BM25分数
                } else {
                    // TF-IDF算法：tf(1 + log(tf)) * idf
                    double tf = 1.0 + std::log(static_cast<double>(raw_tf));  // 对数词频
                    score += tf * idfs[i];  // 累加当前词的TF-IDF分数
                }
            }
            // 将文档ID和评分存入结果向量
            scored.emplace_back(doc_id, score);
        }

        // 标题匹配权重提升
        const double title_boost = env_->getConfig().title_boost;
        if (title_boost > 1.0) {
            // 获取所有查询 token 的文本用于标题匹配
            std::vector<std::string> query_token_texts;
            query_token_texts.reserve(token_ids.size());
            for (auto tid : token_ids) {
                std::string tok = env_->getDatabase().getToken(tid);
                if (!tok.empty()) {
                    // 小写化
                    for (auto& ch : tok) {
                        unsigned char uc = static_cast<unsigned char>(ch);
                        if (uc < 128) ch = static_cast<char>(std::tolower(uc));
                    }
                    query_token_texts.push_back(std::move(tok));
                }
            }

            for (auto& item : scored) {
                std::string title = env_->getDatabase().getDocumentTitle(item.document_id);
                if (title.empty()) continue;
                // 小写化标题
                for (auto& ch : title) {
                    unsigned char uc = static_cast<unsigned char>(ch);
                    if (uc < 128) ch = static_cast<char>(std::tolower(uc));
                }
                // 检查任一查询 token 是否出现在标题中
                for (const auto& tok : query_token_texts) {
                    if (title.find(tok) != std::string::npos) {
                        item.score *= title_boost;
                        break;
                    }
                }
            }
        }
        
        // 对搜索结果按评分降序排序，评分相同的按文档ID升序排序
        std::ranges::sort(scored, [](const SearchResultImpl& a, const SearchResultImpl& b) {
            return a.score == b.score ? a.document_id < b.document_id : a.score > b.score;
        });
        
        // 转换为最终的显示格式（文档ID和评分对）
        std::vector<std::pair<DocId, double>> display;
        display.reserve(scored.size());
        for (const auto& r: scored)
            display.emplace_back(r.document_id, r.score);
        
        return display;
    }

    /**
     * @brief 打印查询词的倒排索引信息
     * 
     * 用于调试目的，显示查询词在磁盘和内存中的倒排索引详情
     * 
     * @param query 查询字符串
     */
    void SearchEngine::printInvertedIndexForQuery(std::string_view query) const {
        // 获取查询词的token ID列表
        auto token_ids = getTokenIds(query);
        if (token_ids.empty()) {
            spdlog::debug("No valid tokens found in query (inverted index print skipped).");
            return;  // 没有有效的查询词，跳过打印
        }

        // 打印查询词倒排索引的标题信息
        spdlog::debug("Inverted index for query tokens (count={}):", token_ids.size());
        
        // 遍历所有查询词的token ID
        for (TokenId token_id: token_ids) {
            // 获取token对应的字符串表示
            std::string token_str = env_->getDatabase().getToken(token_id);

            // 获取持久化倒排索引信息（磁盘）
            auto rec = env_->getDatabase().getPostings(token_id);
            Count disk_docs_cnt = rec ? rec->docs_count : 0;  // 磁盘中的文档数量

            // 获取内存缓存倒排索引信息
            auto mem_postings_list = env_->getIndexBuffer().getPostingsList(token_id);
            size_t mem_docs_cnt = (mem_postings_list ? mem_postings_list->getItems().size() : 0);  // 内存中的文档数量

            // 打印token基本信息
            if (mem_docs_cnt > 0) {
                spdlog::debug("  - Token=\"{}\" id={} disk_docs={} mem_docs={}", token_str, token_id, disk_docs_cnt,
                              mem_docs_cnt);
            } else {
                spdlog::debug("  - Token=\"{}\" id={} disk_docs={}", token_str, token_id, disk_docs_cnt);
            }

            // 打印持久化倒排索引的详细信息
            if (rec && !rec->postings.empty()) {
                // 反序列化倒排列表
                PostingsList pl;
                pl.deserialize(rec->postings);
                
                // 遍历所有文档项
                for (const auto& item: pl.getItems()) {
                    const auto& pos = item->getPositions();
                    std::string pos_line;
                    pos_line.reserve(pos.size() * 4);  // 预分配空间
                    
                    // 构建位置信息的字符串表示
                    for (size_t i = 0; i < pos.size(); ++i) {
                        if (i)
                            pos_line += ',';  // 添加逗号分隔符
                        pos_line += std::to_string(pos[i]);  // 添加位置数字
                    }
                    
                    // 打印磁盘倒排索引项
                    spdlog::debug("      [disk] doc={} positions=[{}]", item->getDocumentId(), pos_line);
                }
            } else {
                // 磁盘中没有该token的倒排索引
                spdlog::debug("      [disk] <empty>");
            }

            // 打印内存缓存倒排索引的详细信息
            if (mem_postings_list && !mem_postings_list->getItems().empty()) {
                // 遍历内存中的所有文档项
                for (const auto& mem_item: mem_postings_list->getItems()) {
                    const auto& pos = mem_item->getPositions();
                    std::string pos_line;
                    pos_line.reserve(pos.size() * 4);  // 预分配空间
                    
                    // 构建位置信息的字符串表示
                    for (size_t i = 0; i < pos.size(); ++i) {
                        if (i)
                            pos_line += ',';  // 添加逗号分隔符
                        pos_line += std::to_string(pos[i]);  // 添加位置数字
                    }
                    
                    // 打印内存倒排索引项
                    spdlog::debug("      [mem ] doc={} positions=[{}]", mem_item->getDocumentId(), pos_line);
                }
            } else {
                // 内存中没有该token的倒排索引
                spdlog::debug("      [mem ] <empty>");
            }
        }
    }

    /**
     * @brief 执行查询并排名搜索结果
     * 
     * 完整的搜索流程，包括分词、获取倒排索引、候选文档筛选、短语匹配过滤和评分计算
     * 
     * @param query 查询字符串
     * @return std::vector<std::pair<DocId, double>> 排名后的搜索结果
     */
    std::vector<std::pair<DocId, double>> SearchEngine::rankQuery(std::string_view query) const {
        using namespace std::chrono;
        const auto t0 = high_resolution_clock::now();  // 开始计时
        
        // 1) 获取所有查询词元的ID（tokenize）
        std::vector<TokenId> token_ids = getTokenIds(query);
        const auto t1 = high_resolution_clock::now();  // 分词完成时间
        
        // 如果没有有效的查询词，使用LIKE子串查询作为后备方案
        if (token_ids.empty()) {
            // 空查询直接返回空结果，避免 instr(col, "") 匹配所有文档
            std::string trimmed{query};
            trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
            if (trimmed.empty()) {
                spdlog::info("search_log | query=\"\" | tokens=0 | phrase={} | result_count=0 | reason=empty_query | time_ms=0",
                             env_->isPhraseSearchEnabled());
                return {};
            }

            // Fallback: LIKE 子串查询（当查询短于 N 或被全部忽略时）
            auto like_ids = env_->getDatabase().searchDocumentsLike(trimmed);
            std::vector<std::pair<DocId, double>> display;
            display.reserve(like_ids.size());
            for (auto id: like_ids)
                display.emplace_back(id, 1.0);  // 所有结果给固定分数1.0
            
            // 记录LIKE查询的耗时日志
            auto like_us = duration_cast<microseconds>(high_resolution_clock::now() - t1).count();
            spdlog::info(
                         "search_log | query=\"{}\" | tokens=0 | phrase={} | result_count={} | reason=LIKE_fallback | time_ms={:.3f} | breakdown={{like:{}us}}",
                         query,
                         env_->isPhraseSearchEnabled(),
                         display.size(),
                         static_cast<double>(like_us) / 1000.0,
                         like_us
                        );
            return display;
        }

        // 2) 为每个词元提取倒排与辅助映射
        QueryData qd = fetchPostings(token_ids);
        const auto t2 = high_resolution_clock::now();  // 获取倒排索引完成时间

        // 3) 求交集，获取候选文档
        std::vector<DocId> candidate_docs = getCandidateDocs(qd);
        const auto t3 = high_resolution_clock::now();  // 候选文档筛选完成时间
        
        // 如果没有候选文档，记录日志并返回空结果
        if (candidate_docs.empty()) {
            auto tokenize_us = duration_cast<microseconds>(t1 - t0).count();
            auto postings_us = duration_cast<microseconds>(t2 - t1).count();
            auto intersect_us = duration_cast<microseconds>(t3 - t2).count();
            double total_ms = static_cast<double>(tokenize_us + postings_us + intersect_us) / 1000.0;
            spdlog::info(
                         "search_log | query=\"{}\" | tokens={} | phrase={} | result_count=0 | reason=no_candidates | time_ms={:.3f} | breakdown={{tokenize:{}us,postings:{}us,intersect:{}us}}",
                         query,
                         token_ids.size(),
                         env_->isPhraseSearchEnabled(),
                         total_ms,
                         tokenize_us,
                         postings_us,
                         intersect_us
                        );
            return {};
        }
        
        // 4) 若启用短语搜索，做位置相邻校验
        std::vector<DocId> result_docs = filterByPhrase(candidate_docs, qd, token_ids);
        const auto t4 = high_resolution_clock::now();  // 短语匹配过滤完成时间
        
        // 如果短语过滤后没有结果，记录日志并返回空结果
        if (result_docs.empty()) {
            auto tokenize_us = duration_cast<microseconds>(t1 - t0).count();
            auto postings_us = duration_cast<microseconds>(t2 - t1).count();
            auto intersect_us = duration_cast<microseconds>(t3 - t2).count();
            auto phrase_us = duration_cast<microseconds>(t4 - t3).count();
            double total_ms = static_cast<double>(tokenize_us + postings_us + intersect_us + phrase_us) / 1000.0;
            spdlog::info(
                         "search_log | query=\"{}\" | tokens={} | phrase={} | result_count=0 | reason=phrase_filter | time_ms={:.3f} | breakdown={{tokenize:{}us,postings:{}us,intersect:{}us,phrase:{}us}}",
                         query,
                         token_ids.size(),
                         env_->isPhraseSearchEnabled(),
                         total_ms,
                         tokenize_us,
                         postings_us,
                         intersect_us,
                         phrase_us
                        );
            return {};
        }

        // 5) 计算得分
        std::vector<std::pair<DocId, double>> display = calculateScores(result_docs, qd, token_ids);
        const auto t5 = high_resolution_clock::now();  // 评分计算完成时间

        // ---- 汇总日志（精细耗时） ----
        {
            // 计算各阶段耗时（微秒）
            auto tokenize_us = duration_cast<microseconds>(t1 - t0).count();
            auto postings_us = duration_cast<microseconds>(t2 - t1).count();
            auto intersect_us = duration_cast<microseconds>(t3 - t2).count();
            auto phrase_us = duration_cast<microseconds>(t4 - t3).count();
            auto score_us = duration_cast<microseconds>(t5 - t4).count();
            auto total_us = duration_cast<microseconds>(t5 - t0).count();
            double total_ms = static_cast<double>(total_us) / 1000.0;
            
            // 构建token ID列表字符串
            std::string token_line;
            token_line.reserve(token_ids.size() * 6);
            for (size_t i = 0; i < token_ids.size(); ++i) {
                if (i)
                    token_line += ',';
                token_line += std::to_string(token_ids[i]);
            }
            
            // 构建top结果列表字符串（前10个文档:分数）
            std::string top;
            const size_t topN = std::min<size_t>(10, display.size());
            top.reserve(topN * 16);
            for (size_t i = 0; i < topN; ++i) {
                if (i)
                    top += ',';
                top += std::format("{}:{:.4f}", display[i].first, display[i].second);
            }
            
            // 记录完整的搜索日志
            spdlog::info(
                         "search_log | query=\"{}\" | tokens={} [{}] | phrase={} | result_count={} | top=[{}] | time_ms={:.3f} | breakdown={{tokenize:{}us,postings:{}us,intersect:{}us,phrase:{}us,score:{}us}}",
                         query,
                         token_ids.size(),
                         token_line,
                         env_->isPhraseSearchEnabled(),
                         display.size(),
                         top,
                         total_ms,
                         tokenize_us,
                         postings_us,
                         intersect_us,
                         phrase_us,
                         score_us
                        );
        }
        return display;
    }

    void SearchEngine::search(std::string_view query) const {
        auto ranked = rankQuery(query);
        if (ranked.empty()) {
            if (getTokenIds(query).empty()) {
                spdlog::info("No valid tokens found in query.");
            } else {
                spdlog::info("No documents found matching the query.");
            }
            return;
        }
        displayResults(ranked);
    }

    std::vector<std::pair<DocId, double>> SearchEngine::searchWithResults(
        std::string_view query, int fuzzy_distance) const {

        // 缓存查找
        std::string cache_key = std::string(query) + "|f=" + std::to_string(fuzzy_distance);
        auto cached = cacheLookup(cache_key);
        if (!cached.empty()) {
            spdlog::debug("search_log | query=\"{}\" | cache_hit", query);
            return cached;
        }

        std::vector<std::pair<DocId, double>> res;

        // 同义词扩展
        std::string expanded_query;
        const auto& syn_dict = env_->getSynonymDict();
        if (!syn_dict.empty()) {
            expanded_query = syn_dict.expandQuery(std::string(query));
            if (expanded_query != query) {
                spdlog::debug("search_log | synonym_expand=\"{}\" -> \"{}\"", query, expanded_query);
            }
        }
        std::string_view effective_query = expanded_query.empty() ? query : std::string_view(expanded_query);

        if (QueryParser::isBooleanQuery(effective_query)) {
            res = rankBooleanQuery(effective_query);
        } else {
            res = rankQuery(effective_query);
            // 精确搜索无结果且启用模糊时，尝试模糊匹配
            if (res.empty() && fuzzy_distance > 0) {
                auto fuzzy_tokens = getTokenIdsFuzzy(query, fuzzy_distance);
                if (!fuzzy_tokens.empty()) {
                    std::vector<TokenId> ids;
                    ids.reserve(fuzzy_tokens.size());
                    for (const auto& [id, weight] : fuzzy_tokens) {
                        ids.push_back(id);
                    }
                    QueryData qd = fetchPostings(ids);
                    auto candidates = getCandidateDocs(qd);
                    if (!candidates.empty()) {
                        res = calculateScores(candidates, qd, ids);
                        // 对模糊结果施加衰减
                        for (auto& [doc_id, score] : res) {
                            score *= 0.8; // 模糊匹配 20% 分数衰减
                        }
                        spdlog::info("search_log | query=\"{}\" | mode=fuzzy | result_count={}",
                                     query, res.size());
                    }
                }
            }
        }

        // 缓存结果
        if (!res.empty()) {
            cacheInsert(cache_key, res);
        }

        #ifndef NDEBUG
        printInvertedIndexForQuery(query);
        #endif
        return res;
    }

    // ------------- UTF-8 安全的输出辅助 -------------
    namespace {
        // 返回从 pos 开始的下一个 UTF-8 字符长度（字节数），遇到不合法字节时退化为 1
        inline size_t utf8CharLen(const std::string& s, size_t pos) {
            unsigned char c = static_cast<unsigned char>(s[pos]);
            if ((c & 0x80) == 0)
                return 1; // 0xxxxxxx
            if ((c & 0xE0) == 0xC0) {
                // 110xxxxx
                if (pos + 1 < s.size())
                    return 2;
                else
                    return 1;
            }
            if ((c & 0xF0) == 0xE0) {
                // 1110xxxx
                if (pos + 2 < s.size())
                    return 3;
                else
                    return 1;
            }
            if ((c & 0xF8) == 0xF0) {
                // 11110xxx
                if (pos + 3 < s.size())
                    return 4;
                else
                    return 1;
            }
            return 1;
        }

        // 归一化空白：\r/\n/\t -> 空格，并压缩连续空格
        inline std::string normalizeSpaces(std::string s) {
            for (char& c: s) {
                if (c == '\r' || c == '\n' || c == '\t')
                    c = ' ';
            }
            std::string out;
            out.reserve(s.size());
            bool last_space = false;
            for (char c: s) {
                if (c == ' ') {
                    if (last_space)
                        continue;
                    last_space = true;
                } else {
                    last_space = false;
                }
                out.push_back(c);
            }
            return out;
        }

        // 按“字符数”（非字节）安全截断 UTF-8，并在被截断时追加 ...
        std::string utf8Preview(const std::string& s, const size_t max_chars) {
            size_t pos = 0, chars = 0;
            while (pos < s.size() && chars < max_chars) {
                size_t len = utf8CharLen(s, pos);
                pos += len;
                ++chars;
            }
            if (pos >= s.size())
                return s;
            std::string out = s.substr(0, pos);
            out += "...";
            return out;
        }
    } // anonymous namespace

    void SearchEngine::printSearchResultBodies(std::string_view query) const {
        auto ranked = rankQuery(query);
        if (ranked.empty()) {
            if (getTokenIds(query).empty()) {
                std::cout << ansi::yellow << "  ⚠ No valid tokens found in query." << ansi::reset << std::endl;
            } else {
                std::cout << ansi::yellow << "  ⚠ No documents found matching the query." << ansi::reset << std::endl;
            }
            return;
        }
        const size_t n = ranked.size();
        const double max_score = ranked.front().second;
        const int w = std::min(Console::terminalWidth(), 100);

        // 结果摘要
        std::cout << "\n" << ansi::bold << ansi::bright_green << "  ✓ " << n << " result"
                  << (n > 1 ? "s" : "") << " found" << ansi::reset
                  << ansi::dim << "  (top score: " << std::fixed << std::setprecision(2)
                  << max_score << ")" << ansi::reset << "\n" << std::endl;

        // 顶部边框
        std::cout << ansi::dim << "  " << Console::topBorder(w - 4) << ansi::reset << std::endl;

        const size_t idx_w = std::to_string(n).size();

        for (size_t i = 0; i < n; ++i) {
            DocId doc_id = ranked[i].first;
            double score = ranked[i].second;
            std::string title = env_->getDatabase().getDocumentTitle(doc_id);
            std::string body = env_->getDatabase().getDocumentBody(doc_id);

            // 序号 + 标题
            std::string idx = std::to_string(i + 1);
            std::string pad(idx_w > idx.size() ? idx_w - idx.size() : 0, ' ');

            std::cout << "  " << ansi::dim << "\xe2\x94\x82" << ansi::reset << " "
                      << ansi::bold << ansi::bright_cyan << pad << idx << "." << ansi::reset << " ";

            if (!title.empty()) {
                std::cout << ansi::bold << ansi::bright_white << Console::truncateUtf8(title, 60) << ansi::reset;
            } else {
                std::cout << ansi::dim << "<untitled>" << ansi::reset;
            }
            std::cout << std::endl;

            // 得分行：得分条 + 数值 + doc_id
            std::cout << "  " << ansi::dim << "\xe2\x94\x82" << ansi::reset << "    "
                      << ansi::green << Console::scoreBar(score, max_score, 10) << ansi::reset
                      << " " << ansi::bold << std::fixed << std::setprecision(2) << score << ansi::reset
                      << ansi::dim << "  (doc #" << doc_id << ")" << ansi::reset << std::endl;

            // 正文预览
            const std::string normalized = Console::normalizeSpaces(body);
            const std::string preview = Console::truncateUtf8(normalized, 200);
            std::cout << "  " << ansi::dim << "\xe2\x94\x82" << ansi::reset << "    "
                      << ansi::dim << preview << ansi::reset << std::endl;

            // 分隔线或底部
            if (i + 1 < n) {
                std::cout << "  " << ansi::dim << Console::separator(w - 4) << ansi::reset << std::endl;
            }
        }
        // 底部边框
        std::cout << "  " << ansi::dim << Console::bottomBorder(w - 4) << ansi::reset << std::endl;
        std::cout << std::endl;
    }

    void SearchEngine::printAllDocumentBodies() const {
        const auto docs = env_->getDatabase().getAllDocuments();
        const size_t total = docs.size();
        if (total == 0) {
            std::cout << ansi::yellow << "  ⚠ No documents in index." << ansi::reset << std::endl;
            return;
        }

        const int w = std::min(Console::terminalWidth(), 100);
        const size_t idx_w = std::to_string(total).size();

        std::cout << "\n" << ansi::bold << "  " << total << " document"
                  << (total > 1 ? "s" : "") << " total" << ansi::reset << "\n" << std::endl;

        std::cout << "  " << ansi::dim << Console::topBorder(w - 4) << ansi::reset << std::endl;

        size_t idx = 0;
        for (const auto& [title, body] : docs) {
            ++idx;
            const std::string idx_str = std::to_string(idx);
            const std::string pad(idx_w > idx_str.size() ? idx_w - idx_str.size() : 0, ' ');

            std::cout << "  " << ansi::dim << "\xe2\x94\x82" << ansi::reset << " "
                      << ansi::bold << ansi::bright_cyan << pad << idx_str << "." << ansi::reset << " "
                      << ansi::bold << (title.empty() ? "<untitled>" : title) << ansi::reset << std::endl;

            if (!body.empty()) {
                const std::string preview = Console::truncateUtf8(Console::normalizeSpaces(body), 200);
                std::cout << "  " << ansi::dim << "\xe2\x94\x82" << ansi::reset << "    "
                          << ansi::dim << preview << ansi::reset << std::endl;
            }

            std::cout << "  " << ansi::dim
                      << (idx < total ? Console::separator(w - 4) : Console::bottomBorder(w - 4))
                      << ansi::reset << std::endl;
        }
        std::cout << std::endl;
    }

    std::vector<TokenId> SearchEngine::getTokenIds(std::string_view query) const {
        std::vector<TokenId> token_ids;

        // 转换为UTF-32
        std::string q{ query };
        auto utf32_query = Utils::utf8ToUtf32(q);

        // 分解为N-gram
        size_t pos = 0;
        const std::int32_t n = env_->getTokenLength();

        while (pos < utf32_query.size()) {
            // 跳过忽略字符
            while (pos < utf32_query.size() && Utils::isIgnoredChar(utf32_query[pos])) {
                ++pos;
            }
            if (pos >= utf32_query.size())
                break;

            // 收集最多n个非忽略字符
            size_t start = pos;
            size_t count = 0;
            while (pos < utf32_query.size() && count < static_cast<size_t>(n) && !
                   Utils::isIgnoredChar(utf32_query[pos])) {
                ++pos;
                ++count;
            }

            if (count >= static_cast<size_t>(n)) {
                std::vector<UTF32Char> token_chars(utf32_query.begin() + start,
                                                   utf32_query.begin() + start + n);
                std::string token = Utils::utf32ToUtf8(token_chars);

                // 查询侧统一小写（ASCII）
                for (auto& ch: token) {
                    unsigned char uch = static_cast<unsigned char>(ch);
                    if (uch < 128)
                        ch = static_cast<char>(std::tolower(uch));
                }

                auto info = env_->getDatabase().getTokenInfo(token, false);
                if (info.has_value() && info->id > 0) {
                    token_ids.push_back(info->id);
                }
            }

            // 滑动一个字符
            pos = start + 1;
        }

        return token_ids;
    }

    std::vector<std::pair<TokenId, double>> SearchEngine::getTokenIdsFuzzy(
        std::string_view query, int max_edit_distance) const {
        std::vector<std::pair<TokenId, double>> result;

        std::string q{query};
        auto utf32_query = Utils::utf8ToUtf32(q);

        size_t pos = 0;
        const std::int32_t n = env_->getTokenLength();

        while (pos < utf32_query.size()) {
            while (pos < utf32_query.size() && Utils::isIgnoredChar(utf32_query[pos])) {
                ++pos;
            }
            if (pos >= utf32_query.size()) break;

            size_t start = pos;
            size_t count = 0;
            while (pos < utf32_query.size() && count < static_cast<size_t>(n) &&
                   !Utils::isIgnoredChar(utf32_query[pos])) {
                ++pos; ++count;
            }

            if (count >= static_cast<size_t>(n)) {
                std::vector<UTF32Char> token_chars(utf32_query.begin() + start,
                                                   utf32_query.begin() + start + n);
                std::string token = Utils::utf32ToUtf8(token_chars);
                for (auto& ch : token) {
                    unsigned char uch = static_cast<unsigned char>(ch);
                    if (uch < 128) ch = static_cast<char>(std::tolower(uch));
                }

                // 先尝试精确匹配
                auto info = env_->getDatabase().getTokenInfo(token, false);
                if (info.has_value() && info->id > 0) {
                    result.emplace_back(info->id, 1.0); // 权重 1.0
                } else if (max_edit_distance > 0) {
                    // 模糊匹配回退
                    auto fuzzy = env_->getDatabase().findSimilarTokens(token, max_edit_distance, 1);
                    if (!fuzzy.empty()) {
                        // 距离越大权重越低
                        double weight = 1.0 / (1.0 + fuzzy[0].distance);
                        result.emplace_back(fuzzy[0].id, weight);
                    }
                }
            }
            pos = start + 1;
        }
        return result;
    }

    std::vector<DocId> SearchEngine::intersectPostings(const std::vector<std::vector<DocId>>& postings_lists) {
        if (postings_lists.empty()) {
            return {};
        }
        if (postings_lists.size() == 1) {
            return postings_lists[0];
        }
        auto min_it = std::ranges::min_element(postings_lists,
                                               [](const auto& a, const auto& b) {
                                                   return a.size() < b.size();
                                               });
        std::vector<DocId> result = *min_it;
        for (const auto& postings_list: postings_lists) {
            if (&postings_list == &(*min_it))
                continue;
            std::vector<DocId> intersection;
            std::ranges::set_intersection(result, postings_list,
                                          std::back_inserter(intersection));
            result = std::move(intersection);
            if (result.empty())
                break;
        }
        return result;
    }

    void SearchEngine::displayResults(const std::vector<std::pair<DocId, double>>& results) const {
        spdlog::info("Found {} matching documents:", results.size());
        std::cout << std::string(60, '=') << std::endl;
        const size_t limit = std::min(results.size(), static_cast<size_t>(10));
        for (size_t i = 0; i < limit; ++i) {
            DocId doc_id = results[i].first;
            double score = results[i].second;
            std::string title = env_->getDatabase().getDocumentTitle(doc_id);
            if (!title.empty()) {
                std::cout << (i + 1) << ". Document ID: " << doc_id << ", Title: " << title << ", Score: " << score <<
                        std::endl;
            } else {
                std::cout << (i + 1) << ". Document ID: " << doc_id << ", Score: " << score << std::endl;
            }
        }
        if (results.size() > 10) {
            std::cout << "... and " << (results.size() - 10) << " more documents." << std::endl;
        }

        std::cout << std::string(60, '=') << std::endl;
    }

    // ========== 布尔查询执行 ==========

    std::vector<std::pair<DocId, double>> SearchEngine::rankBooleanQuery(std::string_view query) const {
        using namespace std::chrono;
        const auto t0 = high_resolution_clock::now();

        QueryParser parser;
        auto tree = parser.parse(query);
        if (!tree) {
            spdlog::info("Boolean query parse failed, falling back to standard search");
            return rankQuery(query);
        }

        auto result = executeBooleanTree(tree.get());
        const auto t1 = high_resolution_clock::now();

        if (result.doc_ids.empty()) {
            auto elapsed_us = duration_cast<microseconds>(t1 - t0).count();
            spdlog::info("search_log | query=\"{}\" | mode=boolean | result_count=0 | time_ms={:.3f}",
                         query, static_cast<double>(elapsed_us) / 1000.0);
            return {};
        }

        // 使用收集到的所有 token 信息计算分数
        auto scored = calculateScores(result.doc_ids, result.merged_qd, result.all_token_ids);
        const auto t2 = high_resolution_clock::now();

        auto total_us = duration_cast<microseconds>(t2 - t0).count();
        spdlog::info("search_log | query=\"{}\" | mode=boolean | tokens={} | result_count={} | time_ms={:.3f}",
                     query, result.all_token_ids.size(), scored.size(),
                     static_cast<double>(total_us) / 1000.0);

        return scored;
    }

    SearchEngine::BooleanResult SearchEngine::executeBooleanTree(const QueryNode* node) const {
        if (!node)
            return {};

        switch (node->type) {
            case QueryNodeType::TERM:
                return executeTermNode(node->value);

            case QueryNodeType::PHRASE:
                return executePhraseNode(node->value);

            case QueryNodeType::AND_OP: {
                auto left = executeBooleanTree(node->left.get());
                auto right = executeBooleanTree(node->right.get());

                BooleanResult result;
                // 文档 ID 交集
                std::ranges::set_intersection(left.doc_ids, right.doc_ids,
                                              std::back_inserter(result.doc_ids));
                // 合并 token 信息
                result.all_token_ids = std::move(left.all_token_ids);
                result.all_token_ids.insert(result.all_token_ids.end(),
                                            right.all_token_ids.begin(), right.all_token_ids.end());
                // 合并 QueryData
                auto& mq = result.merged_qd;
                mq.token_postings = std::move(left.merged_qd.token_postings);
                mq.token_postings.insert(mq.token_postings.end(),
                    std::make_move_iterator(right.merged_qd.token_postings.begin()),
                    std::make_move_iterator(right.merged_qd.token_postings.end()));
                mq.docs_counts = std::move(left.merged_qd.docs_counts);
                mq.docs_counts.insert(mq.docs_counts.end(),
                    right.merged_qd.docs_counts.begin(), right.merged_qd.docs_counts.end());
                mq.token_tf_maps = std::move(left.merged_qd.token_tf_maps);
                mq.token_tf_maps.insert(mq.token_tf_maps.end(),
                    std::make_move_iterator(right.merged_qd.token_tf_maps.begin()),
                    std::make_move_iterator(right.merged_qd.token_tf_maps.end()));
                mq.token_pos_maps = std::move(left.merged_qd.token_pos_maps);
                mq.token_pos_maps.insert(mq.token_pos_maps.end(),
                    std::make_move_iterator(right.merged_qd.token_pos_maps.begin()),
                    std::make_move_iterator(right.merged_qd.token_pos_maps.end()));
                return result;
            }

            case QueryNodeType::OR_OP: {
                auto left = executeBooleanTree(node->left.get());
                auto right = executeBooleanTree(node->right.get());

                BooleanResult result;
                result.doc_ids = unionDocIds(left.doc_ids, right.doc_ids);
                // 合并 token 信息
                result.all_token_ids = std::move(left.all_token_ids);
                result.all_token_ids.insert(result.all_token_ids.end(),
                                            right.all_token_ids.begin(), right.all_token_ids.end());
                auto& mq = result.merged_qd;
                mq.token_postings = std::move(left.merged_qd.token_postings);
                mq.token_postings.insert(mq.token_postings.end(),
                    std::make_move_iterator(right.merged_qd.token_postings.begin()),
                    std::make_move_iterator(right.merged_qd.token_postings.end()));
                mq.docs_counts = std::move(left.merged_qd.docs_counts);
                mq.docs_counts.insert(mq.docs_counts.end(),
                    right.merged_qd.docs_counts.begin(), right.merged_qd.docs_counts.end());
                mq.token_tf_maps = std::move(left.merged_qd.token_tf_maps);
                mq.token_tf_maps.insert(mq.token_tf_maps.end(),
                    std::make_move_iterator(right.merged_qd.token_tf_maps.begin()),
                    std::make_move_iterator(right.merged_qd.token_tf_maps.end()));
                mq.token_pos_maps = std::move(left.merged_qd.token_pos_maps);
                mq.token_pos_maps.insert(mq.token_pos_maps.end(),
                    std::make_move_iterator(right.merged_qd.token_pos_maps.begin()),
                    std::make_move_iterator(right.merged_qd.token_pos_maps.end()));
                return result;
            }

            case QueryNodeType::NOT_OP: {
                auto operand = executeBooleanTree(node->operand.get());
                Count total = env_->getDatabase().getDocumentCount();
                // 限制 NOT 操作的最大文档数，防止内存耗尽
                constexpr Count kMaxNotDocs = 10'000'000;
                if (total > kMaxNotDocs) {
                    spdlog::warn("NOT operation skipped: document count {} exceeds limit {}", total, kMaxNotDocs);
                    return {};
                }
                std::vector<DocId> all_docs;
                all_docs.reserve(total);
                for (DocId i = 1; i <= total; ++i) {
                    all_docs.push_back(i);
                }
                BooleanResult result;
                result.doc_ids = differenceDocIds(all_docs, operand.doc_ids);
                return result;
            }
        }
        return {};
    }

    SearchEngine::BooleanResult SearchEngine::executeTermNode(const std::string& term) const {
        BooleanResult result;
        // 将单个搜索词转为 N-gram token IDs
        result.all_token_ids = getTokenIds(term);
        if (result.all_token_ids.empty()) {
            return result;
        }

        // 获取倒排数据
        result.merged_qd = fetchPostings(result.all_token_ids);

        // 求交集得到候选文档
        result.doc_ids = getCandidateDocs(result.merged_qd);
        return result;
    }

    SearchEngine::BooleanResult SearchEngine::executePhraseNode(const std::string& phrase) const {
        BooleanResult result;
        result.all_token_ids = getTokenIds(phrase);
        if (result.all_token_ids.empty()) {
            return result;
        }

        result.merged_qd = fetchPostings(result.all_token_ids);
        auto candidates = getCandidateDocs(result.merged_qd);

        if (result.all_token_ids.size() <= 1) {
            result.doc_ids = std::move(candidates);
            return result;
        }

        // 强制位置相邻过滤（不依赖全局 phrase search 设置）
        // 与 filterByPhrase 相同的逻辑，但总是执行
        result.doc_ids.reserve(candidates.size());
        for (DocId doc_id : candidates) {
            bool ok = true;
            std::vector<Position> current_positions;
            {
                auto it0 = result.merged_qd.token_pos_maps[0].find(doc_id);
                if (it0 == result.merged_qd.token_pos_maps[0].end()) {
                    ok = false;
                } else {
                    current_positions = it0->second;
                }
            }
            for (size_t i = 1; ok && i < result.all_token_ids.size(); ++i) {
                auto iti = result.merged_qd.token_pos_maps[i].find(doc_id);
                if (iti == result.merged_qd.token_pos_maps[i].end()) {
                    ok = false;
                    break;
                }
                const auto& next_pos = iti->second;
                std::vector<Position> advanced;
                advanced.reserve(current_positions.size());
                size_t p = 0, q = 0;
                while (p < current_positions.size() && q < next_pos.size()) {
                    Position need = static_cast<Position>(current_positions[p] + 1);
                    Position got = next_pos[q];
                    if (got == need) { advanced.push_back(need); ++p; ++q; }
                    else if (got < need) { ++q; }
                    else { ++p; }
                }
                if (advanced.empty()) { ok = false; break; }
                current_positions = std::move(advanced);
            }
            if (ok) result.doc_ids.push_back(doc_id);
        }
        return result;
    }

    std::vector<DocId> SearchEngine::unionDocIds(
        const std::vector<DocId>& a, const std::vector<DocId>& b) {
        std::vector<DocId> result;
        result.reserve(a.size() + b.size());
        std::ranges::set_union(a, b, std::back_inserter(result));
        return result;
    }

    std::vector<DocId> SearchEngine::differenceDocIds(
        const std::vector<DocId>& a, const std::vector<DocId>& b) {
        std::vector<DocId> result;
        result.reserve(a.size());
        std::ranges::set_difference(a, b, std::back_inserter(result));
        return result;
    }

} // namespace wiser
