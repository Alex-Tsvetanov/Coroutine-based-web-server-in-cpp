#include "scheduler/scheduler.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

namespace
{
  Task child_step_task(Scheduler& scheduler, int id)
  {
    std::cout << "[child-" << id << "] start" << std::endl;
    for (int iteration = 0; iteration < 2; ++iteration)
    {
      std::cout << "[child-" << id << "] iteration " << iteration << std::endl;
      co_await scheduler.yield();
    }
    std::cout << "[child-" << id << "] complete" << std::endl;
    co_return;
  }

  Task io_child_task(Scheduler& scheduler, int fd)
  {
    std::cout << "[io-child] waiting for fd " << fd << std::endl;
    co_await scheduler.wait_io(fd, IOCondition::read);
    std::cout << "[io-child] resumed after fd " << fd << " became readable" << std::endl;
    co_return;
  }

  Task parent_task(Scheduler& scheduler, int fd)
  {
    std::cout << "[parent] launch child coroutine" << std::endl;

    auto child = child_step_task(scheduler, 1);
    child.set_scheduler(&scheduler);
    co_await std::move(child);

    std::cout << "[parent] child finished, delegating IO work" << std::endl;
    auto io_child = io_child_task(scheduler, fd);
    io_child.set_scheduler(&scheduler);
    co_await std::move(io_child);

    std::cout << "[parent] done" << std::endl;
    co_return;
  }

  Task sequenced_task(Scheduler& scheduler, std::string_view name, int chunk_count)
  {
    for (int chunk = 0; chunk < chunk_count; ++chunk)
    {
      std::cout << '[' << name << "] chunk " << chunk << std::endl;
      co_await scheduler.yield();
    }
    std::cout << '[' << name << "] complete" << std::endl;
    co_return;
  }

  Task cpu_cooperative_task(Scheduler& scheduler, int id)
  {
    std::cout << "[cpu-" << id << "] start" << std::endl;
    auto interval_start = std::chrono::steady_clock::now();

    for (int iteration = 0; iteration < 5; ++iteration)
    {
      volatile std::size_t accumulator = 0;
      for (int spin = 0; spin < 200'000; ++spin)
      {
        accumulator += static_cast<std::size_t>(spin);
      }

      if (scheduler.should_yield(interval_start, 5ms))
      {
        std::cout << "[cpu-" << id << "] yielding after iteration " << iteration << std::endl;
        interval_start = std::chrono::steady_clock::now();
        co_await scheduler.yield();
      }
    }

    std::cout << "[cpu-" << id << "] complete" << std::endl;
    co_return;
  }
} // namespace

int main()
{
  const auto hardware_threads = std::max(2u, std::thread::hardware_concurrency());
  auto scheduler = std::make_shared<Scheduler>(static_cast<std::size_t>(hardware_threads));

  const int fake_fd = 42;

  scheduler->schedule(parent_task(*scheduler, fake_fd));

  scheduler->schedule(cpu_cooperative_task(*scheduler, 0));
  scheduler->schedule(cpu_cooperative_task(*scheduler, 1));

  auto first_sequence = scheduler->schedule(sequenced_task(*scheduler, "connection-1", 3), 1);
  scheduler->schedule(sequenced_task(*scheduler, "connection-1-followup", 2), 1);
  scheduler->schedule(sequenced_task(*scheduler, "connection-2", 2), 2);

  std::array<Scheduler::TaskHandle, 1> dependencies{first_sequence};
  scheduler->schedule(sequenced_task(*scheduler, "dependent-task", 1), std::nullopt, dependencies);

  std::thread notifier([scheduler, fake_fd]() {
    std::this_thread::sleep_for(200ms);
    std::cout << "[notifier] signalling readiness on fd " << fake_fd << std::endl;
    scheduler->notify_io_ready(fake_fd, IOCondition::read);
  });

  scheduler->wait_idle();
  notifier.join();

  std::cout << "All scheduled work completed." << std::endl;
  return 0;
}