#pragma once

#include "task.hpp"
#include "thread_pool.hpp"

#include "../net/io_reactor.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

enum class IOCondition
{
  read,
  write,
  close
};

class Scheduler: public std::enable_shared_from_this<Scheduler>
{
public:
  using TaskHandle = Task::handle_type;
  using ReactorEvent = net::IoReactor::Event;
  using ReactorCallback = std::function<void(const ReactorEvent&)>;
  using IoHandle = std::uintptr_t;

  explicit Scheduler(std::size_t worker_count);
  ~Scheduler();

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  TaskHandle schedule(Task task, std::optional<std::size_t> sequence_key = std::nullopt,
                      std::span<const TaskHandle> dependencies = {});

  struct YieldAwaiter
  {
    explicit YieldAwaiter(Scheduler& scheduler) noexcept
      : scheduler_(scheduler)
    {}

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) const;
    void await_resume() const noexcept {}

  private:
    Scheduler& scheduler_;
  };

  YieldAwaiter yield() noexcept { return YieldAwaiter{*this}; }

  template <typename Clock, typename Duration>
  [[nodiscard]] bool should_yield(const std::chrono::time_point<Clock, Duration>& start,
                                  std::chrono::nanoseconds budget) const noexcept
  {
    return std::chrono::steady_clock::now() - start >= budget;
  }

  struct IoAwaiter
  {
    IoAwaiter(Scheduler& scheduler, IoHandle handle, IOCondition condition) noexcept
      : scheduler_(scheduler)
      , handle_(handle)
      , condition_(condition)
    {}

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) const;
    void await_resume() const noexcept {}

  private:
    Scheduler& scheduler_;
    IoHandle handle_;
    IOCondition condition_;
  };

  IoAwaiter wait_io(IoHandle handle, IOCondition condition) noexcept { return IoAwaiter{*this, handle, condition}; }
  IoAwaiter wait_io_fd(int fd, IOCondition condition) noexcept
  {
    return IoAwaiter{*this, static_cast<IoHandle>(fd), condition};
  }

  void notify_io_ready(IoHandle handle, IOCondition condition);
  void wake_task(TaskHandle handle);

  void wait_idle();

  void set_io_reactor(std::unique_ptr<net::IoReactor> reactor);
  [[nodiscard]] static std::unique_ptr<net::IoReactor> create_default_reactor();
  net::IoReactor* io_reactor() const noexcept;

  void register_reactor_context(void* context, ReactorCallback callback);
  void unregister_reactor_context(void* context);

  static std::uintptr_t encode_io_key(int fd, IOCondition condition) noexcept;

  // Hooks used by Task internals
  bool await_subtask(Task::awaiter& aw, std::coroutine_handle<>);
  void on_task_finished(TaskHandle handle) noexcept;

private:
  struct TaskState
  {
    TaskHandle handle{};
    std::optional<std::size_t> sequence_key{};
    std::size_t pending_dependencies{0};
    bool waiting_io{false};
    bool running{false};
    bool queued{false};
    bool waiting_sequence{false};
    bool reschedule_requested{false};
  };

  struct SequenceState
  {
    TaskHandle active{};
    std::deque<TaskHandle> waiters;
  };

  struct IoKey
  {
    IoHandle handle;
    IOCondition condition;

    bool operator==(const IoKey& other) const noexcept
    {
      return handle == other.handle && condition == other.condition;
    }
  };

  struct IoKeyHasher
  {
    std::size_t operator()(const IoKey& key) const noexcept
    {
      return std::hash<IoHandle>{}(key.handle) ^ (std::hash<int>{}(static_cast<int>(key.condition)) << 1);
    }
  };

  void dispatch(TaskHandle handle);
  void resume_task(TaskHandle handle);

  void register_dependency(TaskHandle dependent, TaskHandle dependency);
  void resolve_dependency(TaskHandle dependency);

  void try_schedule(TaskHandle handle);
  void enqueue_after_sequence(TaskHandle handle);
  void release_sequence(std::size_t key, TaskHandle finished);

  void mark_waiting_io(TaskHandle handle, IoHandle io_handle, IOCondition condition);

  void request_reschedule(TaskHandle handle) noexcept;

  TaskState* lookup_state(TaskHandle handle);
  void handle_reactor_event(const ReactorEvent& event);
  static std::pair<int, IOCondition> decode_io_key(std::uintptr_t key) noexcept;
  void stop_reactor();

  ThreadPool workers_;

  std::atomic<bool> shutting_down_{false};

  mutable std::mutex state_mutex_;
  std::unordered_map<void*, TaskState> task_states_;

  std::mutex dependency_mutex_;
  std::unordered_map<void*, std::vector<TaskHandle>> dependency_waiters_;

  std::mutex sequence_mutex_;
  std::unordered_map<std::size_t, SequenceState> sequences_;

  std::mutex io_mutex_;
  std::unordered_map<IoKey, std::deque<TaskHandle>, IoKeyHasher> io_waiters_;

  std::atomic<std::size_t> active_tasks_{0};
  std::mutex idle_mutex_;
  std::condition_variable idle_cv_;

  std::unique_ptr<net::IoReactor> reactor_;
  mutable std::mutex reactor_mutex_;
  std::unordered_map<void*, ReactorCallback> reactor_contexts_;
};
