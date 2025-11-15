#pragma once

#include "common.h"
#include "task.h"
#include <vector>
#include <mutex>

class Server {
public:
    Server(int port);
    ~Server();

    void run();

private:
    void accept_loop();
    void set_nonblocking(int fd);

    int listen_fd;
    int port;
    std::vector<Task<>> tasks;
    std::mutex tasks_mutex;
};
