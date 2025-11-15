#include "scheduler.h"
#include <iostream>

Scheduler::Scheduler(size_t num_threads) : num_threads(num_threads) {
    std::cout << "[SCHED] Creating scheduler with " << num_threads << " threads" << std::endl;
    auto dummy = new Node{};
    head.store(dummy, std::memory_order_relaxed);
    tail.store(dummy, std::memory_order_relaxed);
}

Scheduler::~Scheduler() {
    stop();
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    Node* node = head.load(std::memory_order_relaxed);
    while (node) {
        Node* next = node->next.load(std::memory_order_relaxed);
        delete node;
        node = next;
    }
}

void Scheduler::enqueue(std::coroutine_handle<> handle) {
    std::cout << "[SCHED] Enqueuing coroutine handle " << handle.address() << std::endl;
    auto* node = new Node{handle, nullptr};
    
    Node* prev = tail.exchange(node, std::memory_order_acq_rel);
    prev->next.store(node, std::memory_order_release);
    std::cout << "[SCHED] Coroutine handle " << handle.address() << " enqueued" << std::endl;
}

std::coroutine_handle<> Scheduler::try_dequeue() {
    while (true) {
        Node* h = head.load(std::memory_order_acquire);
        Node* next = h->next.load(std::memory_order_acquire);
        
        if (next == nullptr) {
            return nullptr;
        }
        
        if (head.compare_exchange_weak(h, next, std::memory_order_release, std::memory_order_relaxed)) {
            auto handle = next->handle;
            delete h;
            return handle;
        }
    }
}

void Scheduler::run() {
    std::cout << "[SCHED] Worker thread " << std::this_thread::get_id() << " started" << std::endl;
    while (running.load(std::memory_order_acquire)) {
        auto handle = try_dequeue();
        if (handle) {
            std::cout << "[SCHED] Worker " << std::this_thread::get_id() << " resuming coroutine " << handle.address() << std::endl;
            handle.resume();
            std::cout << "[SCHED] Worker " << std::this_thread::get_id() << " finished resuming coroutine " << handle.address() << std::endl;
        } else {
            // No work, yield
            std::this_thread::yield();
        }
    }
    std::cout << "[SCHED] Worker thread " << std::this_thread::get_id() << " stopped" << std::endl;
}

void Scheduler::stop() {
    running.store(false, std::memory_order_release);
}

void Scheduler::spawn_workers() {
    std::cout << "[SCHED] Spawning " << num_threads << " worker threads" << std::endl;
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back([this] { run(); });
    }
    std::cout << "[SCHED] All worker threads spawned" << std::endl;
}
