/**
 * @file graceful.h
 * @brief 优雅停机与信号处理模块。
 */

#pragma once

#include <atomic>
#include <string>

namespace httplib {
    class Server;
}

namespace wiser::web {
    /** 
     * @brief 全局停机请求标志 
     */
    extern std::atomic<bool> g_shutdown_requested;

    /** 
     * @brief 全局 HTTP 服务器指针
     * 
     * 用于在信号处理函数中停止服务器。
     */
    extern httplib::Server* g_server_ptr;

    /**
     * @brief 请求优雅停机
     * @param reason 停机原因描述
     */
    void request_shutdown(const char* reason);

    /**
     * @brief 安装信号处理器 (SIGINT, SIGTERM)
     */
    void install_signal_handlers();

    /**
     * @brief 安装标准输入 EOF 监听器
     * 
     * 在独立线程中等待 stdin 关闭，用于某些环境下的自动退出。
     */
    void install_stdin_eof_watcher();
} // namespace wiser::web
