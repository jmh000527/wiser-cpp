#include "wiser/postings.h"
#include <algorithm>
#include <cstring>

namespace wiser {
    // PostingsItem 实现
    PostingsItem::PostingsItem(DocId doc_id, std::vector<Position> positions)
        : document_id_(doc_id), positions_(std::move(positions)) {}

    void PostingsItem::addPosition(Position position) {
        positions_.push_back(position);
    }

    // PostingsList 实现
    void PostingsList::addPosting(DocId document_id, Position position) {
        auto* item = findOrCreateItem(document_id);
        item->addPosition(position);
    }

    void PostingsList::merge(PostingsList&& other) {
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

    std::vector<char> PostingsList::serialize() const {
        std::vector<char> result;

        // 简单的序列化格式（固定32位宽度）：
        // [items_count:Count][doc_id:DocId][positions_count:Count][position:Position]...

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

    void PostingsList::deserialize(const std::vector<char>& data) {
        items_.clear();
        if (data.empty())
            return;

        const char* ptr = data.data();
        const char* end = ptr + data.size();

        if (ptr + sizeof(Count) > end)
            return;
        Count items_count = *reinterpret_cast<const Count*>(ptr);
        ptr += sizeof(Count);

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

    // InvertedIndex 实现
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
