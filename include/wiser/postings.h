#pragma once

/**
 * @file postings.h
 * @brief 倒排项/倒排列表/倒排索引的数据结构与序列化接口。
 */

#include "types.h"
#include <vector>
#include <memory>
#include <unordered_map>

namespace wiser {
    /**
     * @brief 倒排列表项
     * 
     * 代表一个文档中的词元出现位置集合。
     */
    class PostingsItem {
    public:
        /**
         * @brief 构造倒排列表项
         * @param doc_id 文档 ID
         * @param positions 词元位置信息数组
         */
        PostingsItem(DocId doc_id, std::vector<Position> positions);

        /** 
         * @brief 获取文档 ID
         * @return 文档 ID
         */
        DocId getDocumentId() const { return document_id_; }

        /** 
         * @brief 获取位置信息数组
         * @return 位置信息常引用
         */
        const std::vector<Position>& getPositions() const { return positions_; }

        /** 
         * @brief 获取位置个数
         * @return 位置数量
         */
        Count getPositionsCount() const { return static_cast<Count>(positions_.size()); }

        /**
         * @brief 添加一个出现位置
         * @param position 在文档中的位置
         */
        void addPosition(Position position);

    private:
        DocId document_id_;               ///< 文档编号
        std::vector<Position> positions_; ///< 位置信息数组
    };

    /**
     * @brief 倒排列表
     * 
     * 包含某个词元的所有文档及位置信息。
     */
    class PostingsList {
    public:
        PostingsList() = default;
        ~PostingsList() = default;

        // 不可复制，可移动
        PostingsList(const PostingsList&) = delete;
        PostingsList& operator=(const PostingsList&) = delete;
        PostingsList(PostingsList&&) = default;
        PostingsList& operator=(PostingsList&&) = default;

        /**
         * @brief 向倒排列表添加一条记录
         * @param document_id 文档 ID
         * @param position 词元在文档中的位置
         */
        void addPosting(DocId document_id, Position position);

        /**
         * @brief 合并另一个倒排列表（同一词元）
         * 
         * 对同一 doc_id 的位置向量进行拼接，必要时保持升序。
         * @param other 另一个倒排列表（将被移动）
         */
        void merge(PostingsList&& other);

        /** 
         * @brief 获取内部项的只读数组
         * @return 倒排列表项的智能指针数组
         */
        const std::vector<std::unique_ptr<PostingsItem>>& getItems() const { return items_; }

        /** 
         * @brief 获取涉及的文档数量
         * @return 文档数量
         */
        Count getDocumentsCount() const { return static_cast<Count>(items_.size()); }

        /**
         * @brief 序列化倒排列表
         * @param method 压缩方法 (默认: NONE)
         * @return 序列化后的字节数组
         */
        std::vector<char> serialize(CompressMethod method = CompressMethod::NONE) const;

        /**
         * @brief 从字节数组反序列化
         * @param data 序列化数据
         * @param method 压缩方法 (默认: NONE)
         */
        void deserialize(const std::vector<char>& data, CompressMethod method = CompressMethod::NONE);

    private:
        std::vector<std::unique_ptr<PostingsItem>> items_;

        /**
         * @brief 查找或创建指定文档 ID 的项
         * @param document_id 文档 ID
         * @return 指向该项的指针
         */
        PostingsItem* findOrCreateItem(DocId document_id);
    };

    /**
     * @brief 倒排索引
     * 
     * 管理所有词元的倒排列表。
     */
    class InvertedIndex {
    public:
        InvertedIndex() = default;
        ~InvertedIndex() = default;

        // 不可复制，可移动
        InvertedIndex(const InvertedIndex&) = delete;
        InvertedIndex& operator=(const InvertedIndex&) = delete;
        InvertedIndex(InvertedIndex&&) = default;
        InvertedIndex& operator=(InvertedIndex&&) = default;

        /**
         * @brief 添加一条记录到某个词元的倒排列表
         * @param token_id 词元 ID
         * @param document_id 文档 ID
         * @param position 位置
         */
        void addPosting(TokenId token_id, DocId document_id, Position position);

        /**
         * @brief 获取（可写）倒排列表
         * @param token_id 词元 ID
         * @return 指向倒排列表的指针，若不存在则可能为 nullptr（取决于实现，通常需要先创建）
         */
        PostingsList* getPostingsList(TokenId token_id);

        /**
         * @brief 获取（只读）倒排列表
         * @param token_id 词元 ID
         * @return 指向倒排列表的常量指针，若不存在则可能为 nullptr
         */
        const PostingsList* getPostingsList(TokenId token_id) const;

        /** 
         * @brief 清空所有数据 
         */
        void clear();

        /** 
         * @brief 获取词元数量
         * @return 索引中的词元总数
         */
        size_t size() const { return index_.size(); }

        // 迭代器支持
        auto begin() { return index_.begin(); }
        auto end() { return index_.end(); }
        auto begin() const { return index_.begin(); }
        auto end() const { return index_.end(); }

    private:
        std::unordered_map<TokenId, std::unique_ptr<PostingsList>> index_;
    };
} // namespace wiser
