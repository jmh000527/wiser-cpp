/**
 * @file auth.cpp
 * @brief JWT 身份验证与用户管理实现。
 */

#include "wiser/web/auth.h"
#include "wiser/3rdparty/httplib.h"
#include <jwt-cpp/traits/nlohmann-json/defaults.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>

#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <array>
#include <algorithm>

namespace wiser::web {

    // ================================================================
    // SHA-256 实现（FIPS PUB 180-4，自包含无外部依赖）
    // ================================================================
    namespace {
        static constexpr std::array<uint32_t, 64> K = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
            0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
            0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
        inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
        inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
        inline uint32_t sig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
        inline uint32_t sig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
        inline uint32_t gam0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
        inline uint32_t gam1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

        std::array<uint8_t, 32> sha256_hash(const uint8_t* data, size_t len) {
            uint32_t h0 = 0x6a09e667, h1 = 0xbb67ae85, h2 = 0x3c6ef372, h3 = 0xa54ff53a;
            uint32_t h4 = 0x510e527f, h5 = 0x9b05688c, h6 = 0x1f83d9ab, h7 = 0x5be0cd19;

            // Pre-processing: padding
            size_t bit_len = len * 8;
            size_t padded_len = ((len + 8) / 64 + 1) * 64;
            std::vector<uint8_t> msg(padded_len, 0);
            std::memcpy(msg.data(), data, len);
            msg[len] = 0x80;
            for (int i = 0; i < 8; ++i)
                msg[padded_len - 1 - i] = static_cast<uint8_t>(bit_len >> (i * 8));

            // Process each 512-bit block
            for (size_t offset = 0; offset < padded_len; offset += 64) {
                uint32_t w[64];
                for (int i = 0; i < 16; ++i)
                    w[i] = (uint32_t(msg[offset + i * 4]) << 24) |
                            (uint32_t(msg[offset + i * 4 + 1]) << 16) |
                            (uint32_t(msg[offset + i * 4 + 2]) << 8) |
                            uint32_t(msg[offset + i * 4 + 3]);
                for (int i = 16; i < 64; ++i)
                    w[i] = gam1(w[i - 2]) + w[i - 7] + gam0(w[i - 15]) + w[i - 16];

                uint32_t a = h0, b = h1, c = h2, d = h3;
                uint32_t e = h4, f = h5, g = h6, h = h7;

                for (int i = 0; i < 64; ++i) {
                    uint32_t t1 = h + sig1(e) + ch(e, f, g) + K[i] + w[i];
                    uint32_t t2 = sig0(a) + maj(a, b, c);
                    h = g; g = f; f = e; e = d + t1;
                    d = c; c = b; b = a; a = t1 + t2;
                }

                h0 += a; h1 += b; h2 += c; h3 += d;
                h4 += e; h5 += f; h6 += g; h7 += h;
            }

            std::array<uint8_t, 32> result{};
            auto put = [&result](int off, uint32_t val) {
                result[off]     = uint8_t(val >> 24);
                result[off + 1] = uint8_t(val >> 16);
                result[off + 2] = uint8_t(val >> 8);
                result[off + 3] = uint8_t(val);
            };
            put(0, h0); put(4, h1); put(8, h2); put(12, h3);
            put(16, h4); put(20, h5); put(24, h6); put(28, h7);
            return result;
        }

        std::string to_hex(const uint8_t* data, size_t len) {
            std::ostringstream oss;
            for (size_t i = 0; i < len; ++i)
                oss << std::hex << std::setw(2) << std::setfill('0') << int(data[i]);
            return oss.str();
        }
    } // anonymous namespace

    // ================================================================
    // AuthManager 实现
    // ================================================================

    AuthManager::AuthManager(const AuthConfig& config) : config_(config) {}

    bool AuthManager::initialize(sqlite3* db) {
        if (!db) return false;
        db_ = db;

        const char* sql =
            "CREATE TABLE IF NOT EXISTS users ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  username TEXT NOT NULL UNIQUE,"
            "  password_hash TEXT NOT NULL,"
            "  role TEXT NOT NULL DEFAULT 'viewer',"
            "  api_key TEXT UNIQUE,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_users_api_key ON users(api_key);";

        char* err = nullptr;
        int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            spdlog::error("Failed to create users table: {}", err ? err : "unknown");
            sqlite3_free(err);
            return false;
        }

        spdlog::info("Auth module initialized (enabled={})", config_.enabled);
        return true;
    }

    // ─── 密码哈希 ───

    std::string AuthManager::sha256(const std::string& input) {
        auto hash = sha256_hash(reinterpret_cast<const uint8_t*>(input.data()), input.size());
        return to_hex(hash.data(), hash.size());
    }

    std::string AuthManager::generateSalt() {
        return generateRandomHex(16); // 16 bytes = 32 hex chars
    }

    std::string AuthManager::generateRandomHex(int bytes) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 255);
        std::ostringstream oss;
        for (int i = 0; i < bytes; ++i)
            oss << std::hex << std::setw(2) << std::setfill('0') << dist(gen);
        return oss.str();
    }

    std::string AuthManager::hashPassword(const std::string& password, const std::string& salt) {
        // PBKDF2-like: iterate SHA-256 10000 times
        constexpr int iterations = 10000;
        std::string current = salt + password;
        for (int i = 0; i < iterations; ++i) {
            current = sha256(current);
        }
        // Format: $sha256$iterations$salt$hash
        return "$sha256$" + std::to_string(iterations) + "$" + salt + "$" + current;
    }

    // Constant-time string comparison to prevent timing attacks
    static bool constant_time_equal(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        volatile unsigned char result = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            result |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
        }
        return result == 0;
    }

    bool AuthManager::verifyPassword(const std::string& password, const std::string& stored_hash) {
        // Parse stored hash format: $sha256$iterations$salt$hash
        if (stored_hash.size() < 8 || stored_hash.substr(0, 8) != "$sha256$") return false;

        try {
            size_t pos1 = 8;
            size_t pos2 = stored_hash.find('$', pos1);
            if (pos2 == std::string::npos) return false;
            int iterations = std::stoi(stored_hash.substr(pos1, pos2 - pos1));
            if (iterations <= 0 || iterations > 1000000) return false;

            size_t pos3 = stored_hash.find('$', pos2 + 1);
            if (pos3 == std::string::npos) return false;
            std::string salt = stored_hash.substr(pos2 + 1, pos3 - pos2 - 1);
            std::string expected_hash = stored_hash.substr(pos3 + 1);

            std::string current = salt + password;
            for (int i = 0; i < iterations; ++i) {
                current = sha256(current);
            }
            return constant_time_equal(current, expected_hash);
        } catch (const std::exception&) {
            return false;
        }
    }

    // ─── 用户管理 ───

    std::optional<UserInfo> AuthManager::registerUser(const std::string& username,
                                                       const std::string& password,
                                                       Role role) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!db_) return std::nullopt;

        // 输入验证
        if (username.size() < 3 || username.size() > 64) return std::nullopt;
        if (password.size() < 6 || password.size() > 128) return std::nullopt;

        std::string salt = generateSalt();
        std::string hash = hashPassword(password, salt);
        std::string role_str(roleToString(role));

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?)";
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return std::nullopt;

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, role_str.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return std::nullopt; // UNIQUE constraint violation = user exists
        }

        int64_t user_id = sqlite3_last_insert_rowid(db_);
        sqlite3_finalize(stmt);

        spdlog::info("User registered: {} (role={})", username, role_str);
        return UserInfo{user_id, username, role};
    }

    std::optional<UserInfo> AuthManager::authenticateUser(const std::string& username,
                                                           const std::string& password) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!db_) return std::nullopt;

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT id, password_hash, role FROM users WHERE username = ?";
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return std::nullopt;

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        int64_t id = sqlite3_column_int64(stmt, 0);
        auto hash_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        auto role_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (!hash_ptr || !role_ptr) {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }
        std::string stored_hash(hash_ptr);
        std::string role_str(role_ptr);
        sqlite3_finalize(stmt);

        if (!verifyPassword(password, stored_hash)) {
            return std::nullopt;
        }

        return UserInfo{id, username, roleFromString(role_str)};
    }

    // ─── JWT ───

    std::string AuthManager::generateToken(const UserInfo& user) {
        auto token = jwt::create()
            .set_issuer("wiser")
            .set_subject(user.username)
            .set_payload_claim("uid", jwt::claim(std::to_string(user.id)))
            .set_payload_claim("role", jwt::claim(std::string(roleToString(user.role))))
            .set_issued_at(std::chrono::system_clock::now())
            .set_expires_at(std::chrono::system_clock::now()
                            + std::chrono::hours(config_.token_expiry_hours))
            .sign(jwt::algorithm::hs256{config_.jwt_secret});
        return token;
    }

    std::optional<UserInfo> AuthManager::validateToken(const std::string& token) {
        try {
            auto decoded = jwt::decode(token);
            auto verifier = jwt::verify()
                .allow_algorithm(jwt::algorithm::hs256{config_.jwt_secret})
                .with_issuer("wiser");
            verifier.verify(decoded);

            UserInfo user;
            user.username = decoded.get_subject();
            user.id = std::stoll(decoded.get_payload_claim("uid").as_string());
            user.role = roleFromString(decoded.get_payload_claim("role").as_string());
            return user;
        } catch (const std::exception& e) {
            spdlog::debug("JWT validation failed: {}", e.what());
            return std::nullopt;
        }
    }

    // ─── API Key ───

    std::string AuthManager::generateApiKey(int64_t user_id) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!db_) return {};

        std::string key = "wsk_" + generateRandomHex(32); // wiser key prefix

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "UPDATE users SET api_key = ? WHERE id = ?";
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return {};

        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, user_id);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return (rc == SQLITE_DONE) ? key : std::string{};
    }

    std::optional<UserInfo> AuthManager::authenticateApiKey(const std::string& api_key) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!db_ || api_key.empty()) return std::nullopt;

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT id, username, role FROM users WHERE api_key = ?";
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return std::nullopt;

        sqlite3_bind_text(stmt, 1, api_key.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        auto name_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        auto role_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (!name_ptr || !role_ptr) {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }
        UserInfo user;
        user.id = sqlite3_column_int64(stmt, 0);
        user.username = name_ptr;
        user.role = roleFromString(role_ptr);
        sqlite3_finalize(stmt);
        return user;
    }

    // ─── HTTP 请求认证 ───

    std::optional<UserInfo> AuthManager::authenticateRequest(const httplib::Request& req) {
        // 1. Check Authorization: Bearer <JWT>
        auto auth_header = req.get_header_value("Authorization");
        if (auth_header.size() > 7 && auth_header.substr(0, 7) == "Bearer ") {
            auto token = auth_header.substr(7);
            auto user = validateToken(token);
            if (user) return user;
        }

        // 2. Check X-API-Key header
        auto api_key = req.get_header_value("X-API-Key");
        if (!api_key.empty()) {
            return authenticateApiKey(api_key);
        }

        return std::nullopt;
    }

    // ─── 权限检查 ───

    bool AuthManager::hasPermission(const std::optional<UserInfo>& user, Role required) {
        if (!user) return false;
        // Admin >= Editor >= Viewer
        return static_cast<int>(user->role) >= static_cast<int>(required);
    }

    void AuthManager::setUnauthorized(httplib::Response& res, const std::string& message) {
        res.status = 401;
        res.set_content("{\"error\":\"" + message + "\"}", "application/json");
    }

    void AuthManager::setForbidden(httplib::Response& res, const std::string& message) {
        res.status = 403;
        res.set_content("{\"error\":\"" + message + "\"}", "application/json");
    }

} // namespace wiser::web
