#pragma once

#include <coroutine>
#include <functional>

class Scheduler;

struct YieldAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h);
    void await_resume() const noexcept {}
};

struct IOAwaiter {
    int fd;
    bool is_write;
    
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h);
    void await_resume() const noexcept {}
};

inline YieldAwaiter yield() {
    return YieldAwaiter{};
}

inline IOAwaiter await_readable(int fd) {
    return IOAwaiter{fd, false};
}

inline IOAwaiter await_writable(int fd) {
    return IOAwaiter{fd, true};
}
