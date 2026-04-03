/**
 * @file main.cpp
 * @brief 命令行入口：索引构建与查询检索
 */

#include "wiser/wiser_environment.h"
#include "wiser/utils.h"
#include "wiser/tsv_loader.h"
#include "wiser/json_loader.h"
#include "wiser/console.h"
#include "wiser/log_init.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <chrono>
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
    using namespace wiser::ansi;
    std::cout << bold << "Usage: " << reset << program_name << " [options] db_file\n\n"
              << bold << "Modes:" << reset << "\n"
              << green << "  Indexing  " << reset << ": -x <data_file> [-m N] [-t N] [-c METHOD]\n"
              << "              data_file supports: .xml (Wikipedia), .tsv, .json, .jsonl, .ndjson\n"
              << cyan << "  Searching " << reset << ": -q <query> [-s]\n"
              << dim << "  You can provide both -x and -q to index then search in one run.\n\n" << reset
              << bold << "Options:" << reset << "\n"
              << "  -h, --help               Show this help and exit\n"
              << "  -c <method>              Compression: " << yellow << "none" << reset << " | " << yellow << "golomb" << reset << "\n"
              << "  -x <data_file>           Import data file (auto-detect format)\n"
              << "  -q <query>               Search query (UTF-8)\n"
              << "  -m <max_count>           Max documents to index [-1 = unlimited]\n"
              << "  -t <threshold>           Buffer merge threshold [default: 2048]\n"
              << "  -s                       Enable phrase search\n\n"
              << bold << "Examples:" << reset << "\n"
              << dim << "  " << program_name << " -x data.xml -m 10000 -c golomb wiser.db\n"
              << "  " << program_name << " -x sample.tsv wiser.db\n"
              << "  " << program_name << " -q \"information retrieval\" wiser.db\n" << reset;
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
    // 启用 ANSI 颜色输出 (Windows)
    wiser::Console::enableAnsi();

    // 初始化日志（控制台+文件双 sink）
    wiser::initLogging({
        .level = spdlog::level::info,
        .log_file = "wiser.log"
    });

    // 解析参数
    std::string compress_method_str;
    std::string data_file;
    std::string query;
    bool show_help = false;
    wiser::Config config;

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

    // 检查最后一个参数是否是 help 标志
    if (argc >= 2) {
        std::string last_arg = argv[argc - 1];
        if (last_arg == "-h" || last_arg == "--help") {
            show_help = true;
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
        // ─── 启动横幅 ───
        using namespace wiser::ansi;
        std::cout << "\n"
                  << bold << bright_cyan
                  << "  ╦ ╦╦╔═╗╔═╗╦═╗\n"
                  << "  ║║║║╚═╗║╣ ╠╦╝\n"
                  << "  ╚╩╝╩╚═╝╚═╝╩╚═" << reset
                  << dim << "  v1.0 Full-Text Search Engine" << reset
                  << "\n" << std::endl;

        // 配置摘要
        auto cm = parseCompressMethod(compress_method_str);
        std::cout << "  " << dim << "Database   " << reset << bold << db_path << reset << "\n";
        if (!data_file.empty()) {
            std::cout << "  " << dim << "Data file  " << reset << bold << data_file << reset
                      << dim << "  (" << lowerExt(data_file) << ")" << reset << "\n";
        }
        std::cout << "  " << dim << "Compress   " << reset << compressMethodToString(cm) << "\n"
                  << "  " << dim << "Phrase     " << reset << (config.enable_phrase_search ? "on" : "off") << "\n"
                  << "  " << dim << "Buffer     " << reset << config.buffer_update_threshold << "\n"
                  << std::endl;

        // 初始化环境
        wiser::WiserEnvironment env;

        if (!env.initialize(db_path)) {
            spdlog::error("Failed to initialize Wiser environment.");
            return 3;
        }

        env.setCompressMethod(cm);
        env.setBufferUpdateThreshold(config.buffer_update_threshold);
        env.setPhraseSearchEnabled(config.enable_phrase_search);
        env.setMaxIndexCount(config.max_index_count);

        spdlog::info("Compress method: {}", compressMethodToString(cm));
        spdlog::info("Phrase search: {}, Buffer threshold: {}, Token length: {}",
                     config.enable_phrase_search ? "enabled" : "disabled",
                     config.buffer_update_threshold,
                     env.getTokenLength());

        // ─── 导入数据 ───
        if (!data_file.empty()) {
            if (config.max_index_count >= 0) {
                spdlog::info("Indexing up to: {} documents", config.max_index_count);
            }

            auto import_start = std::chrono::steady_clock::now();

            std::string ext = lowerExt(data_file);
            bool ok = false;
            if (ext == ".xml") {
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

            env.flushIndexBuffer();

            auto import_end = std::chrono::steady_clock::now();
            double import_sec = std::chrono::duration<double>(import_end - import_start).count();

            // 导入汇总
            auto count = env.getIndexedCount();
            std::cout << "\n"
                      << "  " << bold << bright_green << "\xe2\x9c\x93 Import complete" << reset << "\n"
                      << "  " << dim << "Documents  " << reset << count << "\n"
                      << "  " << dim << "Elapsed    " << reset << wiser::Console::formatDuration(import_sec) << "\n"
                      << "  " << dim << "Speed      " << reset
                      << static_cast<int>(count / (std::max)(import_sec, 0.001)) << " docs/sec\n"
                      << std::endl;
        }

        // ─── 搜索 ───
        if (!query.empty()) {
            std::cout << "  " << dim << wiser::Console::horizontalLine(50) << reset << "\n"
                      << "  " << bold << bright_magenta << "🔍 Query: " << reset
                      << bold << query << reset << "\n" << std::endl;

            auto search_start = std::chrono::steady_clock::now();
            env.getSearchEngine().printSearchResultBodies(query);
            auto search_end = std::chrono::steady_clock::now();
            double search_ms = std::chrono::duration<double, std::milli>(search_end - search_start).count();

            std::cout << "  " << dim << "Search completed in "
                      << wiser::Console::formatDuration(search_ms / 1000.0) << reset << "\n" << std::endl;
        }

        env.shutdown();
    } catch (const std::exception& e) {
        spdlog::error("Error: {}", e.what());
        return 5;
    }

    return 0;
}
