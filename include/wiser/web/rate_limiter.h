/**
 * @file rate_limiter.h
 * @brief IP 级令牌桶速率限制器（线程安全）。
 *
 * 每个 IP 维护独立的令牌桶：
 * - 桶满时有 max_tokens 个令牌
 * - 每秒补充 refill_rate 个令牌
 * - 每次请求消耗 1 个令牌
 * - 令牌不足时拒绝请求（返回 false）
 *
 * 定期清理过期桶以防止内存增长。
 */

#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace wiser::web {

    struct RateLimiterConfig {
        double max_tokens = 60.0;       ///< 桶容量（最大突发量）
        double refill_rate = 10.0;      ///< 每秒补充令牌数
        int cleanup_interval_sec = 60;  ///< 过期桶清理间隔（秒）
        int bucket_ttl_sec = 300;       ///< 桶不活跃后过期时间（秒）
        bool enabled = false;           ///< 是否启用限流
    };

    class RateLimiter {
    public:
        explicit RateLimiter(const RateLimiterConfig& cfg = {})
            : config_(cfg)
            , last_cleanup_(std::chrono::steady_clock::now()) {}

        /**
         * @brief 判断是否允许来自该 IP 的请求
         * @param ip 客户端 IP 地址
         * @return true 允许，false 限流（应返回 429）
         */
        bool allowRequest(const std::string& ip) {
            if (!config_.enabled) return true;

            std::lock_guard<std::mutex> lock(mu_);
            auto now = std::chrono::steady_clock::now();

            // 定期清理过期桶
            auto since_cleanup = std::chrono::duration<double>(now - last_cleanup_).count();
            if (since_cleanup > config_.cleanup_interval_sec) {
                cleanupExpired(now);
                last_cleanup_ = now;
            }

            auto it = buckets_.find(ip);
            if (it == buckets_.end()) {
                // 新 IP：创建满桶，消耗 1 个令牌
                buckets_.emplace(ip, Bucket{config_.max_tokens - 1.0, now});
                return true;
            }

            auto& bucket = it->second;
            // 根据时间差补充令牌
            double elapsed = std::chrono::duration<double>(now - bucket.last_refill).count();
            bucket.tokens = std::min(config_.max_tokens,
                                     bucket.tokens + elapsed * config_.refill_rate);
            bucket.last_refill = now;

            if (bucket.tokens >= 1.0) {
                bucket.tokens -= 1.0;
                return true;
            }
            return false; // 令牌不足
        }

        /** @brief 获取指定 IP 的剩余令牌数（调试/header 用） */
        double remainingTokens(const std::string& ip) {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = buckets_.find(ip);
            if (it == buckets_.end()) return config_.max_tokens;
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - it->second.last_refill).count();
            return std::min(config_.max_tokens,
                            it->second.tokens + elapsed * config_.refill_rate);
        }

        /** @brief 获取配置（只读） */
        const RateLimiterConfig& getConfig() const { return config_; }

    private:
        struct Bucket {
            double tokens;
            std::chrono::steady_clock::time_point last_refill;
        };

        void cleanupExpired(std::chrono::steady_clock::time_point now) {
            for (auto it = buckets_.begin(); it != buckets_.end(); ) {
                double idle = std::chrono::duration<double>(now - it->second.last_refill).count();
                if (idle > config_.bucket_ttl_sec) {
                    it = buckets_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        RateLimiterConfig config_;
        std::mutex mu_;
        std::unordered_map<std::string, Bucket> buckets_;
        std::chrono::steady_clock::time_point last_cleanup_;
    };

} // namespace wiser::web
