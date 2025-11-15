#include <atomic>
#include <coroutine>
#include <ctime>
#include <exception>
#include <memory>
#include <queue>
#include <set>
#include <thread>
#include <unistd.h>
#include <vector>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <fcntl.h>

enum class IOResumeCondition
{
    read,
    write,
    close
};

enum class TaskResumeCondition
{
    manual,
    ready
};

class Scheduler : public std::enable_shared_from_this<Scheduler>
{
public:
    class promise_type {
    public:
        using SchedulerPtr = std::weak_ptr<Scheduler>;

        promise_type(SchedulerPtr scheduler)
            : scheduler_(scheduler) {}

        SchedulerPtr get_scheduler() {
            return scheduler_;
        }

        void set_scheduler(std::shared_ptr<Scheduler> scheduler) {
            scheduler_ = scheduler;
        }
    private:
        SchedulerPtr scheduler_;
    };

    Scheduler(size_t cpu_threads, size_t io_threads)
    {
        cpu_workers.reserve(cpu_threads);
        for (size_t i = 0; i < cpu_threads; ++i) {
            cpu_workers.emplace_back();
        }

        kqueue_fd = ::kqueue();

        io_workers.reserve(io_threads);
        for (size_t i = 0; i < io_threads; ++i) {
            io_workers.emplace_back();
        }
    }

    ~Scheduler()
    {
        // Cancel all waiting tasks
        {
            std::lock_guard<std::mutex> lock(cpu_waiting_tasks_mutex);
            for (auto& task : cpu_waiting_tasks) {
                task.destroy();
            }
            cpu_waiting_tasks.clear();
        }
        // Cancel all ready and not yet started tasks
        {
            std::lock_guard<std::mutex> lock(cpu_queue_mutex);
            while (!cpu_task_queue.empty()) {
                auto task = cpu_task_queue.front();
                task.destroy();
                cpu_task_queue.pop();
            }
        }
        // Cancel all I/O tasks
        {
            std::lock_guard<std::mutex> lock(io_read_tasks_mutex);
            for (auto& [fd, tasks] : io_read_tasks) {
                for (auto& task : tasks) {
                    task.destroy();
                }
            }
            io_read_tasks.clear();
        }
        {
            std::lock_guard<std::mutex> lock(io_write_tasks_mutex);
            for (auto& [fd, tasks] : io_write_tasks) {
                for (auto& task : tasks) {
                    task.destroy();
                }
            }
            io_write_tasks.clear();
        }
        {
            std::lock_guard<std::mutex> lock(io_close_tasks_mutex);
            for (auto& [fd, tasks] : io_close_tasks) {
                for (auto& task : tasks) {
                    task.destroy();
                }
            }
            io_close_tasks.clear();
        }
        // Wait for the tasks in-progress to complete
        for (auto& worker : cpu_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        for (auto& worker : io_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        // Close kqueue backend
        ::close(kqueue_fd);
    }

    template<typename PromiseType>
    requires std::derived_from<PromiseType, promise_type>
    void schedule_io_task(IOResumeCondition condition, int client_fd, std::coroutine_handle<PromiseType> handle)
    {
        handle.promise().set_scheduler(shared_from_this());
        switch (condition) {
            case IOResumeCondition::read: {
                std::lock_guard<std::mutex> lock(io_read_tasks_mutex);
                io_read_tasks[client_fd].insert(handle);
                break;
            }
            case IOResumeCondition::write: {
                std::lock_guard<std::mutex> lock(io_write_tasks_mutex);
                io_write_tasks[client_fd].insert(handle);
                break;
            }
            case IOResumeCondition::close: {
                std::lock_guard<std::mutex> lock(io_close_tasks_mutex);
                io_close_tasks[client_fd].insert(handle);
                break;
            }
        }
    }

    template<typename PromiseType>
    requires std::derived_from<PromiseType, promise_type>
    void schedule_cpu_task(TaskResumeCondition condition, int client_fd, std::coroutine_handle<PromiseType> handle)
    {
        handle.promise().set_scheduler(shared_from_this());
        switch (condition) {
            case TaskResumeCondition::manual: {
                std::lock_guard<std::mutex> lock(cpu_waiting_tasks_mutex);
                cpu_waiting_tasks.insert(handle);
                break;
            }
            case TaskResumeCondition::ready: {
                std::lock_guard<std::mutex> lock(cpu_queue_mutex);
                cpu_task_queue.push(handle);
                cpu_queue_cv.notify_one();
                break;
            }
        }
    }

    void mark_as_ready(std::coroutine_handle<> handle)
    {
        {
            std::lock_guard<std::mutex> lock(cpu_waiting_tasks_mutex);
            auto it = cpu_waiting_tasks.find(handle);
            if (it != cpu_waiting_tasks.end()) {
                cpu_waiting_tasks.erase(it);
            }
        }
        {
            std::lock_guard<std::mutex> lock(cpu_queue_mutex);
            cpu_task_queue.push(handle);
            cpu_queue_cv.notify_one();
        }
    }
private:
    void cpu_worker_thread()
    {
        while(running.load(std::memory_order_relaxed))
        {
            std::coroutine_handle<> task;
            {
                std::unique_lock<std::mutex> lock(cpu_queue_mutex);
                cpu_queue_cv.wait(lock, [this] { return !cpu_task_queue.empty(); });
                task = cpu_task_queue.front();
                cpu_task_queue.pop();
            }
            {
                std::lock_guard<std::mutex> running_lock(cpu_running_tasks_mutex);
                cpu_running_tasks.insert(task);
            }
            task.resume();
            {
                std::lock_guard<std::mutex> running_lock(cpu_running_tasks_mutex);
                cpu_running_tasks.erase(task);
            }
        }
    }

    void io_worker_thread()
    {
        const timespec timeout = {.tv_sec = 0, .tv_nsec = 500000}; // 0.5 ms
        // Kqueue event loop
        while(running.load(std::memory_order_relaxed))
        {
            // Wait for events and resume corresponding tasks
            struct kevent events[64];
            int nev = ::kevent(kqueue_fd, nullptr, 0, events, 64, &timeout);
            for (int i = 0; i < nev; ++i) {
                int fd = events[i].ident;
                // Resume read tasks
                if (events[i].filter == EVFILT_READ) {
                    std::lock_guard<std::mutex> lock(io_read_tasks_mutex);
                    auto it = io_read_tasks.find(fd);
                    if (it != io_read_tasks.end()) {
                        for (auto& task : it->second) {
                            mark_as_ready(task);
                        }
                        io_read_tasks.erase(it);
                    }
                }
                // Resume write tasks
                else if (events[i].filter == EVFILT_WRITE) {
                    std::lock_guard<std::mutex> lock(io_write_tasks_mutex);
                    auto it = io_write_tasks.find(fd);
                    if (it != io_write_tasks.end()) {
                        for (auto& task : it->second) {
                            mark_as_ready(task);
                        }
                        io_write_tasks.erase(it);
                    }
                }
                // Resume close tasks
                else if (events[i].filter == EVFILT_READ && (events[i].flags & EV_EOF)) {
                    std::lock_guard<std::mutex> lock(io_close_tasks_mutex);
                    auto it = io_close_tasks.find(fd);
                    if (it != io_close_tasks.end()) {
                        for (auto& task : it->second) {
                            mark_as_ready(task);
                        }
                        io_close_tasks.erase(it);
                    }
                }
            }
        }
    }

    // Kqueue backend
    int kqueue_fd;

    using task = std::coroutine_handle<>;

    // Flag to control worker thread loops
    std::atomic<bool> running{true};

    // CPU-bound worker threads
    std::vector<std::thread> cpu_workers;

    // Shared task queue for the ready CPU-bound tasks
    std::mutex cpu_queue_mutex;
    std::condition_variable cpu_queue_cv;
    std::queue<task> cpu_task_queue;

    // Tasks currently running on CPU-bound worker threads
    std::mutex cpu_running_tasks_mutex;
    std::set<task> cpu_running_tasks;

    // Tasks waiting for external conditions (e.g., I/O completion)
    std::mutex cpu_waiting_tasks_mutex;
    std::set<task> cpu_waiting_tasks;

    // I/O-bound worker threads
    std::vector<std::thread> io_workers;

    // Hask table of the monitored clients and their associated I/O tasks grouped by required resume condition
    std::mutex io_read_tasks_mutex;
    std::unordered_map<int, std::set<task>> io_read_tasks;
    std::mutex io_write_tasks_mutex;
    std::unordered_map<int, std::set<task>> io_write_tasks;
    std::mutex io_close_tasks_mutex;
    std::unordered_map<int, std::set<task>> io_close_tasks;
};

class ClientHandler
{
public:
    struct promise_type
    {
        ClientHandler get_return_object() { return ClientHandler{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
    };
private:
    ClientHandler(std::coroutine_handle<promise_type> h) : handle(h) {}
    std::coroutine_handle<promise_type> handle;
};

struct Buffer
{
    std::unique_ptr<char[]> data;
    size_t length;
    size_t capacity;
};

class ReadGenerator
{
public:
    struct promise_type
    {
        std::coroutine_handle<> parent;
        std::unique_ptr<char[]> data;
        size_t length;
        size_t capacity;
        std::exception_ptr exception;
        ReadGenerator get_return_object() { return ReadGenerator{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        std::suspend_always yield_value(Buffer buf) noexcept {
            // Store buffer and length in promise
            data = std::move(buf.data);
            length = buf.length;
            capacity = buf.capacity;
            return {};
        }
        void unhandled_exception() noexcept {
            exception = std::current_exception();
        }
    };
private:
    ReadGenerator(std::coroutine_handle<promise_type> h) : handle(h) {}
    std::coroutine_handle<promise_type> handle;
};

ReadGenerator read_from_client(int client_fd) {
    Buffer buf;
    buf.capacity = 4096;
    buf.data = std::make_unique<char[]>(buf.capacity);
    buf.length = 0;

    while (true) {
        ssize_t n = ::recv(client_fd, buf.data.get() + buf.length, buf.capacity - buf.length, 0);
        if (n > 0) {
            buf.length += n;
        } else if (n == 0) {
            // Connection closed
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block, yield current buffer
                co_yield std::move(buf);
            } else {
                // Error
                throw std::runtime_error("recv() error");
            }
        }
    }

    co_return;
}

ClientHandler handle_client(int client_fd) {
    ReadGenerator reader = /* ... */;
    co_return;
}