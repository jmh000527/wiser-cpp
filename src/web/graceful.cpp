/**
 * @file graceful.cpp
 * @brief Web 服务器优雅退出支持
 *
 * 功能：
 * - 安装平台相关的信号/控制台事件处理器
 * - 将“退出请求”统一转为调用 httplib::Server::stop()
 * - 通过全局原子标志确保 stop() 只触发一次
 */

#include "wiser/web/graceful.h"
#include "wiser/3rdparty/httplib.h"
#include <spdlog/spdlog.h>
#include <csignal>
#ifdef _WIN32
#include <windows.h>
#endif
#include <thread>
#include <iostream>

namespace wiser::web {
    // 全局原子布尔变量：指示是否已经请求关闭服务器（确保只执行一次关闭逻辑）。
    std::atomic<bool> g_shutdown_requested{ false };
    // 全局服务器指针：在服务器启动前由外部代码赋值，用于在收到关闭信号时调用 stop()。
    httplib::Server* g_server_ptr{ nullptr };

    // 请求关闭服务器。
    // 参数 reason：触发关闭的原因描述（信号名称 / 事件标识 / EOF 等）。
    void request_shutdown(const char* reason) {
        bool expected = false;
        // 使用 CAS 保证关闭逻辑仅被执行一次（避免重复 stop()）。
        if (g_shutdown_requested.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            spdlog::info("Shutdown requested: {}", reason ? reason : "(no reason)");
            if (g_server_ptr) {
                g_server_ptr->stop(); // 触发 httplib::Server 的停止逻辑。
            }
        }
    }

    #ifndef _WIN32
    // POSIX 平台信号处理器：捕获常见终止/中断类信号并触发优雅关闭。
    // 注意：严格来说在信号处理器中调用非异步信号安全函数（如日志）存在风险；此处逻辑尽量保持最小：设置标志+stop()。
    // 如果后续需要更高安全性，可改为写入自管道或 eventfd 再由主循环处理。
    static void posix_signal_handler(int sig) {
        switch (sig) {
            case SIGINT: // Ctrl+C 中断（常见开发/手动停止）
                request_shutdown("SIGINT");
                break;
            case SIGTERM: // kill 默认发送；服务编排停止流程常用
                request_shutdown("SIGTERM");
                break;
    #ifdef SIGQUIT
    case SIGQUIT
        : // Ctrl+\；通常可能生成 core，此处改为优雅退出
        request_shutdown("SIGQUIT");
                break;
    #endif
    #ifdef SIGTSTP
    case SIGTSTP
        : // Ctrl+Z 暂停；为避免进程挂起占资源，转为关闭
        request_shutdown("SIGTSTP");
                break;
    #endif
    default
        :  // 其他未关注信号：忽略
                break;
        }
    }
    #else
    // Windows 控制台事件处理：拦截典型控制台/系统会话结束事件，统一触发优雅关闭。
    // 返回 TRUE 表示事件已处理（阻止默认行为），让主线程驱动退出流程。
    static BOOL WINAPI console_ctrl_handler(DWORD type) {
        switch (type) {
            case CTRL_C_EVENT: // Ctrl+C
                request_shutdown("CTRL_C_EVENT");
                break;
            case CTRL_BREAK_EVENT: // Ctrl+Break
                request_shutdown("CTRL_BREAK_EVENT");
                break;
            case CTRL_CLOSE_EVENT: // 用户关闭控制台窗口
                request_shutdown("CTRL_CLOSE_EVENT");
                break;
            case CTRL_LOGOFF_EVENT: // 用户注销（有时可能不触发，取决于环境）
                request_shutdown("CTRL_LOGOFF_EVENT");
                break;
            case CTRL_SHUTDOWN_EVENT: // 系统关机（需尽快释放资源）
                request_shutdown("CTRL_SHUTDOWN_EVENT");
                break;
            default: // 未处理类型：忽略
                break;
        }
        return TRUE; // 表示事件已处理。
    }
    #endif

    // 安装平台相关的信号/控制台处理器。
    // 说明：处理器为进程范围，应在服务器启动前尽早调用，且只安装一次。
    // 注意：std::signal 会替换相同信号的既有处理器；与其他库共享进程时需协调。
    void install_signal_handlers() {
        #ifndef _WIN32
        // POSIX: 安装常见终止/中断信号的处理器，触发优雅关闭。
        // SIGINT：通常来源为 Ctrl+C。
        std::signal(SIGINT, posix_signal_handler);
        // SIGTERM：默认由 kill 发送，常用于容器/服务管理器停止进程。
        std::signal(SIGTERM, posix_signal_handler);
        #ifdef SIGQUIT
        // SIGQUIT：常为 Ctrl+\，默认可能生成 core，此处改为优雅关闭。
        std::signal(SIGQUIT, posix_signal_handler);
        #endif
        #ifdef SIGTSTP
        // SIGTSTP：常为 Ctrl+Z（暂停）；这里统一视作关闭请求以避免进程悬挂。
        std::signal(SIGTSTP, posix_signal_handler);
        #endif
        #else
        // Windows：注册控制台事件处理函数，涵盖 Ctrl+C、窗口关闭、注销、关机等。
        // 提示：SetConsoleCtrlHandler 返回 BOOL，如需更严谨可检查返回值并记录失败原因
        //（例如无控制台进程可能注册失败）。
        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
        #endif
    }

    // 安装标准输入 EOF 监视器：
    // 当标准输入被关闭（如管道断开、EOF）且尚未请求关闭时，触发优雅退出。
    void install_stdin_eof_watcher() {
        std::thread([]() {
            std::string line;
            // 持续读取 stdin；若被关闭或收到关闭请求则退出循环。
            while (!g_shutdown_requested.load(std::memory_order_acquire) && std::getline(std::cin, line)) {}
            // 如果因 EOF 退出且之前未触发关闭，则标记关闭。
            if (!g_shutdown_requested.load(std::memory_order_acquire)) { request_shutdown("STDIN_EOF"); }
        }).detach(); // 后台线程，无需等待。
    }
} // namespace wiser::web

