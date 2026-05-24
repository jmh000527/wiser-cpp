/**
 * @file postings.cpp
 * @brief 倒排列表与内存倒排索引实现
 *
 * 结构说明：
 * - PostingsItem：单个文档在某个词元下的命中信息（doc_id + positions）
 * - PostingsList：某个词元对应的倒排列表（多个 PostingsItem）
 * - InvertedIndex：内存中的 token_id -> PostingsList 映射（索引构建阶段使用）
 *
 * 序列化格式 (v2，带格式标记)：
 *   [4 bytes: POSTINGS_MAGIC = 0x57504D32]  "WPM2"
 *   [1 byte:  compress_method (0=NONE, 1=GOLOMB)]
 *   [3 bytes: reserved (0)]
 *   [4 bytes: items_count]
 *   [N bytes: 编码数据]
 *
 * 向后兼容：反序列化时检测 magic 标记。若不匹配，按旧格式（无头部）使用
 * 调用者传入的 compress_method 参数解码。
 */

#include "wiser/postings.h"
#include "wiser/compression_utils.h"
#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>

namespace wiser {

    // 格式标记：用于区分带标记的新格式与旧格式数据
    // 值 0x57504D32 作为 uint32 = 1,465,335,090，远超实际 items_count
    static constexpr uint32_t POSTINGS_MAGIC = 0x57504D32; // "2MPW" in memory (LE)
    static constexpr size_t HEADER_SIZE = 8; // magic(4) + method(1) + reserved(3)

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

        // 创建新项并使用二分插入保持按文档ID排序
        auto new_item = std::make_unique<PostingsItem>(document_id, std::vector<Position>{});
        auto it = std::ranges::lower_bound(items_, document_id,
                                           std::less<>{},
                                           [](const auto& item) { return item->getDocumentId(); });
        auto inserted = items_.insert(it, std::move(new_item));
        return inserted->get();
    }

    std::vector<char> PostingsList::serialize(CompressMethod method) const {
        std::vector<char> result;

        // ── 写入格式头部 ──
        // [4 bytes: magic] [1 byte: compress_method] [3 bytes: reserved]
        uint32_t magic = POSTINGS_MAGIC;
        result.insert(result.end(), reinterpret_cast<const char*>(&magic),
                      reinterpret_cast<const char*>(&magic) + sizeof(magic));
        uint8_t method_byte = static_cast<uint8_t>(method);
        result.push_back(static_cast<char>(method_byte));
        result.push_back(0); // reserved
        result.push_back(0);
        result.push_back(0);

        // ── 写入 items_count ──
        Count items_count = static_cast<Count>(items_.size());
        result.insert(result.end(), reinterpret_cast<const char*>(&items_count),
                      reinterpret_cast<const char*>(&items_count) + sizeof(items_count));

        if (method == CompressMethod::GOLOMB) {
            // Golomb 编码
            BitWriter writer;
            const int M_DOC = 128;
            const int M_POS = 16;

            DocId prev_doc_id = 0;
            for (const auto& item : items_) {
                DocId doc_id = item->getDocumentId();
                DocId delta_doc = doc_id - prev_doc_id;
                GolombEncoder::encode(delta_doc, M_DOC, writer);
                prev_doc_id = doc_id;

                Count positions_count = static_cast<Count>(item->getPositions().size());
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
        } else {
            // NONE: Raw binary
            for (const auto& item : items_) {
                DocId doc_id = item->getDocumentId();
                result.insert(result.end(), reinterpret_cast<const char*>(&doc_id),
                              reinterpret_cast<const char*>(&doc_id) + sizeof(doc_id));

                Count positions_count = static_cast<Count>(item->getPositions().size());
                result.insert(result.end(), reinterpret_cast<const char*>(&positions_count),
                              reinterpret_cast<const char*>(&positions_count) + sizeof(positions_count));

                for (Position position : item->getPositions()) {
                    result.insert(result.end(), reinterpret_cast<const char*>(&position),
                                  reinterpret_cast<const char*>(&position) + sizeof(position));
                }
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

        // ── 格式检测：新格式 vs 旧格式 ──
        CompressMethod actual_method = method; // 默认使用调用者传入的方法（旧格式兜底）
        bool has_header = false;

        if (data.size() >= HEADER_SIZE + sizeof(Count)) {
            uint32_t maybe_magic = *reinterpret_cast<const uint32_t*>(ptr);
            if (maybe_magic == POSTINGS_MAGIC) {
                // 新格式：从头部读取压缩方法
                has_header = true;
                actual_method = static_cast<CompressMethod>(
                    static_cast<uint8_t>(ptr[4]));
                ptr += HEADER_SIZE; // 跳过 8 字节头部
            }
        }

        // 读取 items_count
        if (ptr + sizeof(Count) > end)
            return;
        Count items_count = *reinterpret_cast<const Count*>(ptr);
        ptr += sizeof(Count);

        if (actual_method == CompressMethod::GOLOMB) {
            // Golomb 解码
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

                    Count positions_count = GolombDecoder::decode(8, reader);
                    
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
            }
            return;
        }

        // NONE: Raw binary
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
