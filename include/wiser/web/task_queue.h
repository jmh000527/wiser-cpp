#pragma once
#include <string>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

namespace wiser::web {
    // 任务状态枚举
    enum class TaskStatus {
        Queued,     // 已入队，等待处理
        Running,    // 正在处理
        Success,    // 处理成功
        Failed,     // 处理失败
        Unsupported // 不支持的任务或操作
    };

    // 将任务状态转换为只读 C 字符串（返回静态文本）
    const char* status_to_string(TaskStatus st);

    // 上传/处理任务的元数据
    struct Task {
        std::string id;                                                                       // 任务唯一 ID（如 UUID）
        std::string field_key;                                                                // 表单字段 key（前端上传字段名）
        std::string filename;                                                                 // 原始文件名
        std::string temp_path;                                                                // 临时文件路径
        TaskStatus status{ TaskStatus::Queued };                                              // 当前状态（默认：已入队）
        std::string message;                                                                  // 附加消息（错误原因/提示信息）
        std::chrono::steady_clock::time_point created_at{ std::chrono::steady_clock::now() }; // 创建时间（单调时钟）
        std::chrono::steady_clock::time_point updated_at{ std::chrono::steady_clock::now() }; // 最后更新时间（单调时钟）
    };

    // 任务表：键为任务 ID，值为任务信息
    using TaskTable = std::unordered_map<std::string, Task>;

    // 任务 ID 队列：提供线程安全的入队/出队与停止控制
    class TaskQueue {
    public:
        // 将任务 ID 推入队列（线程安全）
        void push(const std::string& id);

        // 阻塞式弹出一个任务 ID
        // \param out_id 接收弹出的任务 ID
        // \return true 表示成功获取任务；false 表示队列已停止且无任务
        bool pop(std::string& out_id);

        // 通知停止队列并唤醒所有等待线程
        void stop();

    private:
        // 保护队列与状态的互斥锁
        std::mutex m_mtx;

        // 等待/通知队列变化的条件变量
        std::condition_variable m_cond;

        // 待处理的任务 ID 队列（FIFO）
        std::deque<std::string> m_queue;

        // 停止标志；为 true 时不再接受任务，pop 将尽快返回 false
        bool m_stopped{ false };
    };
} // namespace wiser::web

