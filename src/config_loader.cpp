/**
 * @file config_loader.cpp
 * @brief 配置文件加载与环境变量覆盖的实现。
 */

#include "wiser/config_loader.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace wiser {

    // ----------------------------------------------------------------
    // 简易 JSON 值提取（避免引入第三方 JSON 库）
    // ----------------------------------------------------------------

    namespace {
        // 跳过空白
        void skipWhitespace(const std::string& s, size_t& pos) {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
                ++pos;
        }

        // 提取带引号的字符串值
        std::optional<std::string> extractString(const std::string& s, size_t& pos) {
            skipWhitespace(s, pos);
            if (pos >= s.size() || s[pos] != '"') return std::nullopt;
            ++pos;
            std::string result;
            while (pos < s.size() && s[pos] != '"') {
                if (s[pos] == '\\' && pos + 1 < s.size()) {
                    ++pos;
                    switch (s[pos]) {
                        case '"': result += '"'; break;
                        case '\\': result += '\\'; break;
                        case 'n': result += '\n'; break;
                        case 't': result += '\t'; break;
                        default: result += s[pos]; break;
                    }
                } else {
                    result += s[pos];
                }
                ++pos;
            }
            if (pos < s.size()) ++pos; // skip closing "
            return result;
        }

        // 在 JSON 对象中查找字符串值
        std::optional<std::string> findStringValue(const std::string& json, const std::string& key) {
            std::string search = "\"" + key + "\"";
            auto pos = json.find(search);
            if (pos == std::string::npos) return std::nullopt;
            pos += search.size();
            // skip : and whitespace
            while (pos < json.size() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\t'))
                ++pos;
            return extractString(json, pos);
        }

        // 在 JSON 对象中查找数值
        std::optional<double> findNumberValue(const std::string& json, const std::string& key) {
            std::string search = "\"" + key + "\"";
            auto pos = json.find(search);
            if (pos == std::string::npos) return std::nullopt;
            pos += search.size();
            while (pos < json.size() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\t'))
                ++pos;
            if (pos >= json.size()) return std::nullopt;

            // Check for true/false
            if (json.substr(pos, 4) == "true") return 1.0;
            if (json.substr(pos, 5) == "false") return 0.0;

            size_t end = pos;
            while (end < json.size() && (std::isdigit(json[end]) || json[end] == '.' || json[end] == '-' || json[end] == '+'))
                ++end;
            if (end == pos) return std::nullopt;
            try {
                return std::stod(json.substr(pos, end - pos));
            } catch (...) {
                return std::nullopt;
            }
        }

        // 在 JSON 对象中查找布尔值
        std::optional<bool> findBoolValue(const std::string& json, const std::string& key) {
            auto val = findNumberValue(json, key);
            if (val.has_value()) return val.value() != 0.0;
            auto sval = findStringValue(json, key);
            if (sval.has_value()) {
                std::string s = sval.value();
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                return s == "true" || s == "1" || s == "yes";
            }
            return std::nullopt;
        }
    } // anonymous namespace

    // ----------------------------------------------------------------
    // loadFromFile
    // ----------------------------------------------------------------

    bool ConfigLoader::loadFromFile(const std::string& filepath,
                                     Config& config,
                                     ServerConfig& server) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            spdlog::warn("Config file not found: {}", filepath);
            return false;
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        std::string json = ss.str();

        spdlog::info("Loading config from: {}", filepath);

        // Index/search config
        if (auto v = findStringValue(json, "db_path"))
            config.db_path = v.value();
        if (auto v = findNumberValue(json, "token_len"))
            config.token_len = static_cast<int32_t>(v.value());
        if (auto v = findStringValue(json, "compress_method")) {
            std::string s = v.value();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            if (s == "golomb") config.compress_method = CompressMethod::GOLOMB;
            else config.compress_method = CompressMethod::NONE;
        }
        if (auto v = findNumberValue(json, "buffer_update_threshold"))
            config.buffer_update_threshold = static_cast<int32_t>(v.value());
        if (auto v = findNumberValue(json, "max_index_count"))
            config.max_index_count = static_cast<int32_t>(v.value());
        if (auto v = findBoolValue(json, "enable_phrase_search"))
            config.enable_phrase_search = v.value();
        if (auto v = findStringValue(json, "scoring_method")) {
            std::string s = v.value();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            if (s == "tfidf" || s == "tf_idf" || s == "tf-idf")
                config.scoring_method = ScoringMethod::TF_IDF;
            else
                config.scoring_method = ScoringMethod::BM25;
        }
        if (auto v = findNumberValue(json, "bm25_k1"))
            config.bm25_k1 = v.value();
        if (auto v = findNumberValue(json, "bm25_b"))
            config.bm25_b = v.value();
        if (auto v = findNumberValue(json, "title_boost"))
            config.title_boost = v.value();

        // Server config
        if (auto v = findStringValue(json, "host"))
            server.host = v.value();
        if (auto v = findNumberValue(json, "port"))
            server.port = static_cast<int>(v.value());
        if (auto v = findNumberValue(json, "worker_threads"))
            server.worker_threads = static_cast<int>(v.value());
        if (auto v = findBoolValue(json, "cors_enabled"))
            server.cors_enabled = v.value();
        if (auto v = findStringValue(json, "cors_origin"))
            server.cors_origin = v.value();
        if (auto v = findNumberValue(json, "max_request_body_size"))
            server.max_request_body_size = static_cast<int>(v.value());
        if (auto v = findNumberValue(json, "max_query_length"))
            server.max_query_length = static_cast<int>(v.value());

        spdlog::info("Config loaded successfully");
        return true;
    }

    // ----------------------------------------------------------------
    // applyEnvironmentOverrides
    // ----------------------------------------------------------------

    void ConfigLoader::applyEnvironmentOverrides(Config& config, ServerConfig& server) {
        if (auto v = getEnv("WISER_DB_PATH"))
            config.db_path = v.value();
        if (auto v = getEnv("WISER_PORT"))
            server.port = std::stoi(v.value());
        if (auto v = getEnv("WISER_HOST"))
            server.host = v.value();
        if (auto v = getEnv("WISER_TOKEN_LEN"))
            config.token_len = std::stoi(v.value());
        if (auto v = getEnv("WISER_COMPRESS")) {
            std::string s = v.value();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            config.compress_method = (s == "golomb") ? CompressMethod::GOLOMB : CompressMethod::NONE;
        }
        if (auto v = getEnv("WISER_BM25_K1"))
            config.bm25_k1 = std::stod(v.value());
        if (auto v = getEnv("WISER_BM25_B"))
            config.bm25_b = std::stod(v.value());
        if (auto v = getEnv("WISER_TITLE_BOOST"))
            config.title_boost = std::stod(v.value());
        if (auto v = getEnv("WISER_BUFFER_THRESHOLD"))
            config.buffer_update_threshold = std::stoi(v.value());
        if (auto v = getEnv("WISER_MAX_INDEX"))
            config.max_index_count = std::stoi(v.value());
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
            server.max_query_length = std::stoi(v.value());
        if (auto v = getEnv("WISER_WORKERS"))
            server.worker_threads = std::stoi(v.value());
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
    "max_query_length": 1000
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
