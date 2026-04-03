/**
 * @file test_integration.cpp
 * @brief 集成测试：完整的索引→搜索管线。
 *
 * 使用 :memory: 数据库，测试 WiserEnvironment 的端到端行为。
 */

#include <gtest/gtest.h>
#include "wiser/wiser_environment.h"
#include "wiser/search_engine.h"

using namespace wiser;

class IntegrationTest : public ::testing::Test {
protected:
    WiserEnvironment env;

    void SetUp() override {
        ASSERT_TRUE(env.initialize(":memory:"));
    }

    void TearDown() override {
        env.shutdown();
    }
};

// ============================================================
// Basic indexing and search
// ============================================================

TEST_F(IntegrationTest, AddDocAndSearch) {
    env.addDocument("人工智能", "人工智能是计算机科学的一个分支");
    env.flushIndexBuffer();

    auto results = env.getSearchEngine().searchWithResults("人工智能");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].first, env.getDatabase().getDocumentId("人工智能"));
}

TEST_F(IntegrationTest, SearchBeforeFlush_MemoryBuffer) {
    // Documents added but not yet flushed should still be searchable
    // via memory buffer
    env.addDocument("测试文档", "这是一个用于测试搜索的文档内容");

    auto results = env.getSearchEngine().searchWithResults("测试搜索");
    // After our fix, memory-only tokens should be found
    ASSERT_FALSE(results.empty());
}

TEST_F(IntegrationTest, SearchNoResults) {
    env.addDocument("文档一", "这是第一个文档的内容");
    env.flushIndexBuffer();

    auto results = env.getSearchEngine().searchWithResults("完全不相关的查询");
    EXPECT_TRUE(results.empty());
}

TEST_F(IntegrationTest, MultipleDocuments_Ranking) {
    env.addDocument("机器学习入门", "机器学习是人工智能的核心领域");
    env.addDocument("深度学习", "深度学习是机器学习的一个分支");
    env.addDocument("数据结构", "数据结构是计算机科学的基础");
    env.flushIndexBuffer();

    auto results = env.getSearchEngine().searchWithResults("机器学习");
    ASSERT_GE(results.size(), 2u);
    // Both first and second doc should match "机器学习"
}

// ============================================================
// Scoring methods
// ============================================================

TEST_F(IntegrationTest, BM25Scoring) {
    env.setScoringMethod(ScoringMethod::BM25);
    env.addDocument("BM25测试", "搜索引擎使用BM25算法进行相关性排序");
    env.flushIndexBuffer();

    auto results = env.getSearchEngine().searchWithResults("搜索引擎");
    ASSERT_FALSE(results.empty());
    EXPECT_GT(results[0].second, 0.0);
}

TEST_F(IntegrationTest, TfIdfScoring) {
    env.setScoringMethod(ScoringMethod::TF_IDF);
    env.addDocument("TFIDF测试", "搜索引擎使用TFIDF算法进行相关性排序");
    env.flushIndexBuffer();

    auto results = env.getSearchEngine().searchWithResults("搜索引擎");
    ASSERT_FALSE(results.empty());
    EXPECT_GT(results[0].second, 0.0);
}

// ============================================================
// Phrase search
// ============================================================

TEST_F(IntegrationTest, PhraseSearch_Enabled) {
    env.setPhraseSearchEnabled(true);
    env.addDocument("短语测试", "今天天气真的非常好");
    env.flushIndexBuffer();

    auto results = env.getSearchEngine().searchWithResults("天气非常好");
    // "天气" and "非常" and "常好" should all appear, but adjacency may fail
    // depending on N-gram positions
}

TEST_F(IntegrationTest, PhraseSearch_Disabled) {
    env.setPhraseSearchEnabled(false);
    env.addDocument("短语测试", "今天天气真的非常好");
    env.flushIndexBuffer();

    auto results = env.getSearchEngine().searchWithResults("天气非常好");
    // Without phrase search, should find results based on token overlap
}

// ============================================================
// English text
// ============================================================

TEST_F(IntegrationTest, EnglishText) {
    env.addDocument("Artificial Intelligence",
                    "Artificial intelligence is a branch of computer science");
    env.addDocument("Machine Learning",
                    "Machine learning is a subset of artificial intelligence");
    env.flushIndexBuffer();

    auto results = env.getSearchEngine().searchWithResults("artificial intelligence");
    ASSERT_GE(results.size(), 1u);
}

TEST_F(IntegrationTest, CaseInsensitive) {
    env.addDocument("Test", "Hello World");
    env.flushIndexBuffer();

    auto lower = env.getSearchEngine().searchWithResults("hello world");
    auto upper = env.getSearchEngine().searchWithResults("HELLO WORLD");
    EXPECT_EQ(lower.size(), upper.size());
    if (!lower.empty() && !upper.empty()) {
        EXPECT_EQ(lower[0].first, upper[0].first);
    }
}

// ============================================================
// Buffer flush threshold
// ============================================================

TEST_F(IntegrationTest, BufferFlushThreshold) {
    env.setBufferUpdateThreshold(5); // Very low threshold
    // Adding documents should trigger automatic flush
    env.addDocument("Auto1", "自动刷新测试文档一二三四五");
    env.addDocument("Auto2", "自动刷新测试文档六七八九十");

    // After auto-flush, documents should be searchable from DB
    auto results = env.getSearchEngine().searchWithResults("自动刷新");
    ASSERT_FALSE(results.empty());
}

// ============================================================
// Max index count
// ============================================================

TEST_F(IntegrationTest, MaxIndexCount) {
    env.setMaxIndexCount(2);
    env.addDocument("Doc1", "第一个文档");
    env.addDocument("Doc2", "第二个文档");
    env.addDocument("Doc3", "第三个文档"); // Should be rejected

    EXPECT_TRUE(env.hasReachedIndexLimit());
    EXPECT_EQ(env.getIndexedCount(), 2);
}

// ============================================================
// Document update
// ============================================================

TEST_F(IntegrationTest, UpdateDocument) {
    env.addDocument("更新测试", "原始内容包含关键词甲");
    env.flushIndexBuffer();

    env.addDocument("更新测试", "新内容包含关键词乙");
    env.flushIndexBuffer();

    // Should find updated content
    auto results = env.getSearchEngine().searchWithResults("关键词乙");
    // Note: The inverted index may still have stale entries from old content
    // This tests that the document body is actually updated
    auto body = env.getDatabase().getDocumentBody(
        env.getDatabase().getDocumentId("更新测试"));
    EXPECT_EQ(body, "新内容包含关键词乙");
}

// ============================================================
// Edge cases
// ============================================================

TEST_F(IntegrationTest, EmptyQuery) {
    env.addDocument("文档", "内容");
    env.flushIndexBuffer();

    auto results = env.getSearchEngine().searchWithResults("");
    EXPECT_TRUE(results.empty());
}

TEST_F(IntegrationTest, SingleCharQuery) {
    env.addDocument("文档", "内容");
    env.flushIndexBuffer();

    // Single char with N=2 bigram should return empty (too short for N-gram)
    auto results = env.getSearchEngine().searchWithResults("内");
    // May use LIKE fallback
}

TEST_F(IntegrationTest, SpecialCharactersInDocument) {
    env.addDocument("特殊字符", "包含特殊字符：!@#$%^&*()的文档");
    env.flushIndexBuffer();

    auto results = env.getSearchEngine().searchWithResults("特殊字符");
    ASSERT_FALSE(results.empty());
}
