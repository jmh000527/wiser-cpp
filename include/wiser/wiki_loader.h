#pragma once

/**
 * @file wiki_loader.h
 * @brief 简化版维基百科XML加载器，提取<title>与<text>作为可索引文档。
 */

#include "types.h"
#include <string>
#include <memory>

namespace wiser {
    class WiserEnvironment;

    /**
     * 维基百科数据加载器
     */
    class WikiLoader {
    public:
        /**
         * 构造加载器
         * @param env 环境指针
         */
        explicit WikiLoader(WiserEnvironment* env);
        ~WikiLoader() = default;

        // 不可复制，可移动
        WikiLoader(const WikiLoader&) = delete;
        WikiLoader& operator=(const WikiLoader&) = delete;
        WikiLoader(WikiLoader&&) = default;
        WikiLoader& operator=(WikiLoader&&) = default;

        /**
         * 从文件加载维基百科数据
         * @param file_path 文件路径
         * @return true成功，false失败
         */
        bool loadFromFile(const std::string& file_path);

        /**
         * 处理单个维基页面
         * @param title 页面标题
         * @param content 页面内容
         * @return true成功，false失败
         */
        bool processPage(const std::string& title, const std::string& content);

    private:
        WiserEnvironment* env_;

        // 辅助函数
        /**
         * 清洗维基语法，输出可索引文本
         * @param raw_text 原始文本
         * @return 清洗后的纯文本
         */
        std::string cleanWikiText(const std::string& raw_text);
        /**
         * 判定页面是否有效并可被索引
         * @param title 页面标题
         * @param content 页面内容
         * @return true 可索引��false 跳过
         */
        bool isValidPage(const std::string& title, const std::string& content);
    };
} // namespace wiser
