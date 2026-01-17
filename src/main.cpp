/**
 * @file main.cpp
 * @brief 命令行入口：索引构建与查询检索
 *
 * 主要功能：
 * - 解析命令行参数，选择数据加载器（XML/TSV/JSON/JSONL/NDJSON）
 * - 初始化 wiser::WiserEnvironment 并配置运行参数
 * - 构建索引、刷新倒排缓冲、执行查询并输出结果
 */

#include "wiser/wiser_environment.h"
#include "wiser/utils.h"
#include "wiser/tsv_loader.h"
#include "wiser/json_loader.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <spdlog/spdlog.h>

static const char* compressMethodToString(wiser::CompressMethod m) {
    // 将压缩枚举映射为可读字符串，用于日志输出
    switch (m) {
        case wiser::CompressMethod::NONE:
            return "none";
        case wiser::CompressMethod::GOLOMB:
            return "golomb";
        default:
            return "unknown";
    }
}

static std::string toLower(std::string s) {
    // ASCII 小写化（保持非 ASCII 字符不变）
    std::ranges::transform(s, s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::string lowerExt(const std::string& path) {
    std::filesystem::path p(path);
    return toLower(p.extension().string());
}

void printUsage(const char* program_name) {
    // 输出命令行帮助信息
    std::cout << std::format("usage: {} [options] db_file\n", program_name);
    std::cout << std::format("\n");
    std::cout << std::format("modes:");
    std::cout << std::format("  Indexing : -x <data_file> [-m N] [-t N] [-c METHOD]\n");
    std::cout << std::format("              data_file supports: .xml (Wikipedia XML), .tsv, .json, .jsonl, .ndjson\n");
    std::cout << std::format("  Searching: -q <query> [-s]\n");
    std::cout << std::format("  You can provide both -x and -q to index then search in one run.\n");
    std::cout << std::format("\n");
    std::cout << std::format("options:\n");
    std::cout << std::format("  -h, --help                   : show this help and exit\n");
    std::cout << std::format("  -c <compress_method>         : postings list compression [default: none]\n");
    std::cout << std::format("                                 values: none | golomb\n");
    std::cout <<
            std::format("  -x <data_file>               : path to data file for indexing; loader is chosen by extension\n");
    std::cout <<
            std::format("                                 .xml -> Wikipedia XML, .tsv -> TSV (title[TAB]body), .json/.jsonl/.ndjson -> JSON\n");
    std::cout << std::format("  -q <search_query>            : query string (UTF-8) for search\n");
    std::cout << std::format("  -m <max_index_count>         : max docs to index [-1 = no limit, default: -1]\n");
    std::cout <<
            std::format("  -t <buffer_threshold>        : inverted index buffer merge threshold [default: 2048]\n");
    std::cout << std::format("  -s                           : enable phrase search (by default it's disabled)\n");
    std::cout << std::format("\n");
    std::cout << std::format("examples:\n");
    std::cout << std::format("  {} -x enwiki-latest-pages-articles.xml -m 10000 -c golomb data/wiser.db\n",
                             program_name);
    std::cout << std::format("  {} -x sample_dataset.tsv data/wiser.db\n", program_name);
    std::cout << std::format("  {} -x sample.jsonl data/wiser.db\n", program_name);
    std::cout << std::format("  {} -q \"information retrieval\" data/wiser.db\n", program_name);
}

wiser::CompressMethod parseCompressMethod(const std::string& method_str) {
    // 将字符串解析为压缩方法；未知值会退回 NONE 并记录错误
    if (method_str.empty() || method_str == "none") {
        return wiser::CompressMethod::NONE;
    } else if (method_str == "golomb") {
        return wiser::CompressMethod::GOLOMB;
    } else {
        spdlog::error("Invalid compress method({}). Using none instead.", method_str);
        return wiser::CompressMethod::NONE;
    }
}

int main(int argc, char* argv[]) {
    // 初始化spdlog
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");

    // 解析参数所需的临时变量
    std::string compress_method_str;
    std::string data_file; // 支持 .xml/.tsv/.json/.jsonl/.ndjson
    std::string query;
    bool show_help = false;
    
    // 使用 Config 类统一管理默认参数配置
    wiser::Config config;
    // 注意：config 中的默认值已经在 config.h 中定义，这里不需要重复设置
    // token_len = 2, buffer_update_threshold = 2048, enable_phrase_search = false, etc.

    // 解析命令行参数（现代 C++ 风格）
    for (int i = 1; i < argc - 1; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            show_help = true;
        } else if (arg == "-c" && i + 1 < argc - 1) {
            compress_method_str = toLower(argv[++i]);
        } else if (arg == "-x" && i + 1 < argc - 1) {
            data_file = argv[++i];
        } else if (arg == "-q" && i + 1 < argc - 1) {
            query = argv[++i];
        } else if (arg == "-m" && i + 1 < argc - 1) {
            try {
                config.max_index_count = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                spdlog::error("Invalid value for -m: {}", argv[i]);
                return 1;
            }
        } else if (arg == "-t" && i + 1 < argc - 1) {
            try {
                config.buffer_update_threshold = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                spdlog::error("Invalid value for -t: {}", argv[i]);
                return 1;
            }
        } else if (arg == "-s") {
            config.enable_phrase_search = true;
        } else {
            spdlog::error("Unknown option: {}. Use -h for help.", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    if (show_help || argc < 2) {
        printUsage(argv[0]);
        return show_help ? 0 : 1;
    }

    std::string db_path = argv[argc - 1];

    // 检查构建索引时数据库是否已存在
    if (!data_file.empty() && std::filesystem::exists(db_path)) {
        spdlog::error("{} already exists.", db_path);
        return 2;
    }

    try {
        // 初始化Wiser环境
        wiser::WiserEnvironment env;

        if (!env.initialize(db_path)) {
            spdlog::error("Failed to initialize Wiser environment.");
            return 3;
        }

        // 设置配置 - 使用 Config 对象统一设置
        auto cm = parseCompressMethod(compress_method_str);
        env.setCompressMethod(cm);
        env.setBufferUpdateThreshold(config.buffer_update_threshold);
        env.setPhraseSearchEnabled(config.enable_phrase_search);
        // 让 -m 生效：设置本次运行的索引上限
        env.setMaxIndexCount(config.max_index_count);

        // 打印最终生效的关键参数（包含压缩方式字符串）
        spdlog::info("Compress method: {}", compressMethodToString(cm));
        spdlog::info("Phrase search: {}, Buffer threshold: {}, Token length: {}",
                     config.enable_phrase_search ? "enabled" : "disabled",
                     config.buffer_update_threshold,
                     env.getTokenLength());
        // 加载数据（根据后缀自动选择加载器）
        if (!data_file.empty()) {
            if (config.max_index_count >= 0) {
                spdlog::info("Indexing up to: {} documents", config.max_index_count);
            }

            std::string ext = lowerExt(data_file);
            bool ok = false;
            if (ext == ".xml") {
                // Wikipedia XML
                ok = env.getWikiLoader().loadFromFile(data_file);
            } else if (ext == ".tsv") {
                wiser::TsvLoader tsv(&env);
                ok = tsv.loadFromFile(data_file, /*has_header=*/true);
            } else if (ext == ".json" || ext == ".jsonl" || ext == ".ndjson") {
                wiser::JsonLoader jl(&env);
                ok = jl.loadFromFile(data_file);
            } else {
                spdlog::error("Unsupported data file extension: {}", ext);
                ok = false;
            }

            if (!ok) {
                spdlog::error("Failed to load data file: {}", data_file);
                return 4;
            }

            // 刷新缓冲区（确保落库）
            env.flushIndexBuffer();

            spdlog::info("Data loaded successfully.");
            spdlog::info("Total indexed documents: {}", env.getIndexedCount());
        }

        // 执行搜索
        if (!query.empty()) {
            std::cout << "===================== Search Results =======================" << std::endl;
            std::cout << "Query: " << query << std::endl;
            env.getSearchEngine().printSearchResultBodies(query);
        }

        // 关闭环境
        env.shutdown();
    } catch (const std::exception& e) {
        spdlog::error("Error: {}", e.what());
        return 5;
    }

    return 0;
}
