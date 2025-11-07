#include "wiser/wiser.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <spdlog/spdlog.h>

int main() {
    // 初始化spdlog
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");

    spdlog::info("=== Wiser-CPP Search Engine Demo ===");

    try {
        // 创建搜索引擎环境
        wiser::WiserEnvironment env;

        // 初始化数据库前，删除旧的 demo.db，保证干净环境
        std::string db_path = "demo.db";
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove(db_path);
        }

        // 初始化数据库
        if (!env.initialize(db_path)) {
            std::cerr << "Failed to initialize database: " << db_path << std::endl;
            return 1;
        }

        std::cout << "Database initialized: " << db_path << std::endl;

        // 添加一些示例文档
        std::cout << "Adding sample documents..." << std::endl;

        env.addDocument("Artificial Intelligence",
                        "Artificial intelligence (AI) is intelligence demonstrated by machines, "
                        "in contrast to the natural intelligence displayed by humans and animals. "
                        "Leading AI textbooks define the field as the study of intelligent agents.");

        env.addDocument("Machine Learning",
                        "Machine learning (ML) is a type of artificial intelligence (AI) that "
                        "allows software applications to become more accurate at predicting outcomes "
                        "without being explicitly programmed to do so.");

        env.addDocument("Deep Learning",
                        "Deep learning is part of a broader family of machine learning methods "
                        "based on artificial neural networks with representation learning. "
                        "Learning can be supervised, semi-supervised or unsupervised.");

        env.addDocument("Natural Language Processing",
                        "Natural language processing (NLP) is a subfield of linguistics, "
                        "computer science, and artificial intelligence concerned with the "
                        "interactions between computers and human language.");

        // 强制刷新缓冲区
        env.addDocument("", "");

        std::cout << "Added " << env.getIndexedCount() << " documents" << std::endl;

        // 执行搜索
        std::cout << "=== Search Results ===" << std::endl;

        std::vector<std::string> queries = {
                    "artificial intelligence",
                    "machine learning",
                    "deep learning",
                    "natural language",
                    "semi"
                };

        for (const auto& query: queries) {
            std::cout << "Searching for: \"" << query << "\"" << std::endl;
            std::cout << "----------------------------------------" << std::endl;
            // 按需打印倒排索引结构（调试用）
            env.getSearchEngine().printInvertedIndexForQuery(query);
            // 执行搜索
            env.getSearchEngine().search(query);
        }

        // 关闭环境
        env.shutdown();

        std::cout << "Demo completed successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
