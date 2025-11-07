#pragma once
#include <string>
#include <vector>
#include <mutex>
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
     * 在提供的 HTTP 服务器上注册所有路由。
     *
     * 线程安全：
     * - 使用 index_mutex 保护索引读写。
     * - 使用 tasks_mu 保护任务表访问。
     *
     * @param svr HTTP 服务器实例（cpp-httplib）。
     * @param env Wiser 运行环境。
     * @param search_engine 搜索引擎实例。
     * @param index_mutex 用于保护索引相关共享状态的互斥量。
     * @param tasks_mu 用于保护任务表的互斥量。
     * @param tasks 任务表，用于跟踪后台任务状态。
     * @param queue 任务队列，处理异步／后台任务。
     * @param seq 全局自增序列号（原子），用于生成任务／事件 ID。
     */
    void register_routes(httplib::Server& svr,
                         wiser::WiserEnvironment& env,
                         wiser::SearchEngine& search_engine,
                         std::mutex& index_mutex,
                         std::mutex& tasks_mu,
                         TaskTable& tasks,
                         TaskQueue& queue,
                         std::atomic<uint64_t>& seq);
} // namespace wiser::web

