#pragma once

#include <coroutine>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <iostream>

class Scheduler {
public:
    static Scheduler& instance() {
        static Scheduler sched;
        return sched;
    }
    
    void start(size_t num_threads) {
        if (running_.load()) return;
        running_.store(true);
        
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this, i] { 
                std::cout << "Scheduler worker " << i << " started" << std::endl;
                worker_loop(); 
            });
        }
        std::cout << "Scheduler started with " << num_threads << " workers" << std::endl;
    }
    
    void stop() {
        if (!running_.load()) return;
        running_.store(false);
        cv_.notify_all();
        
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }
    
    void schedule(std::coroutine_handle<> h) {
        if (!h) return;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_queue_.push(h);
            std::cout << "Scheduled coroutine, queue size: " << ready_queue_.size() << std::endl;
        }
        cv_.notify_one();
    }
    
    bool is_running() const {
        return running_.load();
    }
    
    ~Scheduler() {
        stop();
    }
    
private:
    Scheduler() : running_(false) {}
    
    void worker_loop() {
        while (running_.load()) {
            std::coroutine_handle<> h;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] {
                    return !ready_queue_.empty() || !running_.load();
                });
                
                if (!running_.load() && ready_queue_.empty()) {
                    break;
                }
                
                if (!ready_queue_.empty()) {
                    h = ready_queue_.front();
                    ready_queue_.pop();
                }
            }
            
            if (h) {
                h.resume();
            }
        }
    }
    
    std::vector<std::thread> workers_;
    std::queue<std::coroutine_handle<>> ready_queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_;
};
