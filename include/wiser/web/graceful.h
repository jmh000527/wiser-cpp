#pragma once
#include <atomic>
#include <string>

namespace httplib {
    class Server;
}

namespace wiser::web {
    // Global shutdown state
    extern std::atomic<bool> g_shutdown_requested;
    extern httplib::Server* g_server_ptr;

    void request_shutdown(const char* reason);
    void install_signal_handlers();
    void install_stdin_eof_watcher();
} // namespace wiser::web
