#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <string>

namespace wiser {

    /**
     * @brief 位流写入器
     * 用于按位写入数据
     */
    class BitWriter {
    public:
        void writeBit(bool bit) {
            if (bit_count_ == 8) {
                buffer_.push_back(current_byte_);
                current_byte_ = 0;
                bit_count_ = 0;
            }
            if (bit) {
                current_byte_ |= (1 << (7 - bit_count_));
            }
            bit_count_++;
        }

        void writeBits(uint32_t value, int bits) {
            for (int i = bits - 1; i >= 0; --i) {
                writeBit((value >> i) & 1);
            }
        }

        /**
         * @brief 写入一元编码 (Unary Coding)
         * 用于写入 Golomb 编码的商部分 q
         * 写入 q 个 1，以 0 结束
         */
        void writeUnary(uint32_t q) {
            for (uint32_t i = 0; i < q; ++i) {
                writeBit(true);
            }
            writeBit(false);
        }

        /**
         * @brief 刷入缓冲区并返回数据
         */
        std::vector<char> getData() {
            if (bit_count_ > 0) {
                buffer_.push_back(current_byte_);
                // 不重置，允许继续写入（如果不清理的话），但通常是用一次获取
            }
            return buffer_;
        }

    private:
        std::vector<char> buffer_;
        unsigned char current_byte_ = 0;
        int bit_count_ = 0;
    };

    /**
     * @brief 位流读取器
     * 用于按位读取数据
     */
    class BitReader {
    public:
        BitReader(const std::vector<char>& data) : data_(data) {}

        bool readBit() {
            if (byte_index_ >= data_.size()) {
                throw std::out_of_range("BitReader: End of stream");
            }
            bool bit = (data_[byte_index_] >> (7 - bit_index_)) & 1;
            bit_index_++;
            if (bit_index_ == 8) {
                bit_index_ = 0;
                byte_index_++;
            }
            return bit;
        }

        uint32_t readBits(int bits) {
            uint32_t value = 0;
            for (int i = 0; i < bits; ++i) {
                value = (value << 1) | (readBit() ? 1 : 0);
            }
            return value;
        }

        /**
         * @brief 读取一元编码
         * 读取连续的 1 直到遇到 0
         */
        uint32_t readUnary() {
            uint32_t q = 0;
            while (readBit()) {
                q++;
            }
            return q;
        }

        bool eof() const {
             return byte_index_ >= data_.size();
        }

    private:
        const std::vector<char>& data_;
        size_t byte_index_ = 0;
        int bit_index_ = 0;
    };

    /**
     * @brief Golomb 编码工具类
     */
    class GolombEncoder {
    public:
        /**
         * @brief 编码一个整数
         * @param x 要编码的整数 (必须 >= 0)
         * @param M Golomb 参数 M
         * @param writer 位流写入器
         */
        static void encode(uint32_t x, int M, BitWriter& writer) {
            uint32_t q = x / M;
            uint32_t r = x % M;

            // 1. 写入商 q (Unary)
            writer.writeUnary(q);

            // 2. 写入余数 r (Truncated Binary)
            int b = 0;
            // 计算 b = ceil(log2(M))
            // 简单的做法是寻找满足 2^b >= M 的最小 b
            int tempM = M;
            while ((1 << b) < tempM) {
                b++;
            }

            int cutoff = (1 << b) - M;

            if (r < static_cast<uint32_t>(cutoff)) {
                writer.writeBits(r, b - 1);
            } else {
                writer.writeBits(r + cutoff, b);
            }
        }
    };

    class GolombDecoder {
    public:
        /**
         * @brief 解码一个整数
         * @param M Golomb 参数 M
         * @param reader 位流读取器
         * @return 解码后的整数
         */
        static uint32_t decode(int M, BitReader& reader) {
            // 1. 读取商 q (Unary)
            uint32_t q = reader.readUnary();

            // 2. 读取余数 r (Truncated Binary)
            int b = 0;
            int tempM = M;
            while ((1 << b) < tempM) {
                b++;
            }

            int cutoff = (1 << b) - M;

            // 先读 b-1 位
            uint32_t r = reader.readBits(b - 1);
            if (r < static_cast<uint32_t>(cutoff)) {
                // r = value
            } else {
                // 再读 1 位
                // 原有的 r 是高 (b-1) 位
                uint32_t next_bit = reader.readBits(1);
                r = (r << 1) | next_bit;
                r = r - cutoff;
            }

            return q * M + r;
        }
    };

} // namespace wiser
