#include "server.h"
#include <iostream>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_shutdown{false};

void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    g_shutdown.store(true);
}

int main(int argc, char* argv[]) {
    int port = 8080;
    
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        Server server(port);
        server.start(4); // 4 worker threads
        
        // Run accept loop in main thread - blocks until shutdown
        server.run();
        
        std::cout << "Shutting down server..." << std::endl;
        server.stop();
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
