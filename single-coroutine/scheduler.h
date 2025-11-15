#pragma once

#include "common.h"
#include <atomic>
#include <coroutine>
#include <mutex>
#include <thread>
#include <vector>

class Scheduler {
public:
    explicit Scheduler(size_t num_threads);
    ~Scheduler();

    void enqueue(std::coroutine_handle<> handle);
    void run();
    void stop();
    void spawn_workers();

    struct YieldAwaitable {
        Scheduler* scheduler;

        bool await_ready() const noexcept { return false; }
        
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            scheduler->enqueue(handle);
        }
        
        void await_resume() const noexcept {}
    };

    YieldAwaitable yield() { return YieldAwaitable{this}; }

private:
    struct Node {
        std::coroutine_handle<> handle;
        std::atomic<Node*> next{nullptr};
    };

    alignas(64) std::atomic<Node*> head{nullptr};
    alignas(64) std::atomic<Node*> tail{nullptr};
    std::atomic<bool> running{true};
    std::vector<std::thread> workers;
    size_t num_threads;

    std::coroutine_handle<> try_dequeue();
};
