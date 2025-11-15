#include "server.h"
#include "connection.h"
#include "scheduler.h"
#include "kqueue_backend.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <algorithm>

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        throw std::runtime_error("fcntl F_GETFL failed");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error("fcntl F_SETFL failed");
    }
}

Server::Server(int port) : listen_fd_(-1), port_(port), running_(false) {
}

Server::~Server() {
    stop();
}

void Server::start(size_t num_worker_threads) {
    if (running_.load()) return;
    
    std::cout << "Starting server initialization..." << std::endl;
    
    // Create listening socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error("Failed to create socket");
    }
    
    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error("Failed to set SO_REUSEADDR");
    }
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error("Failed to bind to port " + std::to_string(port_));
    }
    
    if (listen(listen_fd_, 128) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error("Failed to listen");
    }
    
    set_nonblocking(listen_fd_);
    
    // Start scheduler and kqueue backend
    Scheduler::instance().start(num_worker_threads);
    KqueueBackend::instance().start();
    
    running_.store(true);
    
    std::cout << "Server listening on port " << port_ << std::endl;
}

void Server::stop() {
    if (!running_.load()) return;
    running_.store(false);
    
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    
    // Close all connections
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& conn : connections_) {
            conn->close();
        }
        connections_.clear();
    }
    
    KqueueBackend::instance().stop();
    Scheduler::instance().stop();
}

void Server::cleanup_connections() {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
            [](const std::shared_ptr<Connection>& conn) {
                return conn->should_stop();
            }),
        connections_.end()
    );
}

void Server::run() {
    accept_loop();
}

void Server::accept_loop() {
    while (running_.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Set timeout for accept to allow checking running flag
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(listen_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No pending connections, sleep briefly
                usleep(1000);
                continue;
            }
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Accept failed: " << strerror(errno) << std::endl;
            continue;
        }
        
        try {
            set_nonblocking(client_fd);
            
            // Create connection and start coroutines
            auto conn = std::make_shared<Connection>(client_fd);
            
            {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                connections_.push_back(conn);
            }
            
            conn->start();
            
            std::cout << "Accepted connection from " 
                      << inet_ntoa(client_addr.sin_addr) << ":" 
                      << ntohs(client_addr.sin_port) << std::endl;
            
            // Clean up finished connections periodically
            cleanup_connections();
                      
        } catch (const std::exception& e) {
            std::cerr << "Failed to handle connection: " << e.what() << std::endl;
            ::close(client_fd);
        }
    }
}
