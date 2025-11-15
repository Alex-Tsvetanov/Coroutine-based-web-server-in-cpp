#include "kqueue_backend.h"
#include "scheduler.h"
#include <sys/event.h>
#include <unistd.h>
#include <iostream>

KqueueBackend::KqueueBackend() {
    std::cout << "[KQUEUE] Creating kqueue" << std::endl;
    kq_fd = kqueue();
    if (kq_fd < 0) {
        throw std::runtime_error("Failed to create kqueue");
    }
    std::cout << "[KQUEUE] kqueue created with fd=" << kq_fd << std::endl;
}

KqueueBackend::~KqueueBackend() {
    stop();
    if (event_thread.joinable()) {
        event_thread.join();
    }
    close(kq_fd);
}

void KqueueBackend::start() {
    std::cout << "[KQUEUE] Starting event loop thread" << std::endl;
    event_thread = std::thread([this] { event_loop(); });
    std::cout << "[KQUEUE] Event loop thread started" << std::endl;
}

void KqueueBackend::stop() {
    running.store(false, std::memory_order_release);
}

void KqueueBackend::register_fd(int fd, std::coroutine_handle<> read_handle, std::coroutine_handle<> write_handle) {
    std::cout << "[KQUEUE] Registering fd=" << fd << " with read_handle=" << (read_handle ? read_handle.address() : nullptr) 
              << " write_handle=" << (write_handle ? write_handle.address() : nullptr) << std::endl;
    std::lock_guard<std::mutex> lock(fd_mutex);
    
    auto& state = fd_states[fd];
    state.read_handle = read_handle;
    state.write_handle = write_handle;
    state.read_scheduled = false;
    state.write_scheduled = false;

    struct kevent events[2];
    EV_SET(&events[0], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    EV_SET(&events[1], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    int ret = kevent(kq_fd, events, 2, nullptr, 0, nullptr);
    std::cout << "[KQUEUE] Registered fd=" << fd << " with kqueue, kevent returned " << ret << std::endl;
}

void KqueueBackend::unregister_fd(int fd) {
    std::cout << "[KQUEUE] Unregistering fd=" << fd << std::endl;
    std::lock_guard<std::mutex> lock(fd_mutex);
    
    struct kevent events[2];
    EV_SET(&events[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&events[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    int ret = kevent(kq_fd, events, 2, nullptr, 0, nullptr);
    std::cout << "[KQUEUE] Unregistered fd=" << fd << " from kqueue, kevent returned " << ret << std::endl;
    
    fd_states.erase(fd);
}

bool KqueueBackend::update_read_handle(int fd, std::coroutine_handle<> handle) {
    std::cout << "[KQUEUE] Updating read_handle for fd=" << fd << " to " << handle.address() << std::endl;
    std::lock_guard<std::mutex> lock(fd_mutex);
    auto it = fd_states.find(fd);
    if (it != fd_states.end()) {
        it->second.read_handle = handle;
        it->second.write_handle = nullptr; // Clear write handle - we're now waiting for read
        
        // If event already ready, don't suspend (return false = resume immediately)
        if (it->second.read_ready) {
            std::cout << "[KQUEUE] Read event already ready for fd=" << fd << ", resuming immediately without suspending" << std::endl;
            it->second.read_ready = false;
            return false; // Don't suspend - coroutine continues immediately
        }
        
        it->second.read_scheduled = false;
        std::cout << "[KQUEUE] Updated read_handle for fd=" << fd << ", cleared write_handle" << std::endl;
    } else {
        std::cout << "[KQUEUE] WARNING: fd=" << fd << " not found in fd_states!" << std::endl;
    }
    return true; // Suspend and wait for event
}

bool KqueueBackend::update_write_handle(int fd, std::coroutine_handle<> handle) {
    std::cout << "[KQUEUE] Updating write_handle for fd=" << fd << " to " << handle.address() << std::endl;
    std::lock_guard<std::mutex> lock(fd_mutex);
    auto it = fd_states.find(fd);
    if (it != fd_states.end()) {
        it->second.write_handle = handle;
        it->second.read_handle = nullptr; // Clear read handle - we're now waiting for write
        
        // If event already ready, don't suspend (return false = resume immediately)
        if (it->second.write_ready) {
            std::cout << "[KQUEUE] Write event already ready for fd=" << fd << ", resuming immediately without suspending" << std::endl;
            it->second.write_ready = false;
            return false; // Don't suspend - coroutine continues immediately
        }
        
        it->second.write_scheduled = false;
        std::cout << "[KQUEUE] Updated write_handle for fd=" << fd << ", cleared read_handle" << std::endl;
    } else {
        std::cout << "[KQUEUE] WARNING: fd=" << fd << " not found in fd_states!" << std::endl;
    }
    return true; // Suspend and wait for event
}

void KqueueBackend::clear_read_scheduled(int fd) {
    std::lock_guard<std::mutex> lock(fd_mutex);
    auto it = fd_states.find(fd);
    if (it != fd_states.end()) {
        it->second.read_scheduled = false;
    }
}

void KqueueBackend::clear_write_scheduled(int fd) {
    std::lock_guard<std::mutex> lock(fd_mutex);
    auto it = fd_states.find(fd);
    if (it != fd_states.end()) {
        it->second.write_scheduled = false;
    }
}

void KqueueBackend::event_loop() {
    std::cout << "[KQUEUE] Event loop thread " << std::this_thread::get_id() << " starting" << std::endl;
    struct kevent events[MAX_EVENTS];

    while (running.load(std::memory_order_acquire)) {
        std::cout << "[KQUEUE] Calling kevent() to wait for events..." << std::endl;
        int n = kevent(kq_fd, nullptr, 0, events, MAX_EVENTS, nullptr);
        std::cout << "[KQUEUE] kevent() returned " << n << " events" << std::endl;

        for (int i = 0; i < n; ++i) {
            int fd = static_cast<int>(events[i].ident);
            std::cout << "[KQUEUE] Event " << i << ": fd=" << fd << " filter=" << events[i].filter 
                      << " flags=" << events[i].flags << std::endl;
            
            std::lock_guard<std::mutex> lock(fd_mutex);
            auto it = fd_states.find(fd);
            if (it == fd_states.end()) {
                std::cout << "[KQUEUE] WARNING: Event for unregistered fd=" << fd << std::endl;
                continue;
            }

            if (events[i].filter == EVFILT_READ) {
                std::cout << "[KQUEUE] EVFILT_READ for fd=" << fd << " read_handle=" 
                          << (it->second.read_handle ? it->second.read_handle.address() : nullptr)
                          << " read_scheduled=" << it->second.read_scheduled << std::endl;
                if (it->second.read_handle && !it->second.read_scheduled) {
                    std::cout << "[KQUEUE] Scheduling read coroutine for fd=" << fd << std::endl;
                    g_scheduler->enqueue(it->second.read_handle);
                    it->second.read_scheduled = true;
                    it->second.read_ready = false;
                } else if (!it->second.read_handle) {
                    std::cout << "[KQUEUE] Read event ready but no handle yet for fd=" << fd << ", marking ready" << std::endl;
                    it->second.read_ready = true;
                }
            } else if (events[i].filter == EVFILT_WRITE) {
                std::cout << "[KQUEUE] EVFILT_WRITE for fd=" << fd << " write_handle=" 
                          << (it->second.write_handle ? it->second.write_handle.address() : nullptr)
                          << " write_scheduled=" << it->second.write_scheduled << std::endl;
                if (it->second.write_handle && !it->second.write_scheduled) {
                    std::cout << "[KQUEUE] Scheduling write coroutine for fd=" << fd << std::endl;
                    g_scheduler->enqueue(it->second.write_handle);
                    it->second.write_scheduled = true;
                    it->second.write_ready = false;
                } else if (!it->second.write_handle) {
                    std::cout << "[KQUEUE] Write event ready but no handle yet for fd=" << fd << ", marking ready" << std::endl;
                    it->second.write_ready = true;
                }
            }
        }
    }
    std::cout << "[KQUEUE] Event loop thread " << std::this_thread::get_id() << " stopped" << std::endl;
}
