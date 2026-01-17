#pragma once

/**
 * @file tsv_loader.h
 * @brief 从 TSV 文件 (title[TAB]body) 批量导入文档。
 */

#pragma once

#include <string>

namespace wiser {
    class WiserEnvironment;

    /**
     * @brief TSV 加载器类
     * 
     * 从制表符分隔文件 (TSV) 读取文档，格式要求：title[TAB]body。
     */
    class TsvLoader {
    public:
        explicit TsvLoader(WiserEnvironment* env)
            : env_(env) {}

        /**
         * @brief 从 TSV 文件载入文档
         * @param file_path 文件路径
         * @param has_header 首行是否为表头（若是则跳过）
         * @return 成功返回 true，失败返回 false
         */
        bool loadFromFile(const std::string& file_path, bool has_header = true);

    private:
        WiserEnvironment* env_;
    };
} // namespace wiser
