/**
 * @file config.h
 * @brief 系统配置结构体与相关枚举定义。
 */

#pragma once

#include <string>
#include <cstdint>
#include "types.h"

namespace wiser {

    /**
     * @brief 评分方法枚举
     */
    enum class ScoringMethod {
        TF_IDF, ///< 传统的 TF-IDF 算法
        BM25    ///< BM25 概率相关性模型（默认）
    };

    /**
     * @brief 系统配置结构体
     *
     * 集中管理搜索引擎的所有运行时配置参数，包括索引构建、
     * 存储路径以及检索评分策略等。
     */
    struct Config {
        // =========================================================
        // 核心路径配置
        // =========================================================
        
        /** 
         * @brief 数据库文件存储路径 
         */
        std::string db_path;

        // =========================================================
        // 索引/持久化配置 (Index-Critical Settings)
        // 注意：修改这些参数通常需要重新构建索引，因为它们决定了存储结构。
        // =========================================================

        /** 
         * @brief N-gram 分词长度（默认为 2，即 Bi-gram） 
         * @note 改变此值需要重建索引。
         */
        std::int32_t token_len = 2; // N-Gram default

        /** 
         * @brief 倒排列表压缩方式（默认为无压缩） 
         * @note 改变此值需要重建索引。
         */
        CompressMethod compress_method = CompressMethod::NONE;

        // =========================================================
        // 运行时/调优配置 (Runtime Settings)
        // 注意：这些参数可以在运行时修改，不需要重建索引。
        // =========================================================

        // --- 索引过程控制 ---

        /** 
         * @brief 内存倒排缓冲区刷新阈值（Token 数量），达到该值触发落盘 
         */
        std::int32_t buffer_update_threshold = 2048;

        /** 
         * @brief 最大索引文档数量限制（-1 表示不限制）
         * 
         * 用于测试或控制库大小。
         */
        std::int32_t max_index_count = -1; // -1 = Unlimited

        // --- 搜索与评分控制 ---

        /** 
         * @brief 是否启用短语搜索（位置相邻校验） 
         */
        bool enable_phrase_search = false;

        /** 
         * @brief 检索评分算法选择 
         */
        ScoringMethod scoring_method = ScoringMethod::BM25; // Default to BM25

        // Tuning Parameters (BM25)
        /** 
         * @brief BM25 参数 k1：控制词频饱和度（通常 1.2 ~ 2.0） 
         */
        double bm25_k1 = 1.2;

        /** 
         * @brief BM25 参数 b：控制文档长度归一化力度（0 ~ 1，0.75 为经典值） 
         */
        double bm25_b = 0.75;
    };

} // namespace wiser
