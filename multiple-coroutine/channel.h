#pragma once

#include <coroutine>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <optional>

template<typename T>
class Channel {
public:
    explicit Channel(size_t capacity) : capacity_(capacity), closed_(false) {}
    
    ~Channel() {
        close();
    }
    
    struct SendAwaiter {
        Channel* channel;
        T value;
        
        bool await_ready() {
            std::unique_lock<std::mutex> lock(channel->mutex_);
            if (channel->closed_) return true;
            if (channel->buffer_.size() < channel->capacity_) {
                channel->buffer_.push(std::move(value));
                // Wake up a receiver
                if (!channel->recv_waiters_.empty()) {
                    auto waiter = channel->recv_waiters_.front();
                    channel->recv_waiters_.pop();
                    channel->schedule_coroutine(waiter);
                }
                return true;
            }
            return false;
        }
        
        void await_suspend(std::coroutine_handle<> h) {
            std::unique_lock<std::mutex> lock(channel->mutex_);
            channel->send_waiters_.push({h, std::move(value)});
        }
        
        void await_resume() {}
    };
    
    struct RecvAwaiter {
        Channel* channel;
        std::optional<T> result;
        
        bool await_ready() {
            std::unique_lock<std::mutex> lock(channel->mutex_);
            if (!channel->buffer_.empty()) {
                result = std::move(channel->buffer_.front());
                channel->buffer_.pop();
                // Wake up a sender
                if (!channel->send_waiters_.empty()) {
                    auto [waiter, val] = std::move(channel->send_waiters_.front());
                    channel->send_waiters_.pop();
                    channel->buffer_.push(std::move(val));
                    channel->schedule_coroutine(waiter);
                }
                return true;
            }
            if (channel->closed_ && channel->buffer_.empty()) {
                return true;
            }
            return false;
        }
        
        void await_suspend(std::coroutine_handle<> h) {
            std::unique_lock<std::mutex> lock(channel->mutex_);
            channel->recv_waiters_.push(h);
        }
        
        std::optional<T> await_resume() {
            return std::move(result);
        }
    };
    
    SendAwaiter async_send(T value) {
        return SendAwaiter{this, std::move(value)};
    }
    
    RecvAwaiter async_recv() {
        return RecvAwaiter{this, std::nullopt};
    }
    
    void close() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) return;
        closed_ = true;
        
        // Wake all waiters
        while (!send_waiters_.empty()) {
            auto [waiter, _] = std::move(send_waiters_.front());
            send_waiters_.pop();
            schedule_coroutine(waiter);
        }
        
        while (!recv_waiters_.empty()) {
            auto waiter = recv_waiters_.front();
            recv_waiters_.pop();
            schedule_coroutine(waiter);
        }
    }
    
    bool is_closed() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return closed_;
    }
    
private:
    void schedule_coroutine(std::coroutine_handle<> h);
    
    size_t capacity_;
    bool closed_;
    std::queue<T> buffer_;
    std::queue<std::pair<std::coroutine_handle<>, T>> send_waiters_;
    std::queue<std::coroutine_handle<>> recv_waiters_;
    mutable std::mutex mutex_;
};
