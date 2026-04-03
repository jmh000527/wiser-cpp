/**
 * @file test_utils.cpp
 * @brief Utils 类的单元测试：UTF-8/UTF-32 转换、字符判断、JSON 转义等。
 */

#include <gtest/gtest.h>
#include "wiser/utils.h"

using namespace wiser;

// ============================================================
// UTF-8 ↔ UTF-32 转换
// ============================================================

TEST(UtilsTest, Utf8ToUtf32_Ascii) {
    auto result = Utils::utf8ToUtf32("Hello");
    ASSERT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0], 'H');
    EXPECT_EQ(result[4], 'o');
}

TEST(UtilsTest, Utf8ToUtf32_Chinese) {
    // "你好" = U+4F60 U+597D
    auto result = Utils::utf8ToUtf32("你好");
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], 0x4F60u);
    EXPECT_EQ(result[1], 0x597Du);
}

TEST(UtilsTest, Utf8ToUtf32_Empty) {
    auto result = Utils::utf8ToUtf32("");
    EXPECT_TRUE(result.empty());
}

TEST(UtilsTest, Utf8ToUtf32_Mixed) {
    // "A你B" -> ['A', 0x4F60, 'B']
    auto result = Utils::utf8ToUtf32("A你B");
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], 'A');
    EXPECT_EQ(result[1], 0x4F60u);
    EXPECT_EQ(result[2], 'B');
}

TEST(UtilsTest, Utf32ToUtf8_Ascii) {
    std::vector<UTF32Char> input = {'H', 'i'};
    EXPECT_EQ(Utils::utf32ToUtf8(input), "Hi");
}

TEST(UtilsTest, Utf32ToUtf8_Chinese) {
    std::vector<UTF32Char> input = {0x4F60, 0x597D};
    EXPECT_EQ(Utils::utf32ToUtf8(input), "你好");
}

TEST(UtilsTest, Utf8Utf32_Roundtrip) {
    std::string original = "Hello 世界! 🌍";
    auto utf32 = Utils::utf8ToUtf32(original);
    auto back = Utils::utf32ToUtf8(utf32);
    EXPECT_EQ(back, original);
}

// ============================================================
// isIgnoredChar
// ============================================================

TEST(UtilsTest, IsIgnoredChar_Space) {
    EXPECT_TRUE(Utils::isIgnoredChar(' '));
    EXPECT_TRUE(Utils::isIgnoredChar('\t'));
    EXPECT_TRUE(Utils::isIgnoredChar('\n'));
    EXPECT_TRUE(Utils::isIgnoredChar('\r'));
}

TEST(UtilsTest, IsIgnoredChar_Punctuation) {
    EXPECT_TRUE(Utils::isIgnoredChar(','));
    EXPECT_TRUE(Utils::isIgnoredChar(';'));
    EXPECT_TRUE(Utils::isIgnoredChar('!'));
    EXPECT_TRUE(Utils::isIgnoredChar('?'));
    EXPECT_TRUE(Utils::isIgnoredChar('('));
    EXPECT_TRUE(Utils::isIgnoredChar(')'));
}

TEST(UtilsTest, IsIgnoredChar_DotNotIgnored) {
    // '.' is intentionally NOT ignored (supports decimal number indexing like "2.5")
    EXPECT_FALSE(Utils::isIgnoredChar('.'));
}

TEST(UtilsTest, IsIgnoredChar_AlphaNumNotIgnored) {
    EXPECT_FALSE(Utils::isIgnoredChar('A'));
    EXPECT_FALSE(Utils::isIgnoredChar('z'));
    EXPECT_FALSE(Utils::isIgnoredChar('0'));
    EXPECT_FALSE(Utils::isIgnoredChar('9'));
}

TEST(UtilsTest, IsIgnoredChar_ChineseNotIgnored) {
    EXPECT_FALSE(Utils::isIgnoredChar(0x4F60)); // '你'
    EXPECT_FALSE(Utils::isIgnoredChar(0x597D)); // '好'
}

TEST(UtilsTest, IsIgnoredChar_CjkPunctuationIgnored) {
    // CJK punctuation should be ignored
    EXPECT_TRUE(Utils::isIgnoredChar(0x3001)); // 、
    EXPECT_TRUE(Utils::isIgnoredChar(0x3002)); // 。
    EXPECT_TRUE(Utils::isIgnoredChar(0xFF0C)); // ，
}

// ============================================================
// toLowerAscii
// ============================================================

TEST(UtilsTest, ToLowerAsciiInPlace) {
    std::string s = "Hello WORLD 你好";
    Utils::toLowerAsciiInPlace(s);
    EXPECT_EQ(s, "hello world 你好");
}

TEST(UtilsTest, ToLowerAsciiCopy) {
    EXPECT_EQ(Utils::toLowerAsciiCopy("ABC"), "abc");
    EXPECT_EQ(Utils::toLowerAsciiCopy("abc"), "abc");
    EXPECT_EQ(Utils::toLowerAsciiCopy(""), "");
}

// ============================================================
// endsWithIgnoreCase
// ============================================================

TEST(UtilsTest, EndsWithIgnoreCase) {
    EXPECT_TRUE(Utils::endsWithIgnoreCase("data.JSON", ".json"));
    EXPECT_TRUE(Utils::endsWithIgnoreCase("file.TSV", ".tsv"));
    EXPECT_TRUE(Utils::endsWithIgnoreCase("file.xml", ".xml"));
    EXPECT_FALSE(Utils::endsWithIgnoreCase("file.txt", ".xml"));
    EXPECT_FALSE(Utils::endsWithIgnoreCase("x", ".xml"));
}

// ============================================================
// json_escape
// ============================================================

TEST(UtilsTest, JsonEscape_NoEscape) {
    EXPECT_EQ(Utils::json_escape("hello"), "hello");
}

TEST(UtilsTest, JsonEscape_Quotes) {
    EXPECT_EQ(Utils::json_escape("he said \"hi\""), "he said \\\"hi\\\"");
}

TEST(UtilsTest, JsonEscape_Backslash) {
    EXPECT_EQ(Utils::json_escape("path\\to"), "path\\\\to");
}

TEST(UtilsTest, JsonEscape_ControlChars) {
    EXPECT_EQ(Utils::json_escape("a\nb\tc"), "a\\nb\\tc");
}

TEST(UtilsTest, JsonEscape_NullChar) {
    std::string s(1, '\0');
    EXPECT_EQ(Utils::json_escape(s), "\\u0000");
}

// ============================================================
// tokenizeQueryTokens
// ============================================================

TEST(UtilsTest, TokenizeQueryTokens_Bigram) {
    auto tokens = Utils::tokenizeQueryTokens("你好世界", 2);
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "你好");
    EXPECT_EQ(tokens[1], "好世");
    EXPECT_EQ(tokens[2], "世界");
}

TEST(UtilsTest, TokenizeQueryTokens_Short) {
    // Single character query with n=2 should return empty
    auto tokens = Utils::tokenizeQueryTokens("你", 2);
    EXPECT_TRUE(tokens.empty());
}

TEST(UtilsTest, TokenizeQueryTokens_Deduplicated) {
    // "ABAB" bigrams: AB, BA, AB -> deduplicated to AB, BA
    auto tokens = Utils::tokenizeQueryTokens("ABAB", 2);
    ASSERT_EQ(tokens.size(), 2u);
    // Lowercase: ab, ba
    EXPECT_EQ(tokens[0], "ab");
    EXPECT_EQ(tokens[1], "ba");
}

// ============================================================
// calculateUtf8Size
// ============================================================

TEST(UtilsTest, CalculateUtf8Size) {
    std::vector<UTF32Char> ascii = {'H', 'i'};
    EXPECT_EQ(Utils::calculateUtf8Size(ascii), 2);

    std::vector<UTF32Char> chinese = {0x4F60, 0x597D}; // 你好
    EXPECT_EQ(Utils::calculateUtf8Size(chinese), 6); // 3 bytes each
}

// ============================================================
// Buffer
// ============================================================

TEST(BufferTest, AppendAndSize) {
    Buffer buf;
    EXPECT_EQ(buf.size(), 0u);

    const char data[] = "hello";
    buf.append(data, 5);
    EXPECT_EQ(buf.size(), 5u);
    EXPECT_EQ(std::string(buf.data(), buf.size()), "hello");
}

TEST(BufferTest, Clear) {
    Buffer buf;
    buf.append("test", 4);
    buf.clear();
    EXPECT_EQ(buf.size(), 0u);
}

TEST(BufferTest, AppendBit) {
    Buffer buf;
    // Write 8 bits: 10110100 = 0xB4
    buf.appendBit(1);
    buf.appendBit(0);
    buf.appendBit(1);
    buf.appendBit(1);
    buf.appendBit(0);
    buf.appendBit(1);
    buf.appendBit(0);
    buf.appendBit(0);
    EXPECT_EQ(buf.size(), 1u);
    EXPECT_EQ(static_cast<unsigned char>(buf.data()[0]), 0xB4);
}
