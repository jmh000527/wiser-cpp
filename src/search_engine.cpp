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

namespace wiser {
    // 与分词器保持一致的忽略字符判定（UTF-32）
    /**
     * 判断是否为应忽略的分隔标点字符（UTF-32），用于查询侧分词，需与分词器保持一致。
     * ASCII 范围：空白或标点均忽略；非 ASCII：忽略常见全角中文标点及成对引号等。
     * @param ch UTF-32 码点
     * @return true 表示该字符应被跳过
     */
    static inline bool isIgnoredCharQuery(UTF32Char ch) {
        if (ch <= 127) {
            // 仅对 ASCII 使用 <cctype> 分类函数；先转为 unsigned char 以避免未定义行为
            return std::isspace(static_cast<unsigned char>(ch)) || std::ispunct(static_cast<unsigned char>(ch));
        }
        // 非 ASCII：明确列出需忽略的常见全角中文标点与引号
        switch (ch) {
            case 0x3000: // U+3000 IDEOGRAPHIC SPACE（全角空格）
            case 0x3001: // U+3001 IDEOGRAPHIC COMMA（、）
            case 0x3002: // U+3002 IDEOGRAPHIC FULL STOP（。）
            case 0xFF08: // U+FF08 FULLWIDTH LEFT PARENTHESIS（（）
            case 0xFF09: // U+FF09 FULLWIDTH RIGHT PARENTHESIS（））
            case 0xFF01: // U+FF01 FULLWIDTH EXCLAMATION MARK（！）
            case 0xFF0C: // U+FF0C FULLWIDTH COMMA（，）
            case 0xFF1A: // U+FF1A FULLWIDTH COLON（：）
            case 0xFF1B: // U+FF1B FULLWIDTH SEMICOLON（；）
            case 0xFF1F: // U+FF1F FULLWIDTH QUESTION MARK（？）
            case 0xFF3B: // U+FF3B FULLWIDTH LEFT SQUARE BRACKET（【）
            case 0xFF3D: // U+FF3D FULLWIDTH RIGHT SQUARE BRACKET（】）
            case 0x201C: // U+201C LEFT DOUBLE QUOTATION MARK（“）
            case 0x201D: // U+201D RIGHT DOUBLE QUOTATION MARK（”）
            case 0x2018: // U+2018 LEFT SINGLE QUOTATION MARK（‘）
            case 0x2019: // U+2019 RIGHT SINGLE QUOTATION MARK（’）
                return true;
            default:
                return false;
        }
    }

    struct SearchResult {
        DocId document_id;
        double score;

        SearchResult(DocId id, double s)
            : document_id(id), score(s) {}
    };

    SearchEngine::SearchEngine(WiserEnvironment* env)
        : env_(env) {}

    void SearchEngine::printInvertedIndexForQuery(std::string_view query) {
        auto token_ids = getTokenIds(query);
        if (token_ids.empty()) {
            Utils::printInfo("No valid tokens found in query.\n");
            return;
        }

        Utils::printInfo("Inverted index for query tokens:\n");
        for (TokenId token_id: token_ids) {
            std::string token_str = env_->getDatabase().getToken(token_id);

            // 持久化倒排
            auto rec = env_->getDatabase().getPostings(token_id);
            Count disk_docs_cnt = rec ? rec->docs_count : 0;

            // 内存缓存倒排
            auto mem_postings_list = env_->getIndexBuffer().getPostingsList(token_id);
            size_t mem_docs_cnt = (mem_postings_list ? mem_postings_list->getItems().size() : 0);

            if (mem_docs_cnt > 0) {
                std::cout << std::format("  - Token: \"{}\" (id={}), docs(disk)={}, docs(mem)={}",
                                         token_str, token_id, disk_docs_cnt, mem_docs_cnt) << std::endl;
            } else {
                std::cout << std::format("  - Token: \"{}\" (id={}), docs(disk)={}", token_str, token_id,
                                         disk_docs_cnt) << std::endl;
            }

            // 打印持久化倒排
            if (rec && !rec->postings.empty()) {
                PostingsList pl;
                pl.deserialize(rec->postings);
                for (const auto& item: pl.getItems()) {
                    const auto& pos = item->getPositions();
                    std::string pos_line;
                    pos_line.reserve(pos.size() * 4);
                    for (size_t i = 0; i < pos.size(); ++i) {
                        if (i)
                            pos_line += ", ";
                        pos_line += std::to_string(pos[i]);
                    }
                    std::cout << std::format("      [disk] doc {} positions: {}", item->getDocumentId(), pos_line)
                            << std::endl;
                }
            } else {
                std::cout << "      <no postings on disk>" << std::endl;
            }

            // 打印内存缓存倒排
            if (mem_postings_list && !mem_postings_list->getItems().empty()) {
                for (const auto& mem_item: mem_postings_list->getItems()) {
                    const auto& pos = mem_item->getPositions();
                    std::string pos_line;
                    pos_line.reserve(pos.size() * 4);
                    for (size_t i = 0; i < pos.size(); ++i) {
                        if (i)
                            pos_line += ", ";
                        pos_line += std::to_string(pos[i]);
                    }
                    std::cout << std::format("      [mem] doc {} positions: {}", mem_item->getDocumentId(), pos_line)
                            << std::endl;
                }
            } else {
                std::cout << "      <no postings in mem>" << std::endl;
            }
        }
    }

    // 将公共逻辑抽取为可复用的排名函数
    std::vector<std::pair<DocId, double>> SearchEngine::rankQuery(std::string_view query) const {
        // 1) 获取所有查询词元的ID
        std::vector<TokenId> token_ids = getTokenIds(query);
        if (token_ids.empty()) {
            return {};
        }

        // 2) 为每个词元提取倒排与辅助映射
        std::vector<std::vector<DocId>> token_postings;                               // 每个词的doc id列表
        std::vector<Count> docs_counts;                                               // 每个词的文档频次df
        std::vector<std::unordered_map<DocId, Count>> token_tf_maps;                  // 每个词：doc->tf
        std::vector<std::unordered_map<DocId, std::vector<Position>>> token_pos_maps; // 每个词：doc->positions
        token_postings.reserve(token_ids.size());
        docs_counts.reserve(token_ids.size());
        token_tf_maps.reserve(token_ids.size());
        token_pos_maps.reserve(token_ids.size());

        for (TokenId token_id: token_ids) {
            auto rec = env_->getDatabase().getPostings(token_id);
            auto mem_postings_list = env_->getIndexBuffer().getPostingsList(token_id);
            if (rec.has_value()) {
                // 反序列化倒排列表
                PostingsList postings_list;
                postings_list.deserialize(rec->postings);

                // 提取文档ID列表与TF/位置映射（过滤无效ID）
                std::vector<DocId> doc_ids;
                std::unordered_map<DocId, Count> tf_map;
                std::unordered_map<DocId, std::vector<Position>> pos_map;

                // 先处理持久化的倒排
                for (const auto& item: postings_list.getItems()) {
                    DocId did = item->getDocumentId();
                    if (did <= 0) {
                        continue;
                    }
                    const auto& positions = item->getPositions();
                    doc_ids.push_back(did);
                    tf_map[did] = static_cast<Count>(positions.size());
                    pos_map[did] = positions; // 假定为升序
                }

                // 再合并内存缓冲区的倒排（若有）
                if (mem_postings_list) {
                    for (const auto& item: mem_postings_list->getItems()) {
                        DocId did = item->getDocumentId();
                        if (did <= 0) {
                            continue;
                        }
                        const auto& positions = item->getPositions();
                        if (tf_map.find(did) == tf_map.end()) {
                            // 新文档
                            doc_ids.push_back(did);
                            tf_map[did] = static_cast<Count>(positions.size());
                            pos_map[did] = positions; // 假定为升序
                        } else {
                            // 已有文档，合并
                            tf_map[did] += static_cast<Count>(positions.size());
                            auto& existing_positions = pos_map[did];
                            existing_positions.insert(existing_positions.end(), positions.begin(), positions.end());
                            std::ranges::sort(existing_positions); // 保持升序
                        }
                    }
                }
                std::ranges::sort(doc_ids); // 显式排序，保证交集稳定

                token_postings.push_back(std::move(doc_ids));
                docs_counts.push_back(rec->docs_count);
                token_tf_maps.push_back(std::move(tf_map));
                token_pos_maps.push_back(std::move(pos_map));
            } else {
                token_postings.emplace_back();
                docs_counts.push_back(0);
                token_tf_maps.emplace_back();
                token_pos_maps.emplace_back();
            }
        }

        // 3) 求交集，获取候选文档
        std::vector<DocId> candidate_docs = intersectPostings(token_postings);
        candidate_docs.erase(std::remove_if(candidate_docs.begin(), candidate_docs.end(), [](DocId d) {
            return d <= 0;
        }), candidate_docs.end());
        if (candidate_docs.empty()) {
            return {};
        }

        // 4) 若启用短语搜索，做位置相邻校验
        std::vector<DocId> result_docs;
        const bool phrase_enabled = env_->isPhraseSearchEnabled();
        if (phrase_enabled && token_ids.size() > 1) {
            result_docs.reserve(candidate_docs.size());
            for (DocId doc_id: candidate_docs) {
                bool ok = true;
                // 初始为第一个词的位置集合
                std::vector<Position> current_positions; {
                    auto it0 = token_pos_maps[0].find(doc_id);
                    if (it0 == token_pos_maps[0].end()) {
                        ok = false;
                    } else {
                        current_positions = it0->second;
                    }
                }
                // 逐词推进：保留满足 pos_{i+1} = pos_i + 1 的位置链
                for (size_t i = 1; ok && i < token_ids.size(); ++i) {
                    auto iti = token_pos_maps[i].find(doc_id);
                    if (iti == token_pos_maps[i].end()) {
                        ok = false;
                        break;
                    }
                    const auto& next_positions_vec = iti->second; // 升序

                    std::vector<Position> advanced;
                    advanced.reserve(current_positions.size());

                    // 双指针匹配 pos+1
                    size_t p = 0, q = 0;
                    while (p < current_positions.size() && q < next_positions_vec.size()) {
                        Position need = static_cast<Position>(current_positions[p] + 1);
                        Position got = next_positions_vec[q];
                        if (got == need) {
                            advanced.push_back(need);
                            ++p;
                            ++q;
                        } else if (got < need) {
                            ++q;
                        } else {
                            ++p;
                        }
                    }

                    if (advanced.empty()) {
                        ok = false;
                        break;
                    }
                    current_positions.swap(advanced);
                }

                if (ok) {
                    result_docs.push_back(doc_id);
                }
            }

            // if (result_docs.empty()) {
            //     // 短语为空则回退为AND交集
            //     result_docs = candidate_docs;
            // }
        } else {
            result_docs = std::move(candidate_docs);
        }
        if (result_docs.empty()) {
            return {};
        }

        // 5) 计算 TF-IDF 得分
        Count total_docs = env_->getDatabase().getDocumentCount();
        std::vector<double> idfs;
        idfs.reserve(docs_counts.size());
        for (auto df: docs_counts) {
            double idf = std::log((1.0 + static_cast<double>(total_docs)) / (
                                      1.0 + static_cast<double>(std::max<Count>(0, df)))) + 1.0;
            if (!std::isfinite(idf))
                idf = 0.0;
            idfs.push_back(idf);
        }

        std::vector<SearchResult> scored;
        scored.reserve(result_docs.size());
        for (auto doc_id: result_docs) {
            double score = 0.0;
            for (size_t i = 0; i < token_ids.size(); ++i) {
                auto it = token_tf_maps[i].find(doc_id);
                if (it == token_tf_maps[i].end())
                    continue;
                Count raw_tf = std::max<Count>(0, it->second);
                if (raw_tf == 0)
                    continue;
                double tf = 1.0 + std::log(static_cast<double>(raw_tf));
                score += tf * idfs[i];
            }
            scored.emplace_back(doc_id, score);
        }

        std::ranges::sort(scored, [](const SearchResult& a, const SearchResult& b) {
            if (a.score == b.score)
                return a.document_id < b.document_id;
            return a.score > b.score;
        });

        std::vector<std::pair<DocId, double>> display;
        display.reserve(scored.size());
        for (const auto& r: scored)
            display.emplace_back(r.document_id, r.score);
        return display;
    }

    void SearchEngine::search(std::string_view query) {
        auto ranked = rankQuery(query);
        if (ranked.empty()) {
            // 为空时，用一次轻量判断给出更友好的提示
            if (getTokenIds(query).empty()) {
                Utils::printInfo("No valid tokens found in query.\n");
            } else {
                Utils::printInfo("No documents found matching the query.\n");
            }
            return;
        }
        displayResults(ranked);
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
        inline std::string utf8Preview(const std::string& s, size_t max_chars) {
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
                Utils::printInfo("No valid tokens found in query.\n");
            } else {
                Utils::printInfo("No documents found matching the query.\n");
            }
            return;
        } {
            const size_t n = ranked.size();
            Utils::printInfo("Found {} matching documents (bodies):\n", n);
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
    }

    void SearchEngine::printAllDocumentBodies() {
        const auto docs = env_->getDatabase().getAllDocuments();
        const size_t total = docs.size();
        Utils::printInfo("Total documents: {}\n", total);
        if (total == 0) {
            return;
        }

        const size_t width = 60;
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
            while (pos < utf32_query.size() && isIgnoredCharQuery(utf32_query[pos])) {
                ++pos;
            }
            if (pos >= utf32_query.size())
                break;

            // 收集最多n个非忽略字符
            size_t start = pos;
            size_t count = 0;
            while (pos < utf32_query.size() && count < static_cast<size_t>(n) && !
                   isIgnoredCharQuery(utf32_query[pos])) {
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
        auto min_it = std::min_element(postings_lists.begin(), postings_lists.end(),
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
        Utils::printInfo("Found {} matching documents:\n", results.size());
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
