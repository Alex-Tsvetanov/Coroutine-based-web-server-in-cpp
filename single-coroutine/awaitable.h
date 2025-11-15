#pragma once

#include "common.h"
#include "kqueue_backend.h"
#include <iostream>

class WaitReadable {
public:
    explicit WaitReadable(int fd) : fd(fd) {}

    bool await_ready() const noexcept {
        std::cout << "[AWAIT] WaitReadable::await_ready() for fd=" << fd << " returning false" << std::endl;
        return false;
    }

    bool await_suspend(std::coroutine_handle<> handle) noexcept {
        std::cout << "[AWAIT] WaitReadable::await_suspend() for fd=" << fd << " handle=" << handle.address() << std::endl;
        bool should_suspend = g_kqueue->update_read_handle(fd, handle);
        std::cout << "[AWAIT] WaitReadable::await_suspend() for fd=" << fd << " completed, should_suspend=" << should_suspend << std::endl;
        return should_suspend;
    }

    void await_resume() const noexcept {
        std::cout << "[AWAIT] WaitReadable::await_resume() for fd=" << fd << std::endl;
        g_kqueue->clear_read_scheduled(fd);
    }

private:
    int fd;
};

class WaitWritable {
public:
    explicit WaitWritable(int fd) : fd(fd) {}

    bool await_ready() const noexcept {
        std::cout << "[AWAIT] WaitWritable::await_ready() for fd=" << fd << " returning false" << std::endl;
        return false;
    }

    bool await_suspend(std::coroutine_handle<> handle) noexcept {
        std::cout << "[AWAIT] WaitWritable::await_suspend() for fd=" << fd << " handle=" << handle.address() << std::endl;
        bool should_suspend = g_kqueue->update_write_handle(fd, handle);
        std::cout << "[AWAIT] WaitWritable::await_suspend() for fd=" << fd << " completed, should_suspend=" << should_suspend << std::endl;
        return should_suspend;
    }

    void await_resume() const noexcept {
        std::cout << "[AWAIT] WaitWritable::await_resume() for fd=" << fd << std::endl;
        g_kqueue->clear_write_scheduled(fd);
    }

private:
    int fd;
};
