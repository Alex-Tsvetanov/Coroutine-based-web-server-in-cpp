#include "connection.h"
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

static constexpr const char* HTTP_RESPONSE =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 13\r\n"
    "Connection: keep-alive\r\n"
    "\r\n"
    "Hello, World!";

static const size_t HTTP_RESPONSE_LEN = std::strlen(HTTP_RESPONSE);

Task<> handle_connection(int client_fd) {
    std::cout << "[CONN " << client_fd << "] Coroutine started" << std::endl;
    char buffer[BUFFER_SIZE];
    HttpParser parser;
    size_t buffer_pos = 0;

    // Register with kqueue now that the coroutine is running
    std::cout << "[CONN " << client_fd << "] Registering with kqueue" << std::endl;
    g_kqueue->register_fd(client_fd, nullptr, nullptr);
    std::cout << "[CONN " << client_fd << "] Registered with kqueue" << std::endl;

    while (true) {
        std::cout << "[CONN " << client_fd << "] Starting request loop iteration" << std::endl;
        // Read until EAGAIN
        bool got_data = false;
        while (true) {
            std::cout << "[CONN " << client_fd << "] Calling recv(), buffer_pos=" << buffer_pos << std::endl;
            ssize_t n = recv(client_fd, buffer + buffer_pos, BUFFER_SIZE - buffer_pos, 0);
            std::cout << "[CONN " << client_fd << "] recv() returned " << n << std::endl;

            if (n > 0) {
                got_data = true;
                buffer_pos += n;
                std::cout << "[CONN " << client_fd << "] Received " << n << " bytes, total buffer_pos=" << buffer_pos << std::endl;

                // Parse incrementally
                std::cout << "[CONN " << client_fd << "] Parsing HTTP request" << std::endl;
                if (parser.parse(buffer, buffer_pos)) {
                    // Request complete
                    std::cout << "[CONN " << client_fd << "] HTTP request parsing complete" << std::endl;
                    break;
                }

                if (parser.is_error()) {
                    std::cout << "[CONN " << client_fd << "] Parser error, closing connection" << std::endl;
                    goto cleanup;
                }

                if (buffer_pos >= BUFFER_SIZE) {
                    std::cout << "[CONN " << client_fd << "] Buffer full, closing connection" << std::endl;
                    // Buffer full, no complete request
                    goto cleanup;
                }

                // Yield to allow other connections to progress
                std::cout << "[CONN " << client_fd << "] Yielding to scheduler" << std::endl;
                co_await g_scheduler->yield();
                std::cout << "[CONN " << client_fd << "] Resumed after yield" << std::endl;
            } else if (n == 0) {
                // Connection closed
                std::cout << "[CONN " << client_fd << "] Connection closed by peer" << std::endl;
                goto cleanup;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Would block
                    std::cout << "[CONN " << client_fd << "] recv() would block (EAGAIN)" << std::endl;
                    break;
                } else {
                    // Error
                    std::cout << "[CONN " << client_fd << "] recv() error: " << strerror(errno) << std::endl;
                    goto cleanup;
                }
            }
        }

        if (!parser.is_complete()) {
            // Need more data, wait for read readiness
            std::cout << "[CONN " << client_fd << "] Request incomplete, waiting for readable" << std::endl;
            co_await WaitReadable(client_fd);
            std::cout << "[CONN " << client_fd << "] Resumed after WaitReadable" << std::endl;
            continue;
        }

        // Generate response
        std::cout << "[CONN " << client_fd << "] Generating HTTP response" << std::endl;
        const char* response = HTTP_RESPONSE;
        size_t response_len = HTTP_RESPONSE_LEN;
        size_t sent = 0;

        // Write until EAGAIN
        while (sent < response_len) {
            std::cout << "[CONN " << client_fd << "] Waiting for writable, sent=" << sent << "/" << response_len << std::endl;
            co_await WaitWritable(client_fd);
            std::cout << "[CONN " << client_fd << "] Resumed after WaitWritable" << std::endl;

            while (sent < response_len) {
                std::cout << "[CONN " << client_fd << "] Calling send(), sent=" << sent << "/" << response_len << std::endl;
                ssize_t n = send(client_fd, response + sent, response_len - sent, 0);
                std::cout << "[CONN " << client_fd << "] send() returned " << n << std::endl;

                if (n > 0) {
                    sent += n;
                    std::cout << "[CONN " << client_fd << "] Sent " << n << " bytes, total sent=" << sent << std::endl;
                    std::cout << "[CONN " << client_fd << "] Yielding to scheduler" << std::endl;
                    co_await g_scheduler->yield();
                    std::cout << "[CONN " << client_fd << "] Resumed after yield" << std::endl;
                } else if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        std::cout << "[CONN " << client_fd << "] send() would block (EAGAIN)" << std::endl;
                        break;
                    } else {
                        std::cout << "[CONN " << client_fd << "] send() error: " << strerror(errno) << std::endl;
                        goto cleanup;
                    }
                }
            }
        }

        // Reset for next request (keep-alive)
        std::cout << "[CONN " << client_fd << "] Response sent, resetting for keep-alive" << std::endl;
        parser.reset();
        buffer_pos = 0;

        std::cout << "[CONN " << client_fd << "] Yielding to scheduler" << std::endl;
        co_await g_scheduler->yield();
        std::cout << "[CONN " << client_fd << "] Resumed after yield, starting next iteration" << std::endl;
    }

cleanup:
    std::cout << "[CONN " << client_fd << "] Cleanup: unregistering and closing" << std::endl;
    g_kqueue->unregister_fd(client_fd);
    close(client_fd);
    std::cout << "[CONN " << client_fd << "] Connection closed" << std::endl;
    co_return;
}
