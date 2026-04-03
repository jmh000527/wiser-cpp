/**
 * @file test_database.cpp
 * @brief Database 类的单元测试（使用内存数据库 :memory:）。
 */

#include <gtest/gtest.h>
#include "wiser/database.h"

using namespace wiser;

class DatabaseTest : public ::testing::Test {
protected:
    Database db;

    void SetUp() override {
        ASSERT_TRUE(db.initialize(":memory:"));
    }

    void TearDown() override {
        db.close();
    }
};

// ============================================================
// Document CRUD
// ============================================================

TEST_F(DatabaseTest, AddAndGetDocument) {
    ASSERT_TRUE(db.addDocument("Test Title", "Test Body", 5));
    DocId id = db.getDocumentId("Test Title");
    EXPECT_GT(id, 0);
    EXPECT_EQ(db.getDocumentTitle(id), "Test Title");
    EXPECT_EQ(db.getDocumentBody(id), "Test Body");
}

TEST_F(DatabaseTest, GetDocumentId_NonExistent) {
    DocId id = db.getDocumentId("NonExistent");
    EXPECT_EQ(id, 0);
}

TEST_F(DatabaseTest, GetDocumentTitle_NonExistent) {
    EXPECT_EQ(db.getDocumentTitle(99999), "");
}

TEST_F(DatabaseTest, AddDocument_UpdateExisting) {
    ASSERT_TRUE(db.addDocument("Title", "Body1", 3));
    DocId id1 = db.getDocumentId("Title");

    ASSERT_TRUE(db.addDocument("Title", "Body2", 5));
    DocId id2 = db.getDocumentId("Title");

    EXPECT_EQ(id1, id2); // Same doc, just updated
    EXPECT_EQ(db.getDocumentBody(id2), "Body2");
}

TEST_F(DatabaseTest, GetDocumentCount) {
    EXPECT_EQ(db.getDocumentCount(), 0);
    db.addDocument("A", "body", 1);
    EXPECT_EQ(db.getDocumentCount(), 1);
    db.addDocument("B", "body", 1);
    EXPECT_EQ(db.getDocumentCount(), 2);
}

// ============================================================
// Token management
// ============================================================

TEST_F(DatabaseTest, GetTokenInfo_Insert) {
    auto info = db.getTokenInfo("hello", true);
    ASSERT_TRUE(info.has_value());
    EXPECT_GT(info->id, 0);
    EXPECT_EQ(info->docs_count, 0);
}

TEST_F(DatabaseTest, GetTokenInfo_NoInsert) {
    auto info = db.getTokenInfo("nonexistent", false);
    EXPECT_FALSE(info.has_value());
}

TEST_F(DatabaseTest, GetTokenInfo_Idempotent) {
    auto info1 = db.getTokenInfo("token1", true);
    auto info2 = db.getTokenInfo("token1", true);
    ASSERT_TRUE(info1.has_value());
    ASSERT_TRUE(info2.has_value());
    EXPECT_EQ(info1->id, info2->id);
}

TEST_F(DatabaseTest, GetToken) {
    auto info = db.getTokenInfo("mytoken", true);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(db.getToken(info->id), "mytoken");
}

TEST_F(DatabaseTest, GetToken_NonExistent) {
    EXPECT_EQ(db.getToken(99999), "");
}

// ============================================================
// Postings
// ============================================================

TEST_F(DatabaseTest, UpdateAndGetPostings) {
    auto info = db.getTokenInfo("test", true);
    ASSERT_TRUE(info.has_value());

    std::vector<char> data = {0x01, 0x02, 0x03, 0x04};
    ASSERT_TRUE(db.updatePostings(info->id, 2, data));

    auto rec = db.getPostings(info->id);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->docs_count, 2);
    EXPECT_EQ(rec->postings, data);
}

TEST_F(DatabaseTest, GetPostings_NonExistent) {
    auto rec = db.getPostings(99999);
    EXPECT_FALSE(rec.has_value());
}

// ============================================================
// Token count
// ============================================================

TEST_F(DatabaseTest, DocumentTokenCount) {
    db.addDocument("Doc1", "body", 10);
    DocId id = db.getDocumentId("Doc1");
    EXPECT_EQ(db.getDocumentTokenCount(id), 10);

    db.updateDocumentTokenCount(id, 20);
    EXPECT_EQ(db.getDocumentTokenCount(id), 20);
}

TEST_F(DatabaseTest, TotalTokenCount) {
    db.addDocument("A", "body", 10);
    db.addDocument("B", "body", 20);
    EXPECT_EQ(db.getTotalTokenCount(), 30);
}

TEST_F(DatabaseTest, GetAllDocumentTokenCounts) {
    db.addDocument("A", "body", 10);
    db.addDocument("B", "body", 20);

    auto counts = db.getAllDocumentTokenCounts();
    ASSERT_EQ(counts.size(), 2u);

    int total = 0;
    for (auto& [id, count] : counts) {
        total += count;
    }
    EXPECT_EQ(total, 30);
}

// ============================================================
// Settings
// ============================================================

TEST_F(DatabaseTest, SetAndGetSetting) {
    ASSERT_TRUE(db.setSetting("key1", "value1"));
    EXPECT_EQ(db.getSetting("key1"), "value1");
}

TEST_F(DatabaseTest, GetSetting_NonExistent) {
    EXPECT_EQ(db.getSetting("nonexistent"), "");
}

TEST_F(DatabaseTest, SetSetting_Update) {
    db.setSetting("key", "old");
    db.setSetting("key", "new");
    EXPECT_EQ(db.getSetting("key"), "new");
}

// ============================================================
// Transactions
// ============================================================

TEST_F(DatabaseTest, Transaction_CommitPersists) {
    ASSERT_TRUE(db.beginTransaction());
    db.addDocument("TxDoc", "body", 5);
    ASSERT_TRUE(db.commitTransaction());

    EXPECT_GT(db.getDocumentId("TxDoc"), 0);
}

TEST_F(DatabaseTest, Transaction_RollbackReverts) {
    ASSERT_TRUE(db.beginTransaction());
    db.addDocument("RollbackDoc", "body", 5);
    db.rollbackTransaction();

    EXPECT_EQ(db.getDocumentId("RollbackDoc"), 0);
}

// ============================================================
// LIKE search
// ============================================================

TEST_F(DatabaseTest, SearchDocumentsLike) {
    db.addDocument("Alpha", "content about alpha testing", 5);
    db.addDocument("Beta", "content about beta testing", 5);
    db.addDocument("Gamma", "different content entirely", 5);

    auto results = db.searchDocumentsLike("testing");
    EXPECT_EQ(results.size(), 2u);

    auto results2 = db.searchDocumentsLike("Alpha");
    EXPECT_EQ(results2.size(), 1u);
}

// ============================================================
// GetAllDocuments
// ============================================================

TEST_F(DatabaseTest, GetAllDocuments) {
    db.addDocument("A", "body_a", 1);
    db.addDocument("B", "body_b", 2);
    db.addDocument("C", "body_c", 3);

    auto docs = db.getAllDocuments();
    ASSERT_EQ(docs.size(), 3u);
}
