/**
 * @file task_queue.cpp
 * @brief 异步任务队列实现（生产者/消费者）
 *
 * 该队列用于 Web 导入任务：
 * - push：主线程入队任务 id
 * - pop：工作线程阻塞等待并取出任务 id
 * - stop：通知所有等待线程退出
 */

#include "wiser/web/task_queue.h"

namespace wiser::web {
    const char* status_to_string(TaskStatus st) {
        // 将任务状态枚举转换为对应的字符串表示
        switch (st) {
            case TaskStatus::Queued:
                return "queued"; // 已入队，等待执行
            case TaskStatus::Running:
                return "running"; // 正在执行
            case TaskStatus::Success:
                return "success"; // 执行成功
            case TaskStatus::Failed:
                return "failed"; // 执行失败
            case TaskStatus::Unsupported:
                return "unsupported"; // 不支持的任务
        }
        return "unknown";
    }

    void TaskQueue::push(const std::string& id) { {
            // 入队一个任务 ID，持有互斥锁保护队列
            std::lock_guard<std::mutex> lk(m_mtx);
            m_queue.push_back(id);
        }
        // 通知一个等待中的消费者线程有新任务
        m_cond.notify_one();
    }

    bool TaskQueue::pop(std::string& out_id) {
        // 弹出一个任务 ID；如队列为空则阻塞等待，直到有任务或收到停止信号
        std::unique_lock<std::mutex> lk(m_mtx);
        m_cond.wait(lk, [&] {
            return m_stopped || !m_queue.empty(); // 当 m_stopped 为真或队列非空时唤醒
        });
        if (m_stopped && m_queue.empty())
            return false;               // 停止且队列为空：不再提供任务
        out_id = std::move(m_queue.front()); // 取出队首任务（移动以避免拷贝）
        m_queue.pop_front();                 // 从队列移除
        return true;
    }

    void TaskQueue::stop() { {
            // 设置停止标志，阻止后续等待并使消费者退出
            std::lock_guard<std::mutex> lk(m_mtx);
            m_stopped = true;
        }
        // 通知所有等待线程检查停止条件
        m_cond.notify_all();
    }
} // namespace wiser::web

