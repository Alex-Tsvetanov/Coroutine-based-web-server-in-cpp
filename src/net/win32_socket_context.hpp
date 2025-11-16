#pragma once

#ifdef _WIN32

#include "../scheduler/scheduler.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <coroutine>
#include <span>
#include <system_error>

namespace net
{
  class Win32SocketContext
  {
  public:
    Win32SocketContext(Scheduler& scheduler, SOCKET socket) noexcept;
    ~Win32SocketContext();

    Win32SocketContext(const Win32SocketContext&) = delete;
    Win32SocketContext& operator=(const Win32SocketContext&) = delete;
    Win32SocketContext(Win32SocketContext&& other) noexcept;
    Win32SocketContext& operator=(Win32SocketContext&& other) noexcept;

    SOCKET socket() const noexcept { return socket_; }
    Scheduler::IoHandle key() const noexcept { return key_; }
    Scheduler& scheduler() const noexcept { return scheduler_; }

    bool ensure_registered();

    void set_last_result(std::size_t bytes, std::error_code error) noexcept;
    std::size_t last_bytes() const noexcept { return last_bytes_; }
    std::error_code last_error() const noexcept { return last_error_; }

  private:
    void close_socket() noexcept;

    Scheduler& scheduler_;
    SOCKET socket_{INVALID_SOCKET};
    Scheduler::IoHandle key_{0};
    bool registered_{false};
    std::size_t last_bytes_{0};
    std::error_code last_error_{};
  };

  class SocketReceiveAwaiter
  {
  public:
    SocketReceiveAwaiter(Win32SocketContext& context, std::span<char> buffer) noexcept;

    bool await_ready() const noexcept { return buffer_.empty(); }
    bool await_suspend(std::coroutine_handle<> handle);
    std::size_t await_resume();

  private:
    void on_event(const Scheduler::ReactorEvent& event);

    Win32SocketContext& context_;
    std::span<char> buffer_;
    struct OverlappedWrapper: OVERLAPPED
    {
      SocketReceiveAwaiter* owner{nullptr};
    } overlapped_{};
    std::error_code immediate_error_{};
    DWORD immediate_bytes_{0};
    std::coroutine_handle<> awaiting_{};
    bool suspended_{false};
  };

  class SocketSendAwaiter
  {
  public:
    SocketSendAwaiter(Win32SocketContext& context, std::span<const char> buffer) noexcept;

    bool await_ready() const noexcept { return buffer_.empty(); }
    bool await_suspend(std::coroutine_handle<> handle);
    std::size_t await_resume();

  private:
    void on_event(const Scheduler::ReactorEvent& event);

    Win32SocketContext& context_;
    std::span<const char> buffer_;
    struct OverlappedWrapper: OVERLAPPED
    {
      SocketSendAwaiter* owner{nullptr};
    } overlapped_{};
    std::error_code immediate_error_{};
    DWORD immediate_bytes_{0};
    std::coroutine_handle<> awaiting_{};
    bool suspended_{false};
  };

  SocketReceiveAwaiter async_recv(Win32SocketContext& context, std::span<char> buffer);
  SocketSendAwaiter async_send(Win32SocketContext& context, std::span<const char> buffer);

} // namespace net

#endif // _WIN32
