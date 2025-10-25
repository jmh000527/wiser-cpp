#include "wiser/wiser.h"
#include <filesystem>
#include <iostream>

int main() {
    using namespace wiser;
    std::cout << "=== Loader Demo (TSV + JSON) ===" << std::endl;

    // 工作目录建议为构建输出目录(bin)，便于访问示例数据
    std::string db_path = "loader_demo.db";
    if (std::filesystem::exists(db_path)) {
        std::filesystem::remove(db_path);
    }

    WiserEnvironment env;
    if (!env.initialize(db_path)) {
        std::cerr << "Failed to initialize environment" << std::endl;
        return 1;
    }

    // 可选：配置参数
    env.setBufferUpdateThreshold(1024);
    env.setPhraseSearchEnabled(false);
    env.setMaxIndexCount(-1);

    if (env.getMaxIndexCount() >= 0) {
        wiser::Utils::printInfo("Indexing up to: {} documents\n", env.getMaxIndexCount());
    }

    // 1) 从 TSV 加载：第一行为表头
    TsvLoader tsv(&env);
    // tsv.loadFromFile("../data/sample_dataset.tsv", /*has_header=*/true);

    // 2) 从 JSON 加载：支持 JSON Lines 与 JSON 数组
    JsonLoader jloader(&env);
    // jloader.loadFromFile("../data/sample.jsonl");
    // jloader.loadFromFile("../data/sample_array.json");

    // 刷新缓冲区（重要：少量文档未达阈值不会自动落库）
    // env.flushIndexBuffer();

    // 3) 测试JSON加载性能
    // JsonLoader jloader(&env);
    jloader.loadFromFile("../data/sample_array_test.json");

    // 简单查询演示
    auto& se = env.getSearchEngine();
    auto query = "信息";
    se.search(query);
    se.printSearchResultBodies(query);
    // se.printAllDocumentBodies();
    se.printInvertedIndexForQuery(query);

    env.shutdown();
    std::cout << "Done. DB: " << db_path << std::endl;
    return 0;
}
