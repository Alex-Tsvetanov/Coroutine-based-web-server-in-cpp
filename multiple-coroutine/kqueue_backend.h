#pragma once

#include <sys/event.h>
#include <unistd.h>
#include <coroutine>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>

class KqueueBackend {
public:
    static KqueueBackend& instance() {
        static KqueueBackend backend;
        return backend;
    }
    
    void start() {
        if (running_.load()) return;
        
        kq_ = kqueue();
        if (kq_ == -1) {
            throw std::runtime_error("Failed to create kqueue");
        }
        
        running_.store(true);
        event_thread_ = std::thread([this] { event_loop(); });
    }
    
    void stop() {
        if (!running_.load()) return;
        running_.store(false);
        
        if (event_thread_.joinable()) {
            event_thread_.join();
        }
        
        if (kq_ != -1) {
            close(kq_);
            kq_ = -1;
        }
    }
    
    void register_read(int fd, std::coroutine_handle<> h) {
        struct kevent ev;
        EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        kevent(kq_, &ev, 1, nullptr, 0, nullptr);
        
        std::unique_lock<std::mutex> lock(mutex_);
        read_waiters_[fd] = h;
    }
    
    void register_write(int fd, std::coroutine_handle<> h) {
        struct kevent ev;
        EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        kevent(kq_, &ev, 1, nullptr, 0, nullptr);
        
        std::unique_lock<std::mutex> lock(mutex_);
        write_waiters_[fd] = h;
    }
    
    void unregister(int fd) {
        struct kevent ev[2];
        EV_SET(&ev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        EV_SET(&ev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
        kevent(kq_, ev, 2, nullptr, 0, nullptr);
        
        std::unique_lock<std::mutex> lock(mutex_);
        read_waiters_.erase(fd);
        write_waiters_.erase(fd);
    }
    
    ~KqueueBackend() {
        stop();
    }
    
private:
    KqueueBackend() : kq_(-1), running_(false) {}
    
    void event_loop() {
        struct kevent events[64];
        
        while (running_.load()) {
            struct timespec timeout{0, 100000000}; // 100ms
            int n = kevent(kq_, nullptr, 0, events, 64, &timeout);
            
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            
            for (int i = 0; i < n; ++i) {
                int fd = static_cast<int>(events[i].ident);
                std::coroutine_handle<> h;
                
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    
                    if (events[i].filter == EVFILT_READ) {
                        auto it = read_waiters_.find(fd);
                        if (it != read_waiters_.end()) {
                            h = it->second;
                            read_waiters_.erase(it);
                        }
                    } else if (events[i].filter == EVFILT_WRITE) {
                        auto it = write_waiters_.find(fd);
                        if (it != write_waiters_.end()) {
                            h = it->second;
                            write_waiters_.erase(it);
                        }
                    }
                }
                
                if (h) {
                    extern void schedule_coroutine(std::coroutine_handle<> h);
                    schedule_coroutine(h);
                }
            }
        }
    }
    
    int kq_;
    std::atomic<bool> running_;
    std::thread event_thread_;
    std::map<int, std::coroutine_handle<>> read_waiters_;
    std::map<int, std::coroutine_handle<>> write_waiters_;
    std::mutex mutex_;
};
