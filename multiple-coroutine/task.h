#pragma once

#include <coroutine>
#include <exception>
#include <utility>

template<typename T = void>
class Task {
public:
    struct promise_type {
        T value;
        std::exception_ptr exception;
        
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        
        void return_value(T v) {
            value = std::move(v);
        }
        
        void unhandled_exception() {
            exception = std::current_exception();
        }
    };
    
    using handle_type = std::coroutine_handle<promise_type>;
    
    Task(handle_type h) : coro_(h) {}
    
    Task(Task&& other) noexcept : coro_(std::exchange(other.coro_, {})) {}
    
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (coro_) coro_.destroy();
            coro_ = std::exchange(other.coro_, {});
        }
        return *this;
    }
    
    ~Task() {
        if (coro_) coro_.destroy();
    }
    
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    handle_type handle() { return coro_; }
    
    // Detach the handle - coroutine will not be destroyed by this Task
    handle_type detach() {
        return std::exchange(coro_, {});
    }
    
    T result() {
        if (coro_.promise().exception) {
            std::rethrow_exception(coro_.promise().exception);
        }
        return std::move(coro_.promise().value);
    }
    
private:
    handle_type coro_;
};

// Specialization for void
template<>
class Task<void> {
public:
    struct promise_type {
        std::exception_ptr exception;
        
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        
        void return_void() {}
        
        void unhandled_exception() {
            exception = std::current_exception();
        }
    };
    
    using handle_type = std::coroutine_handle<promise_type>;
    
    Task(handle_type h) : coro_(h) {}
    
    Task(Task&& other) noexcept : coro_(std::exchange(other.coro_, {})) {}
    
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (coro_) coro_.destroy();
            coro_ = std::exchange(other.coro_, {});
        }
        return *this;
    }
    
    ~Task() {
        if (coro_) coro_.destroy();
    }
    
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    handle_type handle() { return coro_; }
    
    // Detach the handle - coroutine will not be destroyed by this Task
    handle_type detach() {
        return std::exchange(coro_, {});
    }
    
    void result() {
        if (coro_.promise().exception) {
            std::rethrow_exception(coro_.promise().exception);
        }
    }
    
private:
    handle_type coro_;
};
