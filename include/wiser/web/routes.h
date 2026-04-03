/**
 * @file routes.h
 * @brief Web 服务器路由注册与处理。
 */

#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <atomic>
#include "wiser/web/task_queue.h"
#include "wiser/3rdparty/httplib.h"

namespace wiser {
    class WiserEnvironment;
    class SearchEngine;
}

namespace wiser::web {
    /**
     * @brief 注册所有 HTTP 路由
     *
     * 在提供的 HTTP 服务器上注册所有路由处理函数。
     *
     * 线程安全：
     * - 使用 index_mutex shared_lock 保护搜索（允许并发读）。
     * - 使用 index_mutex unique_lock 保护索引写入和删除。
     * - 使用 tasks_mu 保护任务表访问。
     */
    void register_routes(httplib::Server& svr,
                         wiser::WiserEnvironment& env,
                         wiser::SearchEngine& search_engine,
                         std::shared_mutex& index_mutex,
                         std::mutex& tasks_mu,
                         TaskTable& tasks,
                         TaskQueue& queue,
                         std::atomic<uint64_t>& seq);
} // namespace wiser::web

