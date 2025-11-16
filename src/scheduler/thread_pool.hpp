#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

class ThreadPool
{
public:
  explicit ThreadPool(std::size_t thread_count)
    : running_(true)
  {
    if (thread_count == 0)
    {
      throw std::invalid_argument("ThreadPool requires at least one thread");
    }

    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i)
    {
      workers_.emplace_back([this] { worker_loop(); });
    }
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  ~ThreadPool() { shutdown(); }

  void submit(std::function<void()> task)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_)
      {
        throw std::runtime_error("ThreadPool has been shut down");
      }
      tasks_.push(std::move(task));
    }
    cv_.notify_one();
  }

  void shutdown()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_)
      {
        return;
      }
      running_ = false;
    }
    cv_.notify_all();

    for (auto& worker : workers_)
    {
      if (worker.joinable())
      {
        worker.join();
      }
    }
  }

private:
  void worker_loop()
  {
    while (true)
    {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !tasks_.empty() || !running_; });

        if (!running_ && tasks_.empty())
        {
          return;
        }

        task = std::move(tasks_.front());
        tasks_.pop();
      }

      try
      {
        task();
      }
      catch (...)
      {
        // Swallow exceptions to keep worker threads alive.
      }
    }
  }

  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<std::function<void()>> tasks_;
  bool running_;
};
