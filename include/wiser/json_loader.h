#pragma once

#include <string>

namespace wiser {
class WiserEnvironment;

/**
 * JSON 加载器：支持两种格式
 * 1) JSON Lines (NDJSON)：每行一个对象 {"title":"...","body":"..."}
 * 2) JSON 数组：[{"title":"...","body":"..."}, ...]
 */
class JsonLoader {
public:
    explicit JsonLoader(WiserEnvironment* env) : env_(env) {}

    /**
     * 自动识别并加载 JSON 文件（先看首个非空字符：'[' 视为数组，否则按 JSON Lines 处理）
     */
    bool loadFromFile(const std::string& file_path);

    /** 按行 JSON (NDJSON) 导入 */
    bool loadFromJsonLines(const std::string& file_path);
    /** JSON 数组导入 */
    bool loadFromArrayFile(const std::string& file_path);

private:
    WiserEnvironment* env_;

    // 从一个 JSON 对象文本中提取 string 字段（处理常见转义）
    static bool extractStringField(const std::string& json_obj, const std::string& key, std::string& out);
    static bool parseObjectToTitleBody(const std::string& json_obj, std::string& title, std::string& body);
};
} // namespace wiser

