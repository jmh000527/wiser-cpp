/**
 * @file config_loader.h
 * @brief 配置文件加载器：支持 JSON 格式配置文件和环境变量覆盖。
 *
 * 优先级链（由高到低）：
 *   1. CLI 参数（代码直接设置）
 *   2. 环境变量（WISER_* 前缀）
 *   3. 配置文件（config.json）
 *   4. 硬编码默认值（Config 结构体初始值）
 */

#pragma once

#include "config.h"
#include <string>
#include <optional>

namespace wiser {

    /**
     * @brief 服务器配置（不属于索引配置，仅用于 web_server）
     */
    struct ServerConfig {
        std::string host = "0.0.0.0";
        int port = 54321;
        int worker_threads = 0;    ///< 0 = auto (hardware_concurrency)
        bool cors_enabled = true;
        std::string cors_origin = "*";
        int max_request_body_size = 100 * 1024 * 1024; ///< 100 MB
        int max_query_length = 1000;

        // ─── 认证配置 ───
        bool auth_enabled = true;                         ///< 是否启用 JWT 认证
        std::string jwt_secret = "wiser-default-secret-change-me"; ///< JWT 签名密钥
        int token_expiry_hours = 24;                       ///< JWT 过期时间（小时）
        bool allow_registration = true;                    ///< 是否允许公开注册

        // ─── 限流配置 ───
        bool rate_limit_enabled = false;                   ///< 是否启用速率限制
        double rate_limit_max_tokens = 60.0;               ///< 桶容量
        double rate_limit_refill_rate = 10.0;              ///< 每秒补充令牌数
    };

    /**
     * @brief 配置文件加载器
     */
    class ConfigLoader {
    public:
        /**
         * @brief 从 JSON 配置文件加载配置
         * @param filepath 配置文件路径
         * @param[out] config 索引/搜索配置
         * @param[out] server 服务器配置
         * @return 是否成功加载
         */
        static bool loadFromFile(const std::string& filepath,
                                 Config& config,
                                 ServerConfig& server);

        /**
         * @brief 从环境变量覆盖配置
         *
         * 支持的环境变量：
         *   WISER_DB_PATH        数据库路径
         *   WISER_PORT           监听端口
         *   WISER_HOST           监听地址
         *   WISER_TOKEN_LEN      N-gram 长度
         *   WISER_COMPRESS       压缩方式 (none/golomb)
         *   WISER_BM25_K1        BM25 k1 参数
         *   WISER_BM25_B         BM25 b 参数
         *   WISER_BUFFER_THRESHOLD 缓冲区阈值
         *   WISER_MAX_INDEX      最大索引数
         *   WISER_SCORING        评分方式 (bm25/tfidf)
         *   WISER_PHRASE_SEARCH  启用短语搜索 (true/false/1/0)
         *   WISER_CORS_ORIGIN    CORS 允许域
         *   WISER_MAX_QUERY_LEN  最大查询长度
         *   WISER_WORKERS        工作线程数
         *
         * @param[in,out] config 索引/搜索配置
         * @param[in,out] server 服务器配置
         */
        static void applyEnvironmentOverrides(Config& config, ServerConfig& server);

        /**
         * @brief 校验配置参数合法性
         * @param config 索引配置
         * @param server 服务器配置
         * @return 错误信息列表（空表示通过）
         */
        static std::vector<std::string> validate(const Config& config, const ServerConfig& server);

        /**
         * @brief 生成默认配置文件模板
         * @return JSON 格式的配置文件内容
         */
        static std::string generateDefaultConfig();

    private:
        static std::optional<std::string> getEnv(const char* name);
    };

} // namespace wiser
