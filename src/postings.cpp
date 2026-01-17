/**
 * @file postings.cpp
 * @brief 倒排列表与内存倒排索引实现
 *
 * 结构说明：
 * - PostingsItem：单个文档在某个词元下的命中信息（doc_id + positions）
 * - PostingsList：某个词元对应的倒排列表（多个 PostingsItem）
 * - InvertedIndex：内存中的 token_id -> PostingsList 映射（索引构建阶段使用）
 *
 * 序列化说明：
 * - PostingsList::serialize 使用固定宽度的简单格式，便于直接落库为 BLOB
 * - 反序列化时做边界检查，避免读取越界
 */

#include "wiser/postings.h"
#include "wiser/compression_utils.h"
#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>

namespace wiser {
    // PostingsItem 实现：记录某个文档在该词元下出现的位置列表
    PostingsItem::PostingsItem(DocId doc_id, std::vector<Position> positions)
        : document_id_(doc_id), positions_(std::move(positions)) {}

    void PostingsItem::addPosition(Position position) {
        // 追加一个位置（由分词器按 token 序号递增提供）
        positions_.push_back(position);
    }

    // PostingsList 实现：维护同一 token_id 对应的所有文档命中
    void PostingsList::addPosting(DocId document_id, Position position) {
        // 找到对应文档的 item（不存在则创建），再追加位置
        auto* item = findOrCreateItem(document_id);
        item->addPosition(position);
    }

    void PostingsList::merge(PostingsList&& other) {
        // 将 other 的所有 (doc_id, positions) 合并到当前列表
        for (auto& item: other.items_) {
            auto existing_item = findOrCreateItem(item->getDocumentId());
            for (Position position: item->getPositions()) {
                existing_item->addPosition(position);
            }
        }
        other.items_.clear();
    }

    PostingsItem* PostingsList::findOrCreateItem(DocId document_id) {
        // 查找现有项
        for (auto& item: items_) {
            if (item->getDocumentId() == document_id) {
                return item.get();
            }
        }

        // 创建新项
        auto new_item = std::make_unique<PostingsItem>(document_id, std::vector<Position>{});
        auto* ptr = new_item.get();
        items_.push_back(std::move(new_item));

        // 保持按文档ID排序
        std::ranges::sort(items_,
                          [](const auto& a, const auto& b) {
                              return a->getDocumentId() < b->getDocumentId();
                          });

        return ptr;
    }

    std::vector<char> PostingsList::serialize(CompressMethod method) const {
        // 使用 BitWriter 写出 Golomb 编码
        if (method == CompressMethod::GOLOMB) {
            std::vector<char> result;
            Count items_count = static_cast<Count>(items_.size());
            result.insert(result.end(), reinterpret_cast<const char*>(&items_count),
                          reinterpret_cast<const char*>(&items_count) + sizeof(items_count));

            // Golomb 写入
            BitWriter writer;
            
            // 使用固定的 M 参数（实际应用中可能需要更复杂的选择策略）
            const int M_DOC = 128; // 用于 DocID delta
            const int M_POS = 16;  // 用于 Position delta

            DocId prev_doc_id = 0;
            for (const auto& item : items_) {
                DocId doc_id = item->getDocumentId();
                DocId delta_doc = doc_id - prev_doc_id;
                GolombEncoder::encode(delta_doc, M_DOC, writer);
                prev_doc_id = doc_id;

                Count positions_count = static_cast<Count>(item->getPositions().size());
                // 这里简单假设 positions_count 不需要 Golomb，因为通常比较小，但统一用也行。
                // 也可以用 Gamma 编码，或者简单地写出来。这里为了统一，用 Golomb，参数选小一点。
                // 或者直接用 raw binary 或者 varint。为了简化，我们仅对 delta 序列使用 Golomb。
                // positions_count 暂时用 Golomb 存, M=2
                GolombEncoder::encode(positions_count, 8, writer);

                Position prev_pos = 0;
                for (Position pos : item->getPositions()) {
                    Position delta_pos = pos - prev_pos;
                     GolombEncoder::encode(delta_pos, M_POS, writer);
                    prev_pos = pos;
                }
            }

            auto bits_data = writer.getData();
            result.insert(result.end(), bits_data.begin(), bits_data.end());
            return result;
        }

        // 默认: NONE (Raw binary)
        std::vector<char> result;

        // 简单序列化格式（固定宽度）：
        // [items_count:Count]
        //   循环 items_count 次：
        //   [doc_id:DocId][positions_count:Count][position:Position] * positions_count

        Count items_count = static_cast<Count>(items_.size());
        result.insert(result.end(), reinterpret_cast<const char*>(&items_count),
                      reinterpret_cast<const char*>(&items_count) + sizeof(items_count));

        for (const auto& item: items_) {
            DocId doc_id = item->getDocumentId();
            result.insert(result.end(), reinterpret_cast<const char*>(&doc_id),
                          reinterpret_cast<const char*>(&doc_id) + sizeof(doc_id));

            Count positions_count = static_cast<Count>(item->getPositions().size());
            result.insert(result.end(), reinterpret_cast<const char*>(&positions_count),
                          reinterpret_cast<const char*>(&positions_count) + sizeof(positions_count));

            for (Position position: item->getPositions()) {
                result.insert(result.end(), reinterpret_cast<const char*>(&position),
                              reinterpret_cast<const char*>(&position) + sizeof(position));
            }
        }

        return result;
    }

    void PostingsList::deserialize(const std::vector<char>& data, CompressMethod method) {
        items_.clear();
        if (data.empty())
            return;

        const char* ptr = data.data();
        const char* end = ptr + data.size();

        // 读取 items_count（做边界检查，避免越界）
        if (ptr + sizeof(Count) > end)
            return;
        Count items_count = *reinterpret_cast<const Count*>(ptr);
        ptr += sizeof(Count);

        if (method == CompressMethod::GOLOMB) {
            // Golomb 解码
            // 将剩下的数据构造为 BitReader
            // 注意：BitReader 接受 const std::vector<char>&，我们需要构造这样一个 vector 或修改 BitReader 适配指针
            // 为了简单，尽量复用现有 vector，或者如果 BitReader 只能用 vector，我们得拷贝剩下的数据
            // BitReader 定义是 const std::vector<char>& data_，所以我们需要一个新的 vector
            std::vector<char> bit_data(ptr, end);
            BitReader reader(bit_data);

            const int M_DOC = 128;
            const int M_POS = 16;
            
            DocId prev_doc_id = 0;

            try {
                for (Count i = 0; i < items_count; ++i) {
                    if (reader.eof()) break;

                    DocId delta_doc = GolombDecoder::decode(M_DOC, reader);
                    DocId doc_id = prev_doc_id + delta_doc;
                    prev_doc_id = doc_id;

                    Count positions_count = GolombDecoder::decode(8, reader); // M=8 for count
                    
                    std::vector<Position> positions;
                    positions.reserve(positions_count);

                    Position prev_pos = 0;
                    for (Count j = 0; j < positions_count; ++j) {
                        Position delta_pos = GolombDecoder::decode(M_POS, reader);
                        Position pos = prev_pos + delta_pos;
                        positions.push_back(pos);
                        prev_pos = pos;
                    }

                    items_.push_back(std::make_unique<PostingsItem>(doc_id, std::move(positions)));
                }
            } catch (const std::exception& e) {
                spdlog::error("Error decoding Golomb stream: {}", e.what());
                // 出错时保留已解码的部分
            }
            return;
        }

        // 默认: NONE (Raw binary)
        for (Count i = 0; i < items_count && ptr < end; ++i) {
            if (ptr + sizeof(DocId) > end)
                break;
            DocId doc_id = *reinterpret_cast<const DocId*>(ptr);
            ptr += sizeof(DocId);

            if (ptr + sizeof(Count) > end)
                break;
            Count positions_count = *reinterpret_cast<const Count*>(ptr);
            ptr += sizeof(Count);

            std::vector<Position> positions;
            positions.reserve(positions_count);

            for (Count j = 0; j < positions_count && ptr + sizeof(Position) <= end; ++j) {
                Position position = *reinterpret_cast<const Position*>(ptr);
                ptr += sizeof(Position);
                positions.push_back(position);
            }

            items_.push_back(std::make_unique<PostingsItem>(doc_id, std::move(positions)));
        }
    }

    // InvertedIndex 实现：token_id -> PostingsList 的映射
    void InvertedIndex::addPosting(TokenId token_id, DocId document_id, Position position) {
        auto it = index_.find(token_id);
        if (it == index_.end()) {
            auto new_list = std::make_unique<PostingsList>();
            new_list->addPosting(document_id, position);
            index_[token_id] = std::move(new_list);
        } else {
            it->second->addPosting(document_id, position);
        }
    }

    PostingsList* InvertedIndex::getPostingsList(TokenId token_id) {
        auto it = index_.find(token_id);
        return (it != index_.end()) ? it->second.get() : nullptr;
    }

    const PostingsList* InvertedIndex::getPostingsList(TokenId token_id) const {
        auto it = index_.find(token_id);
        return (it != index_.end()) ? it->second.get() : nullptr;
    }

    void InvertedIndex::clear() {
        index_.clear();
    }
} // namespace wiser
