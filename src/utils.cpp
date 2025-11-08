#include "wiser/utils.h"
#include <chrono>
#include <cstdio>
#include <cctype>
#include <algorithm>

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

    // 现代 printError/printInfo 已移除，统一使用 spdlog

    namespace {
        std::chrono::high_resolution_clock::time_point last_time =
                std::chrono::high_resolution_clock::now();
    }

    // ---- 新增通用工具实现 ----
    bool Utils::isIgnoredChar(const UTF32Char ch) {
        if (ch <= 127) {
            // Treat ASCII whitespace as ignored; for punctuation we ignore most but KEEP '.' so that
            // decimal numbers like 2.5 form a continuous run and can produce n-gram tokens (e.g., "2.", ".5").
            // This enables searching such numbers. Previously '.' was ignored (std::ispunct) causing
            // runs "2" and "5" (< N) to be discarded when N=2, so queries like "2.5" produced no tokens.
            if (std::isspace(static_cast<unsigned char>(ch))) {
                return true;
            }
            if (std::ispunct(static_cast<unsigned char>(ch))) {
                if (ch == '.') {
                    return false; // keep periods inside tokens
                }
                return true;
            }
            return false;
        }
        switch (ch) {
            case 0x3000: // 全角空格
            case 0x3001: // 、
            case 0x3002: // 。
            case 0xFF08: // （
            case 0xFF09: // ）
            case 0xFF01: // ！
            case 0xFF0C: // ，
            case 0xFF1A: // ：
            case 0xFF1B: // ；
            case 0xFF1F: // ？
            case 0xFF3B: // ［
            case 0xFF3D: // ］
            case 0x201C: // “
            case 0x201D: // ”
            case 0x2018: // ‘
            case 0x2019: // ’
                return true;
            default:
                return false;
        }
    }

    void Utils::toLowerAsciiInPlace(std::string& s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            if (c < 128)
                return static_cast<char>(std::tolower(c));
            return static_cast<char>(c);
        });
    }

    bool Utils::endsWithIgnoreCase(const std::string& s, const std::string& ext) {
        if (s.size() < ext.size())
            return false;
        auto it1 = s.rbegin();
        auto it2 = ext.rbegin();
        for (; it2 != ext.rend(); ++it1, ++it2) {
            unsigned char a = static_cast<unsigned char>(*it1);
            unsigned char b = static_cast<unsigned char>(*it2);
            if (a < 128)
                a = static_cast<unsigned char>(std::tolower(a));
            if (b < 128)
                b = static_cast<unsigned char>(std::tolower(b));
            if (a != b)
                return false;
        }
        return true;
    }

    std::vector<std::string> Utils::tokenizeQueryTokens(const std::string& q, int n) {
        std::vector<std::string> tokens;
        auto utf32_vec = utf8ToUtf32(q);

        std::vector<std::vector<UTF32Char>> runs;
        std::vector<UTF32Char> cur;
        cur.reserve(16);
        for (auto cp: utf32_vec) {
            if (isIgnoredChar(cp)) {
                if (!cur.empty()) {
                    runs.push_back(cur);
                    cur.clear();
                }
            } else {
                if (cp <= 127) {
                    cp = static_cast<UTF32Char>(std::tolower(static_cast<unsigned char>(cp)));
                }
                cur.push_back(cp);
            }
        }
        if (!cur.empty())
            runs.push_back(cur);

        for (auto& run: runs) {
            if (run.size() < static_cast<size_t>(n))
                continue;
            for (size_t i = 0; i + static_cast<size_t>(n) <= run.size(); ++i) {
                std::vector<UTF32Char> ng(run.begin() + static_cast<std::ptrdiff_t>(i),
                                          run.begin() + static_cast<std::ptrdiff_t>(i + n));
                tokens.push_back(utf32ToUtf8(ng));
            }
        }

        // 去重，保序
        std::vector<std::string> unique;
        unique.reserve(tokens.size());
        std::unordered_set<std::string> seen;
        for (auto& t: tokens) {
            if (!seen.contains(t)) {
                seen.insert(t);
                unique.push_back(std::move(t));
            }
        }
        return unique;
    }

    std::string Utils::json_escape(const std::string& s) {
        std::ostringstream o;
        for (auto c = s.cbegin(); c != s.cend(); ++c) {
            switch (*c) {
                case '"':
                    o << "\\\"";
                    break;
                case '\\':
                    o << "\\\\";
                    break;
                case '\b':
                    o << "\\b";
                    break;
                case '\f':
                    o << "\\f";
                    break;
                case '\n':
                    o << "\\n";
                    break;
                case '\r':
                    o << "\\r";
                    break;
                case '\t':
                    o << "\\t";
                    break;
                default:
                    if ('\x00' <= *c && *c <= '\x1f') {
                        o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)*c << std::dec;
                    } else {
                        o << *c;
                    }
            }
        }
        return o.str();
    }
} // namespace wiser
