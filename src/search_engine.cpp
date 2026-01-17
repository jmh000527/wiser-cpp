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
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <ranges>
#include <format>
#include <spdlog/spdlog.h>
#include <chrono>
#include "wiser/config.h" // Add header

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

    /**
     * @brief SearchEngine 构造函数
     * 
     * 初始化搜索引擎，关联到指定的Wiser环境
     * 
     * @param env Wiser环境指针
     */
    SearchEngine::SearchEngine(WiserEnvironment* env)
        : env_(env) {}

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
                qd.token_postings.push_back(std::move(doc_ids));
                qd.docs_counts.push_back(rec->docs_count);
                qd.token_tf_maps.push_back(std::move(tf_map));
                qd.token_pos_maps.push_back(std::move(pos_map));
            } else {
                // 数据库中没有该token的记录，添加空数据
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
        if (phrase_enabled && token_ids.size() > 1) {
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
        double avgdl = total_docs > 0 ? static_cast<double>(total_tokens) / static_cast<double>(total_docs) : 0.0;  // 平均文档长度

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
            // Fallback: LIKE 子串查询（当查询短于 N 或被全部忽略时）
            auto like_ids = env_->getDatabase().searchDocumentsLike(std::string(query));
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

    std::vector<std::pair<DocId, double>> SearchEngine::searchWithResults(std::string_view query) const {
        auto res = rankQuery(query);
        #ifndef NDEBUG
        // 调试打印倒排结构（仅 Debug 构建执行）
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
            if (out.size() > 3)
                out.resize(out.size()); // 保持边界，后续附加 ...
            out += "...";
            return out;
        }
    } // anonymous namespace

    void SearchEngine::printSearchResultBodies(std::string_view query) const {
        auto ranked = rankQuery(query);
        if (ranked.empty()) {
            if (getTokenIds(query).empty()) {
                spdlog::info("No valid tokens found in query.");
            } else {
                spdlog::info("No documents found matching the query.");
            }
            return;
        }
        const size_t n = ranked.size();
        spdlog::info("Found {} matching documents (bodies):", n);
        std::cout << std::string(60, '=') << std::endl;

        const size_t idx_w = std::to_string(n).size();

        for (size_t i = 0; i < n; ++i) {
            DocId doc_id = ranked[i].first;
            double score = ranked[i].second;
            std::string title = env_->getDatabase().getDocumentTitle(doc_id);
            std::string body = env_->getDatabase().getDocumentBody(doc_id);

            std::string idx = std::to_string(i + 1);
            std::string pad(idx_w > idx.size() ? idx_w - idx.size() : 0, ' ');
            std::cout << pad << idx << ") Document ID: " << doc_id;
            if (!title.empty()) {
                std::cout << "  |  Title: " << title;
            }
            std::cout << "  |  Score: " << score << std::endl;

            // UTF-8 安全预览
            const std::string normalized = normalizeSpaces(body);
            std::cout << "Body: " << utf8Preview(normalized, 240) << std::endl;

            if (i + 1 < n) {
                std::cout << std::string(60, '-') << std::endl;
            }
        }
        std::cout << std::string(60, '=') << std::endl;
    }

    void SearchEngine::printAllDocumentBodies() const {
        const auto docs = env_->getDatabase().getAllDocuments();
        const size_t total = docs.size();
        spdlog::info("Total documents: {}", total);
        if (total == 0) {
            return;
        }

        constexpr size_t width = 60;
        const std::string top = std::string(width, '=');
        const std::string sep = std::string(width, '-');
        const size_t idx_w = std::to_string(total).size();

        std::cout << top << std::endl;
        size_t idx = 0;
        for (const auto& [title, body]: docs) {
            ++idx;
            const std::string idx_str = std::to_string(idx);
            const std::string pad(idx_w > idx_str.size() ? idx_w - idx_str.size() : 0, ' ');
            std::cout << pad << idx_str << ") Title: " << (title.empty() ? "<untitled>" : title) << std::endl;

            if (!body.empty()) {
                const std::string normalized = normalizeSpaces(body);
                const std::string preview = utf8Preview(normalized, 240);
                std::cout << "Body:" << std::endl;
                std::cout << "  " << preview << std::endl;
            } else {
                std::cout << "Body: <empty>" << std::endl;
            }

            std::cout << ((idx < total) ? sep : top) << std::endl;
        }
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
} // namespace wiser
