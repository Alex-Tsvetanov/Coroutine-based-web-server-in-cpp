#pragma once

#include <coroutine>
#include <exception>

template<typename T = void>
class Task {
public:
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() noexcept { return {}; }
        
        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                h.destroy();
            }
            void await_resume() noexcept {}
        };
        
        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
    };

    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    
    Task(Task&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }

    ~Task() {
        // Don't destroy here, FinalAwaiter handles it
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

private:
    std::coroutine_handle<promise_type> handle;
};
