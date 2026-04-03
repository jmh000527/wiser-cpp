/**
 * @file test_compression.cpp
 * @brief BitWriter/BitReader 与 Golomb 编码的单元测试。
 */

#include <gtest/gtest.h>
#include "wiser/compression_utils.h"

using namespace wiser;

// ============================================================
// BitWriter / BitReader
// ============================================================

TEST(BitWriterTest, SingleBit) {
    BitWriter w;
    w.writeBit(true);
    auto data = w.getData();
    ASSERT_EQ(data.size(), 1u);
    EXPECT_EQ(static_cast<unsigned char>(data[0]), 0x80); // 1000 0000
}

TEST(BitWriterTest, MultipleBits) {
    BitWriter w;
    // Write 10110100
    w.writeBit(true);
    w.writeBit(false);
    w.writeBit(true);
    w.writeBit(true);
    w.writeBit(false);
    w.writeBit(true);
    w.writeBit(false);
    w.writeBit(false);
    auto data = w.getData();
    ASSERT_EQ(data.size(), 1u);
    EXPECT_EQ(static_cast<unsigned char>(data[0]), 0xB4);
}

TEST(BitWriterTest, WriteBitsValue) {
    BitWriter w;
    w.writeBits(0b101, 3); // 3 bits: 101
    auto data = w.getData();
    ASSERT_EQ(data.size(), 1u);
    // 101 followed by 00000 padding -> 10100000 = 0xA0
    EXPECT_EQ(static_cast<unsigned char>(data[0]), 0xA0);
}

TEST(BitWriterTest, GetDataIdempotent) {
    // After our fix, getData() should be safe to call twice
    BitWriter w;
    w.writeBit(true);
    w.writeBit(false);
    auto data1 = w.getData();
    auto data2 = w.getData();
    EXPECT_EQ(data1, data2);
}

TEST(BitReaderTest, ReadBits) {
    // 0xB4 = 10110100
    std::vector<char> data = {static_cast<char>(0xB4)};
    BitReader r(data);
    EXPECT_TRUE(r.readBit());   // 1
    EXPECT_FALSE(r.readBit());  // 0
    EXPECT_TRUE(r.readBit());   // 1
    EXPECT_TRUE(r.readBit());   // 1
    EXPECT_FALSE(r.readBit());  // 0
    EXPECT_TRUE(r.readBit());   // 1
    EXPECT_FALSE(r.readBit());  // 0
    EXPECT_FALSE(r.readBit());  // 0
    EXPECT_TRUE(r.eof());
}

TEST(BitReaderTest, ReadBitsValue) {
    // 0xA0 = 10100000
    std::vector<char> data = {static_cast<char>(0xA0)};
    BitReader r(data);
    uint32_t val = r.readBits(3);
    EXPECT_EQ(val, 0b101u);
}

TEST(BitReaderTest, ReadBeyondEof) {
    std::vector<char> data = {0x00};
    BitReader r(data);
    for (int i = 0; i < 8; ++i) r.readBit();
    EXPECT_THROW(r.readBit(), std::out_of_range);
}

// ============================================================
// BitWriter + BitReader roundtrip
// ============================================================

TEST(BitRoundtripTest, WriteAndRead) {
    BitWriter w;
    w.writeBits(42, 8);   // 00101010
    w.writeBits(7, 4);    // 0111
    w.writeBit(true);     // 1
    auto data = w.getData();

    BitReader r(data);
    EXPECT_EQ(r.readBits(8), 42u);
    EXPECT_EQ(r.readBits(4), 7u);
    EXPECT_TRUE(r.readBit());
}

// ============================================================
// Golomb Encoding / Decoding
// ============================================================

TEST(GolombTest, EncodeDecodeSmallValues) {
    for (int M = 2; M <= 8; ++M) {
        for (uint32_t x = 0; x < 50; ++x) {
            BitWriter w;
            GolombEncoder::encode(x, M, w);
            auto data = w.getData();

            BitReader r(data);
            uint32_t decoded = GolombDecoder::decode(M, r);
            EXPECT_EQ(decoded, x)
                << "Failed for M=" << M << ", x=" << x;
        }
    }
}

TEST(GolombTest, EncodeDecodeSequence) {
    int M = 4;
    std::vector<uint32_t> values = {0, 1, 3, 7, 15, 31, 100};

    BitWriter w;
    for (auto v : values) {
        GolombEncoder::encode(v, M, w);
    }
    auto data = w.getData();

    BitReader r(data);
    for (auto expected : values) {
        uint32_t decoded = GolombDecoder::decode(M, r);
        EXPECT_EQ(decoded, expected);
    }
}

TEST(GolombTest, EncodeDecodeZero) {
    BitWriter w;
    GolombEncoder::encode(0, 3, w);
    auto data = w.getData();

    BitReader r(data);
    EXPECT_EQ(GolombDecoder::decode(3, r), 0u);
}

TEST(GolombTest, EncodeDecodeLargeValue) {
    uint32_t x = 10000;
    int M = 5;
    BitWriter w;
    GolombEncoder::encode(x, M, w);
    auto data = w.getData();

    BitReader r(data);
    EXPECT_EQ(GolombDecoder::decode(M, r), x);
}

// ============================================================
// Unary Encoding
// ============================================================

TEST(UnaryTest, WriteAndRead) {
    BitWriter w;
    w.writeUnary(0); // just "0"
    w.writeUnary(3); // "1110"
    w.writeUnary(1); // "10"
    auto data = w.getData();

    BitReader r(data);
    EXPECT_EQ(r.readUnary(), 0u);
    EXPECT_EQ(r.readUnary(), 3u);
    EXPECT_EQ(r.readUnary(), 1u);
}
