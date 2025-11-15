#include "server.h"
#include "connection.h"
#include "kqueue_backend.h"
#include "scheduler.h"
#include <arpa/inet.h>
#include <iostream>
#include <stdexcept>

Server::Server(int port) : port(port) {
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(listen_fd);
        throw std::runtime_error("Failed to bind socket");
    }

    if (listen(listen_fd, BACKLOG) < 0) {
        close(listen_fd);
        throw std::runtime_error("Failed to listen on socket");
    }

    set_nonblocking(listen_fd);
}

Server::~Server() {
    if (listen_fd >= 0) {
        close(listen_fd);
    }
}

void Server::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void Server::run() {
    accept_loop();
}

void Server::accept_loop() {
    std::cout << "[SERVER] Server listening on port " << port << std::endl;

    while (true) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        //std::cout << "[SERVER] Calling accept() on listen_fd=" << listen_fd << std::endl;
        int client_fd = accept(listen_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);

        if (client_fd >= 0) {
            std::cout << "[SERVER] Accepted new connection: client_fd=" << client_fd << std::endl;
            set_nonblocking(client_fd);

            // Spawn connection coroutine - it will register itself with kqueue
            std::cout << "[SERVER] Spawning handle_connection coroutine for fd=" << client_fd << std::endl;
            handle_connection(client_fd);
            std::cout << "[SERVER] Coroutine spawned for fd=" << client_fd << std::endl;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                //std::cout << "[SERVER] accept() would block, sleeping..." << std::endl;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            } else {
                std::cout << "[SERVER] accept() error: " << strerror(errno) << std::endl;
            }
        }
    }
}
