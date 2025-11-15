#include "kqueue_backend.h"
#include "scheduler.h"
#include "server.h"
#include <csignal>
#include <iostream>
#include <thread>

static void signal_handler(int) {
    std::cout << "[MAIN] Signal received, shutting down..." << std::endl;
    if (g_scheduler) {
        g_scheduler->stop();
    }
    if (g_kqueue) {
        g_kqueue->stop();
    }
}

int main() {
    std::cout << "[MAIN] Starting server..." << std::endl;
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        const size_t num_workers = std::thread::hardware_concurrency();
        std::cout << "[MAIN] Creating scheduler with " << num_workers << " worker threads" << std::endl;
        
        Scheduler scheduler(num_workers);
        g_scheduler = &scheduler;

        std::cout << "[MAIN] Creating kqueue backend" << std::endl;
        KqueueBackend kqueue_backend;
        g_kqueue = &kqueue_backend;

        std::cout << "[MAIN] Starting kqueue backend" << std::endl;
        kqueue_backend.start();
        
        std::cout << "[MAIN] Spawning worker threads" << std::endl;
        scheduler.spawn_workers();

        std::cout << "[MAIN] Creating server on port 8080" << std::endl;
        Server server(8080);
        
        std::cout << "[MAIN] Starting server accept loop" << std::endl;
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "[MAIN] Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "[MAIN] Server stopped" << std::endl;
    return 0;
}
