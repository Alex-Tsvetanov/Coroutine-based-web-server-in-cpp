#pragma once

#include "common.h"
#include "channel.h"
#include "task.h"
#include <atomic>
#include <memory>
#include <vector>

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(int fd);
    ~Connection();
    
    void start();
    void close();
    
    int fd() const { return fd_; }
    bool should_stop() const { return stop_.load(); }
    
private:
    Task<void> read_task();
    Task<void> logic_task();
    Task<void> write_task();
    
    int fd_;
    std::atomic<bool> stop_;
    Channel<BufferPtr> incoming_;
    Channel<BufferPtr> outgoing_;
};
