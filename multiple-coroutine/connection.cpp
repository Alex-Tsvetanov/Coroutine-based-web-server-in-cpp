#include "connection.h"
#include "task.h"
#include "awaitable.h"
#include "http_parser.h"
#include "kqueue_backend.h"
#include "scheduler.h"
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <sstream>
#include <iostream>

Connection::Connection(int fd) 
    : fd_(fd), stop_(false), incoming_(100), outgoing_(100) {
}

Connection::~Connection() {
    if (fd_ >= 0) {
        KqueueBackend::instance().unregister(fd_);
        ::close(fd_);
    }
}

void Connection::start() {
    auto self = shared_from_this();
    
    // Start the three coroutines and detach them
    auto read_coro = read_task();
    auto logic_coro = logic_task();
    auto write_coro = write_task();
    
    Scheduler::instance().schedule(read_coro.detach());
    Scheduler::instance().schedule(logic_coro.detach());
    Scheduler::instance().schedule(write_coro.detach());
}

void Connection::close() {
    stop_.store(true);
    incoming_.close();
    outgoing_.close();
}

Task<void> Connection::read_task() {
    auto self = shared_from_this();
    
    std::cout << "read_task started for fd " << fd_ << std::endl;
    
    while (!stop_.load()) {
        try {
            // Wait for socket to be readable
            std::cout << "read_task waiting for readable on fd " << fd_ << std::endl;
            co_await await_readable(fd_);
            
            if (stop_.load()) break;
            
            // Allocate buffer and read
            auto buffer = std::make_shared<Buffer>(8192);
            ssize_t n = ::recv(fd_, buffer->get(), buffer->size(), 0);
            
            std::cout << "read_task: recv returned " << n << " (errno=" << errno << ")" << std::endl;
            
            if (n <= 0) {
                if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    // Connection closed or error
                    stop_.store(true);
                    co_await incoming_.async_send(nullptr); // Sentinel
                    break;
                }
                continue;
            }
            
            buffer->data.resize(n);
            std::cout << "read_task: sending " << n << " bytes to incoming channel" << std::endl;
            co_await incoming_.async_send(buffer);
            std::cout << "read_task: buffer sent successfully" << std::endl;
            
        } catch (...) {
            stop_.store(true);
            break;
        }
    }
    
    co_await incoming_.async_send(nullptr);
    co_return;
}

Task<void> Connection::logic_task() {
    auto self = shared_from_this();
    HttpParser parser;
    
    std::cout << "logic_task started for fd " << fd_ << std::endl;
    
    while (!stop_.load()) {
        try {
            auto buffer_opt = co_await incoming_.async_recv();
            
            if (!buffer_opt || stop_.load()) {
                break;
            }
            
            auto buffer = *buffer_opt;
            
            // Parse HTTP request incrementally
            bool complete = parser.parse(buffer->get(), buffer->size());
            
            // Yield after parsing to allow other tasks to run
            co_await yield();
            
            if (complete) {
                // Build response
                std::ostringstream response;
                response << "HTTP/1.1 200 OK\r\n";
                response << "Content-Type: text/plain\r\n";
                response << "Connection: close\r\n";
                
                std::string body = "Hello from coroutine server!\n";
                body += "Method: " + parser.method() + "\n";
                body += "Path: " + parser.path() + "\n";
                
                response << "Content-Length: " << body.size() << "\r\n";
                response << "\r\n";
                response << body;
                
                std::string response_str = response.str();
                
                // Split response into chunks and send
                const size_t chunk_size = 4096;
                for (size_t offset = 0; offset < response_str.size(); offset += chunk_size) {
                    size_t len = std::min(chunk_size, response_str.size() - offset);
                    auto chunk = std::make_shared<Buffer>();
                    chunk->data.assign(
                        response_str.begin() + offset,
                        response_str.begin() + offset + len
                    );
                    co_await outgoing_.async_send(chunk);
                }
                
                // Send sentinel to signal end
                co_await outgoing_.async_send(nullptr);
                stop_.store(true);
                break;
            }
            
        } catch (...) {
            stop_.store(true);
            break;
        }
    }
    
    co_await outgoing_.async_send(nullptr);
    co_return;
}

Task<void> Connection::write_task() {
    auto self = shared_from_this();
    
    while (!stop_.load()) {
        try {
            auto buffer_opt = co_await outgoing_.async_recv();
            
            if (!buffer_opt || stop_.load()) {
                break;
            }
            
            auto buffer = *buffer_opt;
            
            // Write all data in buffer
            size_t offset = 0;
            while (offset < buffer->size()) {
                co_await await_writable(fd_);
                
                if (stop_.load()) break;
                
                ssize_t n = ::send(fd_, buffer->get() + offset, buffer->size() - offset, 0);
                
                if (n <= 0) {
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        continue;
                    }
                    // Error or closed
                    stop_.store(true);
                    break;
                }
                
                offset += n;
            }
            
        } catch (...) {
            stop_.store(true);
            break;
        }
    }
    
    co_return;
}
