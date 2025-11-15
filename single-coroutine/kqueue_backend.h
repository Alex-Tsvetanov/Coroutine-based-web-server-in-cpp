#pragma once

#include "common.h"
#include <atomic>
#include <thread>
#include <unordered_map>
#include <mutex>

class KqueueBackend {
public:
    KqueueBackend();
    ~KqueueBackend();

    void start();
    void stop();
    void register_fd(int fd, std::coroutine_handle<> read_handle, std::coroutine_handle<> write_handle);
    void unregister_fd(int fd);
    bool update_read_handle(int fd, std::coroutine_handle<> handle);
    bool update_write_handle(int fd, std::coroutine_handle<> handle);
    void clear_read_scheduled(int fd);
    void clear_write_scheduled(int fd);

private:
    void event_loop();

    int kq_fd;
    std::thread event_thread;
    std::atomic<bool> running{true};
    
    struct FdState {
        std::coroutine_handle<> read_handle;
        std::coroutine_handle<> write_handle;
        bool read_scheduled{false};
        bool write_scheduled{false};
        bool read_ready{false};   // Event ready but no handle yet
        bool write_ready{false};  // Event ready but no handle yet
    };

    std::unordered_map<int, FdState> fd_states;
    std::mutex fd_mutex;

    static constexpr int MAX_EVENTS = 64;
};
