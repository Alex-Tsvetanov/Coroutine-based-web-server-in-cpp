#pragma once

#include <memory>
#include <atomic>
#include <vector>
#include <mutex>

class Connection;

class Server {
public:
    Server(int port);
    ~Server();
    
    void start(size_t num_worker_threads = 4);
    void stop();
    void run();
    
private:
    void accept_loop();
    void cleanup_connections();
    
    int listen_fd_;
    int port_;
    std::atomic<bool> running_;
    std::vector<std::shared_ptr<Connection>> connections_;
    std::mutex connections_mutex_;
};
