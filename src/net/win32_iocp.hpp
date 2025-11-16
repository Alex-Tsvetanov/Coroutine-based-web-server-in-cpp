#pragma once

#ifndef _WIN32
#error "IOCP integration is only supported on Windows builds"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <system_error>
#include <thread>

namespace net
{
  class IocpService
  {
  public:
    struct Completion
    {
      DWORD bytes_transferred{0};
      std::error_code error{};
      ULONG_PTR completion_key{0};
      OVERLAPPED* overlapped{nullptr};
    };

    using CompletionCallback = std::function<void(const Completion&)>;

    IocpService() = default;
    ~IocpService();

    IocpService(const IocpService&) = delete;
    IocpService& operator=(const IocpService&) = delete;

    bool initialize(CompletionCallback callback, unsigned int concurrency = 0);
    void shutdown();

    HANDLE port() const noexcept { return port_; }
    bool associate_handle(HANDLE handle, ULONG_PTR completion_key) const noexcept;
    bool post_status(ULONG_PTR completion_key, DWORD bytes_transferred = 0,
                     OVERLAPPED* overlapped = nullptr) const noexcept;

  private:
    void worker_main();

    CompletionCallback callback_{};
    HANDLE port_{nullptr};
    std::thread worker_{};
    std::atomic<bool> running_{false};
    bool wsa_initialized_{false};
    WSADATA wsa_data_{};
  };
} // namespace net
