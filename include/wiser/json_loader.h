#pragma once

/**
 * @file json_loader.h
 * @brief JSON 文档加载器：支持 NDJSON 与 JSON 数组两种格式。
 *
 * JSON 加载器支持两种格式：
 * 1. JSON Lines (NDJSON)：每行一个对象 {"title":"...","body":"..."}
 * 2. JSON 数组：[{"title":"...","body":"..."}, ...]
 */

#pragma once

#include <string>

namespace wiser {
    class WiserEnvironment;

    /**
     * @brief JSON 加载器类
     */
    class JsonLoader {
    public:
        explicit JsonLoader(WiserEnvironment* env) : env_(env) {}

        /**
         * @brief 自动识别并加载 JSON 文件
         * 
         * 根据首个非空字符判断格式：'[' 视为数组，否则按 JSON Lines 处理。
         * @param file_path 文件路径
         * @return 加载成功返回 true，否则返回 false
         */
        bool loadFromFile(const std::string& file_path);

        /** 
         * @brief 按行 JSON (NDJSON) 导入
         * @param file_path 文件路径
         * @return 加载成功返回 true
         */
        bool loadFromJsonLines(const std::string& file_path);

        /** 
         * @brief JSON 数组导入
         * @param file_path 文件路径
         * @return 加载成功返回 true
         */
        bool loadFromArrayFile(const std::string& file_path);

    private:
        WiserEnvironment* env_;

        // 从一个 JSON 对象文本中提取 string 字段（处理常见转义）
        static bool extractStringField(const std::string& json_obj, const std::string& key, std::string& out);
        static bool parseObjectToTitleBody(const std::string& json_obj, std::string& title, std::string& body);
    };
} // namespace wiser
