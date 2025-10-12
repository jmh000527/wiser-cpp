#pragma once

#include <string>

namespace wiser {
class WiserEnvironment;

/**
 * TSV 加载器：从制表符分隔文件读取文档，格式：title[TAB]body
 */
class TsvLoader {
public:
    explicit TsvLoader(WiserEnvironment* env) : env_(env) {}

    /**
     * 从TSV文件载入文档
     * @param file_path 文件路径
     * @param has_header 首行是否为表头（若是则跳过）
     * @return 成功返回已处理行数>=0；失败返回false
     */
    bool loadFromFile(const std::string& file_path, bool has_header = false);

private:
    WiserEnvironment* env_;
};
} // namespace wiser

