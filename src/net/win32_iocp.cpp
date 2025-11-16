#include "win32_iocp.hpp"

#include <system_error>

namespace net
{
  namespace
  {
    constexpr DWORD kShutdownBytes = 0xFFFFFFFF;
  }

  IocpService::~IocpService()
  {
    shutdown();
  }

  bool IocpService::initialize(CompletionCallback callback, unsigned int concurrency)
  {
    if (running_.load(std::memory_order_acquire))
    {
      return false;
    }

    callback_ = std::move(callback);
    if (!callback_)
    {
      return false;
    }

    if (!wsa_initialized_)
    {
      const int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa_data_);
      if (wsa_result != 0)
      {
        return false;
      }
      wsa_initialized_ = true;
    }

    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, concurrency == 0 ? 0 : concurrency);
    if (port == nullptr)
    {
      return false;
    }

    port_ = port;
    running_.store(true, std::memory_order_release);
    worker_ = std::thread(&IocpService::worker_main, this);
    return true;
  }

  void IocpService::shutdown()
  {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
    {
      return;
    }

    if (port_)
    {
      PostQueuedCompletionStatus(port_, kShutdownBytes, 0, nullptr);
    }

    if (worker_.joinable())
    {
      worker_.join();
    }

    if (port_)
    {
      CloseHandle(port_);
      port_ = nullptr;
    }

    callback_ = nullptr;

    if (wsa_initialized_)
    {
      WSACleanup();
      wsa_initialized_ = false;
    }
  }

  bool IocpService::associate_handle(HANDLE handle, ULONG_PTR completion_key) const noexcept
  {
    if (!port_ || handle == nullptr)
    {
      return false;
    }

    HANDLE associated = CreateIoCompletionPort(handle, port_, completion_key, 0);
    return associated == port_;
  }

  bool IocpService::post_status(ULONG_PTR completion_key, DWORD bytes_transferred,
                                OVERLAPPED* overlapped) const noexcept
  {
    if (!port_)
    {
      return false;
    }
    return PostQueuedCompletionStatus(port_, bytes_transferred, completion_key, overlapped) != FALSE;
  }

  void IocpService::worker_main()
  {
    while (true)
    {
      Completion completion;
      DWORD bytes = 0;
      ULONG_PTR key = 0;
      OVERLAPPED* overlapped = nullptr;

      const BOOL ok = GetQueuedCompletionStatus(port_, &bytes, &key, &overlapped, INFINITE);
      completion.bytes_transferred = bytes;
      completion.completion_key = key;
      completion.overlapped = overlapped;

      if (!ok)
      {
        completion.error = std::error_code(GetLastError(), std::system_category());
      }
      else
      {
        completion.error.clear();
      }

      if (!running_.load(std::memory_order_acquire) && bytes == kShutdownBytes && overlapped == nullptr)
      {
        break;
      }

      if (callback_)
      {
        callback_(completion);
      }
    }
  }
} // namespace net
