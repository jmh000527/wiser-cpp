/**
 * @file test_postings.cpp
 * @brief PostingsItem / PostingsList / InvertedIndex 的单元测试。
 */

#include <gtest/gtest.h>
#include "wiser/postings.h"

using namespace wiser;

// ============================================================
// PostingsItem
// ============================================================

TEST(PostingsItemTest, Construction) {
    std::vector<Position> pos = {1, 5, 10};
    PostingsItem item(42, pos);
    EXPECT_EQ(item.getDocumentId(), 42);
    EXPECT_EQ(item.getPositionsCount(), 3);
    EXPECT_EQ(item.getPositions()[0], 1);
    EXPECT_EQ(item.getPositions()[2], 10);
}

TEST(PostingsItemTest, AddPosition) {
    PostingsItem item(1, {});
    EXPECT_EQ(item.getPositionsCount(), 0);
    item.addPosition(5);
    item.addPosition(10);
    EXPECT_EQ(item.getPositionsCount(), 2);
    EXPECT_EQ(item.getPositions()[0], 5);
    EXPECT_EQ(item.getPositions()[1], 10);
}

// ============================================================
// PostingsList
// ============================================================

TEST(PostingsListTest, AddPosting_SingleDoc) {
    PostingsList pl;
    pl.addPosting(1, 0);
    pl.addPosting(1, 5);
    pl.addPosting(1, 10);

    ASSERT_EQ(pl.getDocumentsCount(), 1);
    EXPECT_EQ(pl.getItems()[0]->getDocumentId(), 1);
    EXPECT_EQ(pl.getItems()[0]->getPositionsCount(), 3);
}

TEST(PostingsListTest, AddPosting_MultipleDocs) {
    PostingsList pl;
    pl.addPosting(3, 0);
    pl.addPosting(1, 0);
    pl.addPosting(2, 0);

    ASSERT_EQ(pl.getDocumentsCount(), 3);
    // Should be sorted by doc_id after binary insertion
    EXPECT_EQ(pl.getItems()[0]->getDocumentId(), 1);
    EXPECT_EQ(pl.getItems()[1]->getDocumentId(), 2);
    EXPECT_EQ(pl.getItems()[2]->getDocumentId(), 3);
}

TEST(PostingsListTest, AddPosting_SameDocMultipleTimes) {
    PostingsList pl;
    pl.addPosting(1, 0);
    pl.addPosting(1, 1);
    pl.addPosting(1, 2);

    ASSERT_EQ(pl.getDocumentsCount(), 1);
    EXPECT_EQ(pl.getItems()[0]->getPositionsCount(), 3);
}

TEST(PostingsListTest, Serialize_Deserialize_None) {
    PostingsList original;
    original.addPosting(1, 0);
    original.addPosting(1, 5);
    original.addPosting(2, 3);
    original.addPosting(2, 7);
    original.addPosting(3, 1);

    auto data = original.serialize(CompressMethod::NONE);
    EXPECT_GT(data.size(), 0u);

    PostingsList restored;
    restored.deserialize(data, CompressMethod::NONE);

    ASSERT_EQ(restored.getDocumentsCount(), 3);
    EXPECT_EQ(restored.getItems()[0]->getDocumentId(), 1);
    EXPECT_EQ(restored.getItems()[0]->getPositionsCount(), 2);
    EXPECT_EQ(restored.getItems()[1]->getDocumentId(), 2);
    EXPECT_EQ(restored.getItems()[1]->getPositionsCount(), 2);
    EXPECT_EQ(restored.getItems()[2]->getDocumentId(), 3);
    EXPECT_EQ(restored.getItems()[2]->getPositionsCount(), 1);
}

TEST(PostingsListTest, Serialize_Deserialize_Golomb) {
    PostingsList original;
    original.addPosting(1, 0);
    original.addPosting(1, 5);
    original.addPosting(5, 3);
    original.addPosting(10, 1);

    auto data = original.serialize(CompressMethod::GOLOMB);
    EXPECT_GT(data.size(), 0u);

    PostingsList restored;
    restored.deserialize(data, CompressMethod::GOLOMB);

    ASSERT_EQ(restored.getDocumentsCount(), 3);
    EXPECT_EQ(restored.getItems()[0]->getDocumentId(), 1);
    EXPECT_EQ(restored.getItems()[0]->getPositionsCount(), 2);
    EXPECT_EQ(restored.getItems()[1]->getDocumentId(), 5);
    EXPECT_EQ(restored.getItems()[2]->getDocumentId(), 10);
}

TEST(PostingsListTest, Serialize_Deserialize_Empty) {
    PostingsList empty;
    auto data = empty.serialize(CompressMethod::NONE);

    PostingsList restored;
    restored.deserialize(data, CompressMethod::NONE);
    EXPECT_EQ(restored.getDocumentsCount(), 0);
}

TEST(PostingsListTest, Merge) {
    PostingsList a;
    a.addPosting(1, 0);
    a.addPosting(1, 1);

    PostingsList b;
    b.addPosting(1, 2);
    b.addPosting(2, 0);

    a.merge(std::move(b));

    ASSERT_EQ(a.getDocumentsCount(), 2);
    // doc 1 should have merged positions
    EXPECT_EQ(a.getItems()[0]->getDocumentId(), 1);
    EXPECT_GE(a.getItems()[0]->getPositionsCount(), 3);
    // doc 2
    EXPECT_EQ(a.getItems()[1]->getDocumentId(), 2);
    EXPECT_EQ(a.getItems()[1]->getPositionsCount(), 1);
}

// ============================================================
// InvertedIndex
// ============================================================

TEST(InvertedIndexTest, AddAndRetrieve) {
    InvertedIndex idx;
    idx.addPosting(100, 1, 0);
    idx.addPosting(100, 1, 5);
    idx.addPosting(100, 2, 3);
    idx.addPosting(200, 1, 1);

    auto* pl100 = idx.getPostingsList(100);
    ASSERT_NE(pl100, nullptr);
    EXPECT_EQ(pl100->getDocumentsCount(), 2);

    auto* pl200 = idx.getPostingsList(200);
    ASSERT_NE(pl200, nullptr);
    EXPECT_EQ(pl200->getDocumentsCount(), 1);
}

TEST(InvertedIndexTest, GetNonExistent) {
    InvertedIndex idx;
    EXPECT_EQ(idx.getPostingsList(999), nullptr);
}

TEST(InvertedIndexTest, Clear) {
    InvertedIndex idx;
    idx.addPosting(1, 1, 0);
    idx.addPosting(2, 1, 0);
    EXPECT_EQ(idx.size(), 2u);

    idx.clear();
    EXPECT_EQ(idx.size(), 0u);
    EXPECT_EQ(idx.getPostingsList(1), nullptr);
}

TEST(InvertedIndexTest, SizeTracking) {
    InvertedIndex idx;
    EXPECT_EQ(idx.size(), 0u);

    idx.addPosting(1, 1, 0);
    EXPECT_EQ(idx.size(), 1u);

    idx.addPosting(2, 1, 0);
    EXPECT_EQ(idx.size(), 2u);

    // Adding to existing token shouldn't increase size
    idx.addPosting(1, 2, 0);
    EXPECT_EQ(idx.size(), 2u);
}
