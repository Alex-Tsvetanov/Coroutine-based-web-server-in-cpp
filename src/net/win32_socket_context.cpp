#include "win32_socket_context.hpp"

#ifdef _WIN32

#include <cstring>
#include <system_error>

namespace net
{
  namespace
  {
    Scheduler::IoHandle make_io_handle(SOCKET socket) noexcept
    {
      return reinterpret_cast<Scheduler::IoHandle>(socket);
    }

    std::error_code make_wsa_error(int code) noexcept
    {
      return std::error_code(code, std::system_category());
    }
  } // namespace

  Win32SocketContext::Win32SocketContext(Scheduler& scheduler, SOCKET socket) noexcept
    : scheduler_(scheduler)
    , socket_(socket)
    , key_(make_io_handle(socket))
  {}

  Win32SocketContext::~Win32SocketContext()
  {
    close_socket();
  }

  Win32SocketContext::Win32SocketContext(Win32SocketContext&& other) noexcept
    : scheduler_(other.scheduler_)
    , socket_(other.socket_)
    , key_(other.key_)
    , registered_(other.registered_)
    , last_bytes_(other.last_bytes_)
    , last_error_(other.last_error_)
  {
    other.socket_ = INVALID_SOCKET;
    other.registered_ = false;
  }

  Win32SocketContext& Win32SocketContext::operator=(Win32SocketContext&& other) noexcept
  {
    if (this == &other)
    {
      return *this;
    }

    close_socket();

    socket_ = other.socket_;
    key_ = other.key_;
    registered_ = other.registered_;
    last_bytes_ = other.last_bytes_;
    last_error_ = other.last_error_;

    other.socket_ = INVALID_SOCKET;
    other.registered_ = false;

    return *this;
  }

  void Win32SocketContext::close_socket() noexcept
  {
    if (socket_ != INVALID_SOCKET)
    {
      closesocket(socket_);
      socket_ = INVALID_SOCKET;
    }
  }

  bool Win32SocketContext::ensure_registered()
  {
    if (registered_)
    {
      return true;
    }

    auto* reactor = scheduler_.io_reactor();
    if (!reactor)
    {
      return false;
    }

    if (!reactor->register_handle(reinterpret_cast<std::uintptr_t>(socket_), key_))
    {
      return false;
    }

    registered_ = true;
    return true;
  }

  void Win32SocketContext::set_last_result(std::size_t bytes, std::error_code error) noexcept
  {
    last_bytes_ = bytes;
    last_error_ = error;
  }

  SocketReceiveAwaiter::SocketReceiveAwaiter(Win32SocketContext& context, std::span<char> buffer) noexcept
    : context_(context)
    , buffer_(buffer)
  {
    std::memset(&overlapped_, 0, sizeof(overlapped_));
    overlapped_.owner = this;
  }

  bool SocketReceiveAwaiter::await_suspend(std::coroutine_handle<> handle)
  {
    if (buffer_.empty())
    {
      return false;
    }

    awaiting_ = handle;

    if (!context_.ensure_registered())
    {
      immediate_error_ = make_wsa_error(WSAENOTSOCK);
      return false;
    }

    WSABUF buf;
    buf.buf = buffer_.data();
    buf.len = static_cast<ULONG>(buffer_.size());

    DWORD flags = 0;
    DWORD bytes = 0;

    context_.scheduler().register_reactor_context(&overlapped_,
                                                  [this](const Scheduler::ReactorEvent& event) { on_event(event); });

    const int result = WSARecv(context_.socket(), &buf, 1, &bytes, &flags, &overlapped_, nullptr);
    if (result == SOCKET_ERROR)
    {
      const int error = WSAGetLastError();
      if (error != WSA_IO_PENDING)
      {
        context_.scheduler().unregister_reactor_context(&overlapped_);
        immediate_error_ = make_wsa_error(error);
        immediate_bytes_ = bytes;
        return false;
      }

      suspended_ = true;
      return true;
    }

    context_.scheduler().unregister_reactor_context(&overlapped_);
    context_.set_last_result(bytes, {});
    suspended_ = false;
    immediate_error_.clear();
    immediate_bytes_ = bytes;
    return false;
  }

  std::size_t SocketReceiveAwaiter::await_resume()
  {
    if (!suspended_)
    {
      if (immediate_error_)
      {
        throw std::system_error(immediate_error_);
      }
      return immediate_bytes_;
    }

    if (context_.last_error())
    {
      throw std::system_error(context_.last_error());
    }

    return context_.last_bytes();
  }

  void SocketReceiveAwaiter::on_event(const Scheduler::ReactorEvent& event)
  {
    context_.scheduler().unregister_reactor_context(&overlapped_);
    context_.set_last_result(event.bytes_transferred, event.error);

    suspended_ = false;
    immediate_error_.clear();
    immediate_bytes_ = 0;

    if (awaiting_)
    {
      auto task_handle = Task::handle_type::from_address(awaiting_.address());
      context_.scheduler().wake_task(task_handle);
    }
  }

  SocketSendAwaiter::SocketSendAwaiter(Win32SocketContext& context, std::span<const char> buffer) noexcept
    : context_(context)
    , buffer_(buffer)
  {
    std::memset(&overlapped_, 0, sizeof(overlapped_));
    overlapped_.owner = this;
  }

  bool SocketSendAwaiter::await_suspend(std::coroutine_handle<> handle)
  {
    if (buffer_.empty())
    {
      return false;
    }

    awaiting_ = handle;

    if (!context_.ensure_registered())
    {
      immediate_error_ = make_wsa_error(WSAENOTSOCK);
      return false;
    }

    WSABUF buf;
    buf.buf = const_cast<char*>(buffer_.data());
    buf.len = static_cast<ULONG>(buffer_.size());

    DWORD bytes = 0;

    context_.scheduler().register_reactor_context(&overlapped_,
                                                  [this](const Scheduler::ReactorEvent& event) { on_event(event); });

    const int result = WSASend(context_.socket(), &buf, 1, &bytes, 0, &overlapped_, nullptr);
    if (result == SOCKET_ERROR)
    {
      const int error = WSAGetLastError();
      if (error != WSA_IO_PENDING)
      {
        context_.scheduler().unregister_reactor_context(&overlapped_);
        immediate_error_ = make_wsa_error(error);
        immediate_bytes_ = bytes;
        return false;
      }

      suspended_ = true;
      return true;
    }

    context_.scheduler().unregister_reactor_context(&overlapped_);
    context_.set_last_result(bytes, {});
    suspended_ = false;
    immediate_error_.clear();
    immediate_bytes_ = bytes;
    return false;
  }

  std::size_t SocketSendAwaiter::await_resume()
  {
    if (!suspended_)
    {
      if (immediate_error_)
      {
        throw std::system_error(immediate_error_);
      }
      return immediate_bytes_;
    }

    if (context_.last_error())
    {
      throw std::system_error(context_.last_error());
    }

    return context_.last_bytes();
  }

  void SocketSendAwaiter::on_event(const Scheduler::ReactorEvent& event)
  {
    context_.scheduler().unregister_reactor_context(&overlapped_);
    context_.set_last_result(event.bytes_transferred, event.error);

    suspended_ = false;
    immediate_error_.clear();
    immediate_bytes_ = 0;

    if (awaiting_)
    {
      auto task_handle = Task::handle_type::from_address(awaiting_.address());
      context_.scheduler().wake_task(task_handle);
    }
  }

  SocketReceiveAwaiter async_recv(Win32SocketContext& context, std::span<char> buffer)
  {
    return SocketReceiveAwaiter(context, buffer);
  }

  SocketSendAwaiter async_send(Win32SocketContext& context, std::span<const char> buffer)
  {
    return SocketSendAwaiter(context, buffer);
  }

} // namespace net

#endif // _WIN32
