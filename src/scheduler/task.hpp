#pragma once

#include <coroutine>
#include <memory>
#include <optional>
#include <vector>

class Scheduler;

class Task
{
public:
  struct promise_type
  {
    struct SharedState;

    using TaskHandle = std::coroutine_handle<promise_type>;

    promise_type();

    Task get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;

    struct FinalAwaiter
    {
      bool await_ready() const noexcept { return false; }
      void await_suspend(TaskHandle handle) const noexcept;
      void await_resume() const noexcept {}
    };

    FinalAwaiter final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;

    void set_scheduler(Scheduler* owner) noexcept;
    void set_sequence_key(std::optional<std::size_t> key) noexcept;

    bool register_continuation(std::coroutine_handle<> awaiting) noexcept;
    std::vector<std::coroutine_handle<>> take_continuations() noexcept;
    void rethrow_exception() const;

    Scheduler* scheduler;
    std::optional<std::size_t> sequence_key;
    std::shared_ptr<SharedState> state;
  };

  using handle_type = promise_type::TaskHandle;

  Task() noexcept = default;
  explicit Task(handle_type handle) noexcept;
  Task(Task&& other) noexcept;
  Task& operator=(Task&& other) noexcept;
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task();

  explicit operator bool() const noexcept;
  handle_type handle() const noexcept;
  handle_type release() noexcept;
  void reset() noexcept;
  void set_scheduler(Scheduler* scheduler) noexcept;

  struct awaiter
  {
    explicit awaiter(handle_type handle) noexcept;

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<> awaiting);
    void await_resume();

    handle_type task_handle() const noexcept { return handle; }

  private:
    handle_type handle;
    std::shared_ptr<promise_type::SharedState> state;
  };

  awaiter operator co_await() && noexcept;
  static Task adopt(handle_type handle) noexcept;

private:
  handle_type handle_{nullptr};
  friend class Scheduler;
};