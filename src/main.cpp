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
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::string lowerExt(const std::string& path) {
    std::filesystem::path p(path);
    return toLower(p.extension().string());
}

void printUsage(const char* program_name) {
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

    std::string compress_method_str;
    std::string data_file; // 支持 .xml/.tsv/.json/.jsonl/.ndjson
    std::string query;
    int max_index_count = -1; // 不限制索引文档数量
    int buffer_threshold = 2048;
    bool enable_phrase_search = false;
    bool show_help = false;

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
                max_index_count = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                spdlog::error("Invalid value for -m: {}", argv[i]);
                return 1;
            }
        } else if (arg == "-t" && i + 1 < argc - 1) {
            try {
                buffer_threshold = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                spdlog::error("Invalid value for -t: {}", argv[i]);
                return 1;
            }
        } else if (arg == "-s") {
            enable_phrase_search = true;
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

        // 设置配置
        auto cm = parseCompressMethod(compress_method_str);
        env.setCompressMethod(cm);
        env.setBufferUpdateThreshold(buffer_threshold);
        env.setPhraseSearchEnabled(enable_phrase_search);
        // 让 -m 生效：设置本次运行的索引上限
        env.setMaxIndexCount(max_index_count);

        // 打印最终生效的关键参数（包含压缩方式字符串）
        spdlog::info("Compress method: {}", compressMethodToString(cm));
        spdlog::info("Phrase search: {}, Buffer threshold: {}, Token length: {}",
                     enable_phrase_search ? "enabled" : "disabled",
                     buffer_threshold,
                     env.getTokenLength());
        // 加载数据（根据后缀自动选择加载器）
        if (!data_file.empty()) {
            if (max_index_count >= 0) {
                spdlog::info("Indexing up to: {} documents", max_index_count);
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
