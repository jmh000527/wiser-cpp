#include "wiser/utils.h"
#include <chrono>
#include <cstdio>

namespace wiser {
    // Buffer 实现
    Buffer::Buffer() {
        buffer_.reserve(32); // 初始容量
    }

    void Buffer::append(const void* data, size_t size) {
        if (bit_position_ > 0) {
            // 如果有位数据待处理，先补齐当前字节
            bit_position_ = 0;
        }

        const char* char_data = static_cast<const char*>(data);
        buffer_.insert(buffer_.end(), char_data, char_data + size);
    }

    void Buffer::appendBit(std::int32_t bit) {
        if (bit_position_ == 0) {
            buffer_.push_back(0); // 添加新字节
        }

        if (bit) {
            buffer_.back() |= (1 << (7 - bit_position_));
        }

        bit_position_ = (bit_position_ + 1) % 8;
    }

    void Buffer::clear() {
        buffer_.clear();
        bit_position_ = 0;
    }

    // Utils 实现
    std::string Utils::utf32ToUtf8(const std::vector<UTF32Char>& utf32_str) {
        if (utf32_str.empty()) {
            return "";
        }

        std::string result;
        result.reserve(utf32_str.size() * 4); // 预分配空间

        for (UTF32Char code_point: utf32_str) {
            if (code_point <= 0x7F) {
                // 1字节UTF-8
                result.push_back(static_cast<char>(code_point));
            } else if (code_point <= 0x7FF) {
                // 2字节UTF-8
                result.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
                result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
            } else if (code_point <= 0xFFFF) {
                // 3字节UTF-8
                result.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
                result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
            } else if (code_point <= 0x10FFFF) {
                // 4字节UTF-8
                result.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
                result.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
            }
            // 忽略无效的码点
        }

        return result;
    }

    std::vector<UTF32Char> Utils::utf8ToUtf32(const std::string& utf8_str) {
        std::vector<UTF32Char> result;

        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(utf8_str.c_str());
        size_t len = utf8_str.length();

        for (size_t i = 0; i < len;) {
            UTF32Char code_point = 0;
            std::int32_t byte_count = 0;

            unsigned char first_byte = bytes[i];

            if ((first_byte & 0x80) == 0) {
                // 1字节字符 (0xxxxxxx)
                code_point = first_byte;
                byte_count = 1;
            } else if ((first_byte & 0xE0) == 0xC0) {
                // 2字节字符 (110xxxxx 10xxxxxx)
                code_point = first_byte & 0x1F;
                byte_count = 2;
            } else if ((first_byte & 0xF0) == 0xE0) {
                // 3字节字符 (1110xxxx 10xxxxxx 10xxxxxx)
                code_point = first_byte & 0x0F;
                byte_count = 3;
            } else if ((first_byte & 0xF8) == 0xF0) {
                // 4字节字符 (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
                code_point = first_byte & 0x07;
                byte_count = 4;
            } else {
                // 无效的UTF-8序列，跳过
                ++i;
                continue;
            }

            // 检查是否有足够的字节
            if (i + byte_count > len) {
                break;
            }

            // 读取后续字节
            bool valid = true;
            for (std::int32_t j = 1; j < byte_count; ++j) {
                if ((bytes[i + j] & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
                code_point = (code_point << 6) | (bytes[i + j] & 0x3F);
            }

            if (valid) {
                result.push_back(code_point);
                i += static_cast<size_t>(byte_count);
            } else {
                // 无效序列，跳过第一个字节
                ++i;
            }
        }

        return result;
    }

    std::int32_t Utils::calculateUtf8Size(const std::vector<UTF32Char>& utf32_str) {
        std::int32_t size = 0;

        for (UTF32Char code_point: utf32_str) {
            if (code_point <= 0x7F) {
                size += 1;
            } else if (code_point <= 0x7FF) {
                size += 2;
            } else if (
                code_point <= 0xFFFF) {
                size += 3;
            } else if (code_point <= 0x10FFFF) {
                size += 4;
            }
        }

        return size;
    }

    // 现代 printError/printInfo 在头文件中以内联模板实现

    namespace {
        std::chrono::high_resolution_clock::time_point last_time =
                std::chrono::high_resolution_clock::now();
    }

    void Utils::printTimeDiff() {
        auto current_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                                              current_time - last_time);

        printInfo("Time elapsed: {} ms\n", duration.count());

        last_time = current_time;
    }
} // namespace wiser
