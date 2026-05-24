/**
 * @file auth.h
 * @brief JWT 身份验证与用户管理模块。
 *
 * 功能：
 * - 用户注册/登录（SHA-256 + salt 密码哈希）
 * - JWT 令牌签发与验证（HS256）
 * - API Key 认证
 * - 角色权限检查（admin / editor / viewer）
 * - 从 HTTP 请求中提取认证信息
 *
 * 安全设计：
 * - 密码使用 SHA-256 迭代 10000 次 + 随机 salt
 * - JWT 使用可配置密钥签名
 * - 支持 Bearer token 和 X-API-Key 两种认证方式
 */

#pragma once

#include <string>
#include <optional>
#include <vector>
#include <mutex>

struct sqlite3;

namespace httplib {
    struct Request;
    struct Response;
}

namespace wiser::web {

    /** @brief 用户角色枚举 */
    enum class Role {
        Viewer,  ///< 只读搜索
        Editor,  ///< 可导入/添加文档
        Admin    ///< 完全控制
    };

    /** @brief 将角色转换为字符串 */
    inline const char* roleToString(Role r) {
        switch (r) {
            case Role::Admin:  return "admin";
            case Role::Editor: return "editor";
            case Role::Viewer: return "viewer";
            default:           return "viewer";
        }
    }

    /** @brief 从字符串解析角色 */
    inline Role roleFromString(const std::string& s) {
        if (s == "admin")  return Role::Admin;
        if (s == "editor") return Role::Editor;
        return Role::Viewer;
    }

    /** @brief 认证用户信息 */
    struct UserInfo {
        int64_t id = 0;
        std::string username;
        Role role = Role::Viewer;
    };

    /** @brief 认证模块配置 */
    struct AuthConfig {
        bool enabled = false;                        ///< 是否启用认证
        std::string jwt_secret = "wiser-default-secret-change-me"; ///< JWT 签名密钥
        int token_expiry_hours = 24;                 ///< JWT 过期时间（小时）
        bool allow_registration = true;              ///< 是否允许公开注册
        Role default_role = Role::Viewer;             ///< 新用户默认角色
    };

    /**
     * @brief 认证管理器
     *
     * 管理用户注册、登录、JWT 签发与验证。
     * 使用已有的 SQLite 数据库存储用户表。
     */
    class AuthManager {
    public:
        explicit AuthManager(const AuthConfig& config = {});
        ~AuthManager() = default;

        /**
         * @brief 初始化用户表（在数据库打开后调用）
         * @param db SQLite 数据库句柄
         * @return 成功返回 true
         */
        bool initialize(sqlite3* db);

        /**
         * @brief 注册新用户
         * @param username 用户名（3-64 字符）
         * @param password 密码（6-128 字符）
         * @param role 用户角色
         * @return 成功返回用户信息，失败返回 nullopt（用户已存在等）
         */
        std::optional<UserInfo> registerUser(const std::string& username,
                                             const std::string& password,
                                             Role role);

        /**
         * @brief 用户登录验证
         * @param username 用户名
         * @param password 密码
         * @return 验证通过返回用户信息，否则返回 nullopt
         */
        std::optional<UserInfo> authenticateUser(const std::string& username,
                                                 const std::string& password);

        /**
         * @brief 生成 JWT 令牌
         * @param user 用户信息
         * @return JWT 字符串
         */
        std::string generateToken(const UserInfo& user);

        /**
         * @brief 验证 JWT 令牌
         * @param token JWT 字符串
         * @return 验证通过返回用户信息，否则返回 nullopt
         */
        std::optional<UserInfo> validateToken(const std::string& token);

        /**
         * @brief 生成 API Key
         * @param user_id 用户 ID
         * @return API Key 字符串
         */
        std::string generateApiKey(int64_t user_id);

        /**
         * @brief 通过 API Key 认证
         * @param api_key API Key
         * @return 验证通过返回用户信息，否则返回 nullopt
         */
        std::optional<UserInfo> authenticateApiKey(const std::string& api_key);

        /**
         * @brief 从 HTTP 请求中提取并验证认证信息
         *
         * 优先级：
         *   1. Authorization: Bearer <JWT>
         *   2. X-API-Key: <key>
         *
         * @param req HTTP 请求
         * @return 认证成功返回用户信息，否则返回 nullopt
         */
        std::optional<UserInfo> authenticateRequest(const httplib::Request& req);

        /**
         * @brief 检查用户是否有足够权限
         * @param user 用户信息（可为空表示未认证）
         * @param required 所需最低角色
         * @return true 表示有权限
         */
        static bool hasPermission(const std::optional<UserInfo>& user, Role required);

        /**
         * @brief 设置 401 未授权响应
         */
        static void setUnauthorized(httplib::Response& res, const std::string& message = "Unauthorized");

        /**
         * @brief 设置 403 无权限响应
         */
        static void setForbidden(httplib::Response& res, const std::string& message = "Forbidden");

        /** @brief 获取配置 */
        const AuthConfig& config() const { return config_; }

        /** @brief 是否已初始化 */
        bool isInitialized() const { return db_ != nullptr; }

    private:
        // SHA-256 哈希工具
        static std::string sha256(const std::string& input);
        static std::string hashPassword(const std::string& password, const std::string& salt);
        static bool verifyPassword(const std::string& password, const std::string& stored_hash);
        static std::string generateSalt();
        static std::string generateRandomHex(int bytes);

        AuthConfig config_;
        sqlite3* db_ = nullptr;
        mutable std::mutex mu_;
    };

} // namespace wiser::web
