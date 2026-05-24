/**
 * @file config_loader.cpp
 * @brief 配置文件加载与环境变量覆盖的实现。
 */

#include "wiser/config_loader.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace wiser {

    // ----------------------------------------------------------------
    // loadFromFile — 使用 nlohmann/json 安全解析
    // ----------------------------------------------------------------

    bool ConfigLoader::loadFromFile(const std::string& filepath,
                                     Config& config,
                                     ServerConfig& server) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            spdlog::warn("Config file not found: {}", filepath);
            return false;
        }

        spdlog::info("Loading config from: {}", filepath);

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(file, nullptr, true, true); // allow comments
        } catch (const nlohmann::json::parse_error& e) {
            spdlog::error("Config parse error: {}", e.what());
            return false;
        }

        // Index/search config
        if (j.contains("db_path") && j["db_path"].is_string())
            config.db_path = j["db_path"].get<std::string>();
        if (j.contains("token_len") && j["token_len"].is_number_integer())
            config.token_len = j["token_len"].get<int32_t>();
        if (j.contains("compress_method") && j["compress_method"].is_string()) {
            std::string s = j["compress_method"].get<std::string>();
            std::ranges::transform(s, s.begin(), ::tolower);
            config.compress_method = (s == "golomb") ? CompressMethod::GOLOMB : CompressMethod::NONE;
        }
        if (j.contains("buffer_update_threshold") && j["buffer_update_threshold"].is_number())
            config.buffer_update_threshold = j["buffer_update_threshold"].get<int32_t>();
        if (j.contains("max_index_count") && j["max_index_count"].is_number())
            config.max_index_count = j["max_index_count"].get<int32_t>();
        if (j.contains("enable_phrase_search") && j["enable_phrase_search"].is_boolean())
            config.enable_phrase_search = j["enable_phrase_search"].get<bool>();
        if (j.contains("scoring_method") && j["scoring_method"].is_string()) {
            std::string s = j["scoring_method"].get<std::string>();
            std::ranges::transform(s, s.begin(), ::tolower);
            config.scoring_method = (s == "tfidf" || s == "tf_idf" || s == "tf-idf")
                                    ? ScoringMethod::TF_IDF : ScoringMethod::BM25;
        }
        if (j.contains("bm25_k1") && j["bm25_k1"].is_number())
            config.bm25_k1 = j["bm25_k1"].get<double>();
        if (j.contains("bm25_b") && j["bm25_b"].is_number())
            config.bm25_b = j["bm25_b"].get<double>();
        if (j.contains("title_boost") && j["title_boost"].is_number())
            config.title_boost = j["title_boost"].get<double>();

        // Server config
        if (j.contains("host") && j["host"].is_string())
            server.host = j["host"].get<std::string>();
        if (j.contains("port") && j["port"].is_number_integer())
            server.port = j["port"].get<int>();
        if (j.contains("worker_threads") && j["worker_threads"].is_number_integer())
            server.worker_threads = j["worker_threads"].get<int>();
        if (j.contains("cors_enabled") && j["cors_enabled"].is_boolean())
            server.cors_enabled = j["cors_enabled"].get<bool>();
        if (j.contains("cors_origin") && j["cors_origin"].is_string())
            server.cors_origin = j["cors_origin"].get<std::string>();
        if (j.contains("max_request_body_size") && j["max_request_body_size"].is_number())
            server.max_request_body_size = j["max_request_body_size"].get<int>();
        if (j.contains("max_query_length") && j["max_query_length"].is_number())
            server.max_query_length = j["max_query_length"].get<int>();

        // Auth config
        if (j.contains("auth_enabled") && j["auth_enabled"].is_boolean())
            server.auth_enabled = j["auth_enabled"].get<bool>();
        if (j.contains("jwt_secret") && j["jwt_secret"].is_string())
            server.jwt_secret = j["jwt_secret"].get<std::string>();
        if (j.contains("token_expiry_hours") && j["token_expiry_hours"].is_number_integer())
            server.token_expiry_hours = j["token_expiry_hours"].get<int>();
        if (j.contains("allow_registration") && j["allow_registration"].is_boolean())
            server.allow_registration = j["allow_registration"].get<bool>();

        // Rate limiting config
        if (j.contains("rate_limit_enabled") && j["rate_limit_enabled"].is_boolean())
            server.rate_limit_enabled = j["rate_limit_enabled"].get<bool>();
        if (j.contains("rate_limit_max_tokens") && j["rate_limit_max_tokens"].is_number())
            server.rate_limit_max_tokens = j["rate_limit_max_tokens"].get<double>();
        if (j.contains("rate_limit_refill_rate") && j["rate_limit_refill_rate"].is_number())
            server.rate_limit_refill_rate = j["rate_limit_refill_rate"].get<double>();

        spdlog::info("Config loaded successfully");
        return true;
    }

    // ----------------------------------------------------------------
    // applyEnvironmentOverrides
    // ----------------------------------------------------------------

    void ConfigLoader::applyEnvironmentOverrides(Config& config, ServerConfig& server) {
        // Safe numeric parser with fallback and logging
        auto safeInt = [](const std::string& val, int fallback, const char* name) -> int {
            try { return std::stoi(val); }
            catch (const std::exception& e) {
                spdlog::warn("Invalid {} value '{}': {}, using default {}", name, val, e.what(), fallback);
                return fallback;
            }
        };
        auto safeDbl = [](const std::string& val, double fallback, const char* name) -> double {
            try { return std::stod(val); }
            catch (const std::exception& e) {
                spdlog::warn("Invalid {} value '{}': {}, using default {}", name, val, e.what(), fallback);
                return fallback;
            }
        };

        if (auto v = getEnv("WISER_DB_PATH"))
            config.db_path = v.value();
        if (auto v = getEnv("WISER_PORT"))
            server.port = safeInt(v.value(), server.port, "WISER_PORT");
        if (auto v = getEnv("WISER_HOST"))
            server.host = v.value();
        if (auto v = getEnv("WISER_TOKEN_LEN"))
            config.token_len = safeInt(v.value(), config.token_len, "WISER_TOKEN_LEN");
        if (auto v = getEnv("WISER_COMPRESS")) {
            std::string s = v.value();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            config.compress_method = (s == "golomb") ? CompressMethod::GOLOMB : CompressMethod::NONE;
        }
        if (auto v = getEnv("WISER_BM25_K1"))
            config.bm25_k1 = safeDbl(v.value(), config.bm25_k1, "WISER_BM25_K1");
        if (auto v = getEnv("WISER_BM25_B"))
            config.bm25_b = safeDbl(v.value(), config.bm25_b, "WISER_BM25_B");
        if (auto v = getEnv("WISER_TITLE_BOOST"))
            config.title_boost = safeDbl(v.value(), config.title_boost, "WISER_TITLE_BOOST");
        if (auto v = getEnv("WISER_BUFFER_THRESHOLD"))
            config.buffer_update_threshold = safeInt(v.value(), config.buffer_update_threshold, "WISER_BUFFER_THRESHOLD");
        if (auto v = getEnv("WISER_MAX_INDEX"))
            config.max_index_count = safeInt(v.value(), config.max_index_count, "WISER_MAX_INDEX");
        if (auto v = getEnv("WISER_SCORING")) {
            std::string s = v.value();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            config.scoring_method = (s == "tfidf" || s == "tf_idf") ? ScoringMethod::TF_IDF : ScoringMethod::BM25;
        }
        if (auto v = getEnv("WISER_PHRASE_SEARCH")) {
            std::string s = v.value();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            config.enable_phrase_search = (s == "true" || s == "1" || s == "yes");
        }
        if (auto v = getEnv("WISER_CORS_ORIGIN"))
            server.cors_origin = v.value();
        if (auto v = getEnv("WISER_MAX_QUERY_LEN"))
            server.max_query_length = safeInt(v.value(), server.max_query_length, "WISER_MAX_QUERY_LEN");
        if (auto v = getEnv("WISER_WORKERS"))
            server.worker_threads = safeInt(v.value(), server.worker_threads, "WISER_WORKERS");

        // Auth overrides
        if (auto v = getEnv("WISER_AUTH_ENABLED")) {
            std::string s = v.value();
            std::ranges::transform(s, s.begin(), ::tolower);
            server.auth_enabled = (s == "true" || s == "1" || s == "yes");
        }
        if (auto v = getEnv("WISER_JWT_SECRET"))
            server.jwt_secret = v.value();
        if (auto v = getEnv("WISER_TOKEN_EXPIRY_HOURS"))
            server.token_expiry_hours = std::stoi(v.value());
        if (auto v = getEnv("WISER_ALLOW_REGISTRATION")) {
            std::string s = v.value();
            std::ranges::transform(s, s.begin(), ::tolower);
            server.allow_registration = (s == "true" || s == "1" || s == "yes");
        }

        // Rate limiting overrides
        if (auto v = getEnv("WISER_RATE_LIMIT_ENABLED")) {
            std::string s = v.value();
            std::ranges::transform(s, s.begin(), ::tolower);
            server.rate_limit_enabled = (s == "true" || s == "1" || s == "yes");
        }
        if (auto v = getEnv("WISER_RATE_LIMIT_MAX_TOKENS"))
            server.rate_limit_max_tokens = std::stod(v.value());
        if (auto v = getEnv("WISER_RATE_LIMIT_REFILL_RATE"))
            server.rate_limit_refill_rate = std::stod(v.value());
    }

    // ----------------------------------------------------------------
    // validate
    // ----------------------------------------------------------------

    std::vector<std::string> ConfigLoader::validate(const Config& config, const ServerConfig& server) {
        std::vector<std::string> errors;

        if (config.token_len < 1 || config.token_len > 8)
            errors.push_back("token_len must be between 1 and 8, got " + std::to_string(config.token_len));
        if (config.bm25_k1 < 0.0 || config.bm25_k1 > 10.0)
            errors.push_back("bm25_k1 must be between 0.0 and 10.0, got " + std::to_string(config.bm25_k1));
        if (config.bm25_b < 0.0 || config.bm25_b > 1.0)
            errors.push_back("bm25_b must be between 0.0 and 1.0, got " + std::to_string(config.bm25_b));
        if (config.title_boost < 1.0 || config.title_boost > 10.0)
            errors.push_back("title_boost must be between 1.0 and 10.0, got " + std::to_string(config.title_boost));
        if (config.buffer_update_threshold < 1)
            errors.push_back("buffer_update_threshold must be >= 1, got " + std::to_string(config.buffer_update_threshold));

        if (server.port < 1 || server.port > 65535)
            errors.push_back("port must be between 1 and 65535, got " + std::to_string(server.port));
        if (server.max_query_length < 1 || server.max_query_length > 100000)
            errors.push_back("max_query_length must be between 1 and 100000, got " + std::to_string(server.max_query_length));
        if (server.worker_threads < 0 || server.worker_threads > 256)
            errors.push_back("worker_threads must be between 0 and 256, got " + std::to_string(server.worker_threads));

        return errors;
    }

    // ----------------------------------------------------------------
    // generateDefaultConfig
    // ----------------------------------------------------------------

    std::string ConfigLoader::generateDefaultConfig() {
        return R"({
    "db_path": "wiser.db",
    "token_len": 2,
    "compress_method": "none",
    "buffer_update_threshold": 2048,
    "max_index_count": -1,
    "enable_phrase_search": false,
    "scoring_method": "bm25",
    "bm25_k1": 1.2,
    "bm25_b": 0.75,
    "title_boost": 1.5,

    "host": "0.0.0.0",
    "port": 54321,
    "worker_threads": 0,
    "cors_enabled": true,
    "cors_origin": "*",
    "max_request_body_size": 104857600,
    "max_query_length": 1000,

    "auth_enabled": false,
    "jwt_secret": "CHANGE-ME-TO-A-RANDOM-SECRET",
    "token_expiry_hours": 24,
    "allow_registration": true,

    "rate_limit_enabled": false,
    "rate_limit_max_tokens": 60,
    "rate_limit_refill_rate": 10
}
)";
    }

    // ----------------------------------------------------------------
    // getEnv
    // ----------------------------------------------------------------

    std::optional<std::string> ConfigLoader::getEnv(const char* name) {
#ifdef _MSC_VER
        char* buf = nullptr;
        size_t sz = 0;
        if (_dupenv_s(&buf, &sz, name) == 0 && buf != nullptr) {
            std::string val(buf);
            free(buf);
            return val;
        }
        return std::nullopt;
#else
        const char* val = std::getenv(name);
        if (val != nullptr && val[0] != '\0') {
            return std::string(val);
        }
        return std::nullopt;
#endif
    }

} // namespace wiser
