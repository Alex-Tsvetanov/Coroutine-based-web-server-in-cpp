#include "scheduler.hpp"

#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef _WIN32
#include "../net/win32_iocp_reactor.hpp"
#endif

struct Task::promise_type::SharedState
{
  std::mutex mutex;
  std::vector<std::coroutine_handle<>> continuations;
  bool completed{false};
  std::exception_ptr exception;
};

Task::promise_type::promise_type()
  : scheduler(nullptr)
  , sequence_key(std::nullopt)
  , state(std::make_shared<SharedState>())
{}

void Scheduler::set_io_reactor(std::unique_ptr<net::IoReactor> reactor)
{
  stop_reactor();

  if (!reactor)
  {
    return;
  }

  std::weak_ptr<Scheduler> weak = shared_from_this();
  auto callback = [weak](const ReactorEvent& event) {
    if (auto self = weak.lock())
    {
      self->handle_reactor_event(event);
    }
  };

  if (!reactor->start(callback))
  {
    reactor->stop();
    throw std::runtime_error("Failed to start IO reactor");
  }

  std::lock_guard lock(reactor_mutex_);
  reactor_ = std::move(reactor);
}

std::unique_ptr<net::IoReactor> Scheduler::create_default_reactor()
{
#ifdef _WIN32
  auto derived = std::make_unique<net::Win32IocpReactor>();
  return std::unique_ptr<net::IoReactor>(derived.release());
#else
  return nullptr;
#endif
}

net::IoReactor* Scheduler::io_reactor() const noexcept
{
  std::lock_guard lock(reactor_mutex_);
  return reactor_.get();
}

void Scheduler::register_reactor_context(void* context, ReactorCallback callback)
{
  if (context == nullptr || !callback)
  {
    return;
  }

  std::lock_guard lock(reactor_mutex_);
  reactor_contexts_[context] = std::move(callback);
}

void Scheduler::unregister_reactor_context(void* context)
{
  if (context == nullptr)
  {
    return;
  }

  std::lock_guard lock(reactor_mutex_);
  reactor_contexts_.erase(context);
}

std::uintptr_t Scheduler::encode_io_key(int fd, IOCondition condition) noexcept
{
  constexpr std::uintptr_t mask = 0x3;
  auto condition_bits = static_cast<std::uintptr_t>(static_cast<int>(condition) & mask);
  return (static_cast<std::uintptr_t>(fd) << 2) | condition_bits;
}

std::pair<int, IOCondition> Scheduler::decode_io_key(std::uintptr_t key) noexcept
{
  constexpr std::uintptr_t mask = 0x3;
  auto condition_bits = static_cast<std::uintptr_t>(key & mask);
  IOCondition condition = static_cast<IOCondition>(condition_bits);
  int fd = static_cast<int>(key >> 2);
  return {fd, condition};
}

void Scheduler::handle_reactor_event(const ReactorEvent& event)
{
  ReactorCallback callback;
  if (event.context)
  {
    std::lock_guard lock(reactor_mutex_);
    auto it = reactor_contexts_.find(event.context);
    if (it != reactor_contexts_.end())
    {
      callback = it->second;
    }
  }

  if (callback)
  {
    callback(event);
    return;
  }

  auto [fd, condition] = decode_io_key(event.key);
  if (fd >= 0)
  {
    notify_io_ready(fd, condition);
  }
}

void Scheduler::stop_reactor()
{
  std::unique_ptr<net::IoReactor> reactor;
  {
    std::lock_guard lock(reactor_mutex_);
    reactor = std::move(reactor_);
    reactor_contexts_.clear();
  }

  if (reactor)
  {
    reactor->stop();
  }
}

Task Task::promise_type::get_return_object() noexcept
{
  return Task{TaskHandle::from_promise(*this)};
}

std::suspend_always Task::promise_type::initial_suspend() noexcept
{
  return {};
}

Task::promise_type::FinalAwaiter Task::promise_type::final_suspend() noexcept
{
  return {};
}

void Task::promise_type::return_void() noexcept
{}

void Task::promise_type::unhandled_exception() noexcept
{
  if (state)
  {
    state->exception = std::current_exception();
  }
}

void Task::promise_type::set_scheduler(Scheduler* owner) noexcept
{
  scheduler = owner;
}

void Task::promise_type::set_sequence_key(std::optional<std::size_t> key) noexcept
{
  sequence_key = key;
}

bool Task::promise_type::register_continuation(std::coroutine_handle<> awaiting) noexcept
{
  if (!awaiting || !state)
  {
    return false;
  }

  std::lock_guard<std::mutex> lock(state->mutex);
  if (state->completed)
  {
    return false;
  }
  state->continuations.push_back(awaiting);
  return true;
}

std::vector<std::coroutine_handle<>> Task::promise_type::take_continuations() noexcept
{
  if (!state)
  {
    return {};
  }

  std::vector<std::coroutine_handle<>> continuations;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->completed = true;
    continuations = std::move(state->continuations);
    state->continuations.clear();
  }
  return continuations;
}

void Task::promise_type::rethrow_exception() const
{
  if (state && state->exception)
  {
    std::rethrow_exception(state->exception);
  }
}

Task::Task(handle_type handle) noexcept
  : handle_(handle)
{}

Task::Task(Task&& other) noexcept
  : handle_(other.release())
{}

Task& Task::operator=(Task&& other) noexcept
{
  if (this != &other)
  {
    reset();
    handle_ = other.release();
  }
  return *this;
}

Task::~Task()
{
  reset();
}

Task::operator bool() const noexcept
{
  return handle_ != nullptr;
}

Task::handle_type Task::handle() const noexcept
{
  return handle_;
}

Task::handle_type Task::release() noexcept
{
  auto tmp = handle_;
  handle_ = nullptr;
  return tmp;
}

void Task::reset() noexcept
{
  if (handle_)
  {
    handle_.destroy();
    handle_ = nullptr;
  }
}

void Task::set_scheduler(Scheduler* scheduler) noexcept
{
  if (handle_)
  {
    handle_.promise().set_scheduler(scheduler);
  }
}

Task::awaiter Task::operator co_await() && noexcept
{
  return awaiter{release()};
}

Task Task::adopt(handle_type handle) noexcept
{
  return Task{handle};
}

Task::awaiter::awaiter(handle_type handle) noexcept
  : handle(handle)
  , state(handle ? handle.promise().state : nullptr)
{}

bool Task::awaiter::await_ready() const noexcept
{
  return handle == nullptr || handle.done();
}

bool Task::awaiter::await_suspend(std::coroutine_handle<> awaiting)
{
  if (!handle)
  {
    return false;
  }

  auto& promise = handle.promise();

  if (!state)
  {
    state = promise.state;
  }

  const bool registered = promise.register_continuation(awaiting);
  if (!registered)
  {
    return false;
  }

  if (promise.scheduler == nullptr)
  {
    handle.resume();
    return !handle.done();
  }

  return promise.scheduler->await_subtask(*this, awaiting);
}

void Task::awaiter::await_resume()
{
  if (handle)
  {
    handle.promise().rethrow_exception();
  }
}

void Task::promise_type::FinalAwaiter::await_suspend(TaskHandle handle) const noexcept
{
  auto& promise = handle.promise();

  if (promise.scheduler != nullptr)
  {
    promise.scheduler->on_task_finished(handle);
    return;
  }

  auto continuations = promise.take_continuations();
  for (auto continuation : continuations)
  {
    if (continuation)
    {
      continuation.resume();
    }
  }

  handle.destroy();
}

Scheduler::Scheduler(std::size_t worker_count)
  : workers_(worker_count)
{}

Scheduler::~Scheduler()
{
  shutting_down_.store(true, std::memory_order_relaxed);

  stop_reactor();

  workers_.shutdown();

  std::vector<TaskHandle> remaining;
  {
    std::lock_guard lock(state_mutex_);
    for (auto& [_, state] : task_states_)
    {
      if (state.handle)
      {
        remaining.push_back(state.handle);
      }
    }
    task_states_.clear();
  }

  for (auto handle : remaining)
  {
    if (handle)
    {
      handle.destroy();
    }
  }
}

Scheduler::TaskHandle Scheduler::schedule(Task task, std::optional<std::size_t> sequence_key,
                                          std::span<const TaskHandle> dependencies)
{
  auto handle = task.release();
  if (!handle)
  {
    throw std::invalid_argument("Cannot schedule an empty task");
  }

  handle.promise().set_scheduler(this);
  handle.promise().set_sequence_key(sequence_key);

  {
    std::lock_guard lock(state_mutex_);
    auto key = handle.address();
    auto& state = task_states_[key];
    state.handle = handle;
    state.sequence_key = sequence_key;
    state.pending_dependencies = dependencies.size();
    state.waiting_io = false;
    state.running = false;
    state.queued = false;
    state.waiting_sequence = false;
    state.reschedule_requested = false;
  }

  active_tasks_.fetch_add(1, std::memory_order_relaxed);

  for (auto dependency : dependencies)
  {
    register_dependency(handle, dependency);
  }

  try_schedule(handle);

  return handle;
}

void Scheduler::notify_io_ready(IoHandle handle_value, IOCondition condition)
{
  std::deque<TaskHandle> ready;
  {
    std::lock_guard lock(io_mutex_);
    IoKey key{handle_value, condition};
    auto it = io_waiters_.find(key);
    if (it != io_waiters_.end())
    {
      ready = std::move(it->second);
      io_waiters_.erase(it);
    }
  }

  for (auto handle : ready)
  {
    {
      std::lock_guard state_lock(state_mutex_);
      if (auto state = lookup_state(handle))
      {
        state->waiting_io = false;
      }
    }
    try_schedule(handle);
  }
}

void Scheduler::wake_task(TaskHandle handle)
{
  request_reschedule(handle);
}

void Scheduler::wait_idle()
{
  std::unique_lock lock(idle_mutex_);
  idle_cv_.wait(lock, [this] { return active_tasks_.load(std::memory_order_relaxed) == 0; });
}

bool Scheduler::await_subtask(Task::awaiter& aw, std::coroutine_handle<>)
{
  auto handle = aw.task_handle();
  if (!handle)
  {
    return false;
  }

  handle.promise().set_scheduler(this);

  {
    std::lock_guard lock(state_mutex_);
    auto key = handle.address();
    auto [it, inserted] = task_states_.try_emplace(key);
    auto& state = it->second;
    state.handle = handle;
    if (!state.sequence_key && handle.promise().sequence_key)
    {
      state.sequence_key = handle.promise().sequence_key;
    }
    if (inserted)
    {
      active_tasks_.fetch_add(1, std::memory_order_relaxed);
    }
    state.pending_dependencies = 0;
    state.waiting_io = false;
    state.running = false;
    state.queued = false;
    state.waiting_sequence = false;
    state.reschedule_requested = false;
  }

  try_schedule(handle);

  return true;
}

void Scheduler::on_task_finished(TaskHandle handle) noexcept
{
  std::shared_ptr<Task::promise_type::SharedState> shared_state;
  std::optional<std::size_t> sequence_key;

  {
    std::lock_guard lock(state_mutex_);
    auto it = task_states_.find(handle.address());
    if (it != task_states_.end())
    {
      shared_state = handle.promise().state;
      sequence_key = it->second.sequence_key;
      task_states_.erase(it);
      active_tasks_.fetch_sub(1, std::memory_order_relaxed);
    }
    else
    {
      shared_state = handle.promise().state;
    }
  }

  if (sequence_key)
  {
    release_sequence(*sequence_key, handle);
  }

  resolve_dependency(handle);

  if (shared_state)
  {
    std::vector<std::coroutine_handle<>> continuations;
    {
      std::lock_guard lock(shared_state->mutex);
      shared_state->completed = true;
      continuations = std::move(shared_state->continuations);
      shared_state->continuations.clear();
    }

    for (auto continuation : continuations)
    {
      if (!continuation)
      {
        continue;
      }
      auto task_handle = Task::handle_type::from_address(continuation.address());
      request_reschedule(task_handle);
    }
  }

  handle.destroy();

  {
    std::lock_guard lock(idle_mutex_);
    if (active_tasks_.load(std::memory_order_relaxed) == 0)
    {
      idle_cv_.notify_all();
    }
  }
}

void Scheduler::dispatch(TaskHandle handle)
{
  if (!handle || shutting_down_.load(std::memory_order_relaxed))
  {
    return;
  }

  auto self = shared_from_this();
  workers_.submit([self, handle] { self->resume_task(handle); });
}

void Scheduler::resume_task(TaskHandle handle)
{
  if (!handle)
  {
    return;
  }

  {
    std::lock_guard lock(state_mutex_);
    if (auto state = lookup_state(handle))
    {
      state->running = true;
      state->queued = false;
    }
  }

  handle.resume();

  if (!handle.done())
  {
    {
      std::lock_guard lock(state_mutex_);
      if (auto state = lookup_state(handle))
      {
        state->running = false;
      }
    }
    try_schedule(handle);
  }
}

void Scheduler::register_dependency(TaskHandle dependent, TaskHandle dependency)
{
  if (!dependency)
  {
    return;
  }

  std::lock_guard lock(dependency_mutex_);
  dependency_waiters_[dependency.address()].push_back(dependent);
}

void Scheduler::resolve_dependency(TaskHandle dependency)
{
  std::vector<TaskHandle> dependents;
  {
    std::lock_guard lock(dependency_mutex_);
    auto it = dependency_waiters_.find(dependency.address());
    if (it != dependency_waiters_.end())
    {
      dependents = std::move(it->second);
      dependency_waiters_.erase(it);
    }
  }

  for (auto dependent : dependents)
  {
    {
      std::lock_guard state_lock(state_mutex_);
      if (auto state = lookup_state(dependent))
      {
        if (state->pending_dependencies > 0)
        {
          --state->pending_dependencies;
        }
      }
    }
    try_schedule(dependent);
  }
}

void Scheduler::try_schedule(TaskHandle handle)
{
  std::optional<std::size_t> sequence_key;
  bool dispatch_now = false;

  {
    std::lock_guard lock(state_mutex_);
    auto state = lookup_state(handle);
    if (!state)
    {
      return;
    }

    if (state->running || state->queued || state->waiting_io || state->pending_dependencies > 0 ||
        state->waiting_sequence)
    {
      return;
    }

    state->reschedule_requested = false;

    if (state->sequence_key)
    {
      sequence_key = state->sequence_key;
      state->waiting_sequence = true;
    }
    else
    {
      state->queued = true;
      dispatch_now = true;
    }
  }

  if (sequence_key)
  {
    enqueue_after_sequence(handle);
  }
  else if (dispatch_now)
  {
    dispatch(handle);
  }
}

void Scheduler::enqueue_after_sequence(TaskHandle handle)
{
  std::size_t key_value = 0;
  bool run_immediately = false;

  {
    std::lock_guard state_lock(state_mutex_);
    auto state = lookup_state(handle);
    if (!state || !state->sequence_key)
    {
      return;
    }
    key_value = *state->sequence_key;
  }

  {
    std::lock_guard seq_lock(sequence_mutex_);
    auto& sequence = sequences_[key_value];
    if (!sequence.active)
    {
      sequence.active = handle;
      run_immediately = true;
    }
    else
    {
      sequence.waiters.push_back(handle);
    }
  }

  if (run_immediately)
  {
    {
      std::lock_guard state_lock(state_mutex_);
      if (auto state = lookup_state(handle))
      {
        state->waiting_sequence = false;
        state->queued = true;
      }
    }
    dispatch(handle);
  }
}

void Scheduler::release_sequence(std::size_t key, TaskHandle finished)
{
  TaskHandle next;
  {
    std::lock_guard seq_lock(sequence_mutex_);
    auto it = sequences_.find(key);
    if (it == sequences_.end())
    {
      return;
    }

    auto& sequence = it->second;
    if (!sequence.active || sequence.active.address() != finished.address())
    {
      return;
    }

    if (!sequence.waiters.empty())
    {
      next = sequence.waiters.front();
      sequence.waiters.pop_front();
      sequence.active = next;
    }
    else
    {
      sequence.active = {};
      sequences_.erase(it);
    }
  }

  if (next)
  {
    {
      std::lock_guard state_lock(state_mutex_);
      if (auto state = lookup_state(next))
      {
        state->waiting_sequence = false;
        state->queued = true;
      }
    }
    dispatch(next);
  }
}

void Scheduler::mark_waiting_io(TaskHandle handle, IoHandle io_handle, IOCondition condition)
{
  if (!handle)
  {
    return;
  }

  {
    std::lock_guard state_lock(state_mutex_);
    if (auto state = lookup_state(handle))
    {
      state->waiting_io = true;
      state->running = false;
      state->queued = false;
    }
  }

  IoKey key{io_handle, condition};
  {
    std::lock_guard io_lock(io_mutex_);
    io_waiters_[key].push_back(handle);
  }
}

void Scheduler::request_reschedule(TaskHandle handle) noexcept
{
  {
    std::lock_guard lock(state_mutex_);
    if (auto state = lookup_state(handle))
    {
      if (state->running)
      {
        state->reschedule_requested = true;
        return;
      }
    }
  }

  try_schedule(handle);
}

Scheduler::TaskState* Scheduler::lookup_state(TaskHandle handle)
{
  auto it = task_states_.find(handle.address());
  if (it == task_states_.end())
  {
    return nullptr;
  }
  return &it->second;
}

void Scheduler::YieldAwaiter::await_suspend(std::coroutine_handle<> handle) const
{
  scheduler_.request_reschedule(Task::handle_type::from_address(handle.address()));
}

void Scheduler::IoAwaiter::await_suspend(std::coroutine_handle<> handle) const
{
  scheduler_.mark_waiting_io(Task::handle_type::from_address(handle.address()), handle_, condition_);
}
