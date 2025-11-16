#include "win32_acceptor.hpp"

#ifdef _WIN32

#include <mswsock.h>

#include <cstring>
#include <stdexcept>

namespace net
{
  namespace
  {
    constexpr GUID kAcceptExGuid = WSAID_ACCEPTEX;
    constexpr GUID kGetAcceptExAddrsGuid = WSAID_GETACCEPTEXSOCKADDRS;

    Scheduler::IoHandle make_io_handle(SOCKET socket) noexcept
    {
      return reinterpret_cast<Scheduler::IoHandle>(socket);
    }
  } // namespace

  Win32Acceptor::Ptr Win32Acceptor::create(Scheduler& scheduler, std::uint16_t port)
  {
    SOCKET listen_socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (listen_socket == INVALID_SOCKET)
    {
      throw std::system_error(WSAGetLastError(), std::system_category(), "WSASocketW failed");
    }

    auto acceptor = Ptr(new Win32Acceptor(scheduler, listen_socket, nullptr, nullptr, 0));
    if (!acceptor->load_extension_functions() || !acceptor->configure_socket(port))
    {
      throw std::runtime_error("Failed to initialize listener socket");
    }

    return acceptor;
  }

  Win32Acceptor::Win32Acceptor(Scheduler& scheduler, SOCKET listen_socket, LPFN_ACCEPTEX accept_ex,
                               LPFN_GETACCEPTEXSOCKADDRS get_addrs, DWORD address_length)
    : scheduler_(scheduler)
    , listen_socket_(listen_socket)
    , accept_ex_(accept_ex)
    , get_addrs_(get_addrs)
    , address_length_(address_length)
  {}

  Win32Acceptor::~Win32Acceptor()
  {
    if (listen_socket_ != INVALID_SOCKET)
    {
      closesocket(listen_socket_);
      listen_socket_ = INVALID_SOCKET;
    }
  }

  bool Win32Acceptor::load_extension_functions()
  {
    DWORD bytes = 0;
    const SOCKET sock = listen_socket_;

    if (WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, const_cast<GUID*>(&kAcceptExGuid), sizeof(GUID), &accept_ex_,
                 sizeof(accept_ex_), &bytes, nullptr, nullptr) == SOCKET_ERROR)
    {
      return false;
    }

    if (WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, const_cast<GUID*>(&kGetAcceptExAddrsGuid), sizeof(GUID),
                 &get_addrs_, sizeof(get_addrs_), &bytes, nullptr, nullptr) == SOCKET_ERROR)
    {
      return false;
    }

    address_length_ = sizeof(sockaddr_in) + 16;
    return true;
  }

  bool Win32Acceptor::configure_socket(std::uint16_t port)
  {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
      return false;
    }

    if (listen(listen_socket_, SOMAXCONN) == SOCKET_ERROR)
    {
      return false;
    }

    auto* reactor = scheduler_.io_reactor();
    if (!reactor)
    {
      return false;
    }

    if (!reactor->register_handle(reinterpret_cast<std::uintptr_t>(listen_socket_), make_io_handle(listen_socket_)))
    {
      return false;
    }

    return true;
  }

  Win32Acceptor::AcceptAwaiter Win32Acceptor::accept()
  {
    return AcceptAwaiter(shared_from_this());
  }

  Win32Acceptor::AcceptAwaiter::AcceptAwaiter(Ptr acceptor)
    : acceptor_(std::move(acceptor))
  {
    buffer_.resize(acceptor_->address_length() * 2);
    std::memset(&overlapped_, 0, sizeof(overlapped_));
    overlapped_.owner = this;
  }

  bool Win32Acceptor::AcceptAwaiter::await_suspend(std::coroutine_handle<> handle)
  {
    awaiting_ = handle;

    socket_ = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET)
    {
      immediate_error_ = std::error_code(WSAGetLastError(), std::system_category());
      return false;
    }

    DWORD bytes = 0;
    BOOL result =
      acceptor_->accept_ex()(acceptor_->listen_socket(), socket_, buffer_.data(), 0, acceptor_->address_length(),
                             acceptor_->address_length(), &bytes, &overlapped_);
    if (!result)
    {
      const int error = WSAGetLastError();
      if (error != ERROR_IO_PENDING)
      {
        immediate_error_ = std::error_code(error, std::system_category());
        cleanup_socket();
        return false;
      }

      suspended_ = true;
      acceptor_->scheduler().register_reactor_context(
        &overlapped_, [this](const Scheduler::ReactorEvent& event) { on_event(event); });
      return true;
    }

    completion_error_.clear();
    bytes_ = bytes;
    finalize_socket();
    return false;
  }

  SOCKET Win32Acceptor::AcceptAwaiter::await_resume()
  {
    if (suspended_)
    {
      if (completion_error_)
      {
        cleanup_socket();
        throw std::system_error(completion_error_);
      }
    }
    else if (immediate_error_)
    {
      cleanup_socket();
      throw std::system_error(immediate_error_);
    }

    if (!finalize_socket())
    {
      cleanup_socket();
      throw std::system_error(WSAGetLastError(), std::system_category());
    }

    return socket_;
  }

  void Win32Acceptor::AcceptAwaiter::on_event(const Scheduler::ReactorEvent& event)
  {
    acceptor_->scheduler().unregister_reactor_context(&overlapped_);
    completion_error_ = event.error;
    bytes_ = event.bytes_transferred;
    suspended_ = false;

    if (awaiting_)
    {
      auto handle = Task::handle_type::from_address(awaiting_.address());
      acceptor_->scheduler().wake_task(handle);
    }
  }

  bool Win32Acceptor::AcceptAwaiter::finalize_socket()
  {
    if (socket_ == INVALID_SOCKET)
    {
      return false;
    }

    if (!acceptor_->ensure_registered_child(socket_))
    {
      return false;
    }

    SOCKET listen_socket = acceptor_->listen_socket();
    if (setsockopt(socket_, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<const char*>(&listen_socket),
                   sizeof(SOCKET)) == SOCKET_ERROR)
    {
      return false;
    }

    return true;
  }

  void Win32Acceptor::AcceptAwaiter::cleanup_socket() noexcept
  {
    if (socket_ != INVALID_SOCKET)
    {
      closesocket(socket_);
      socket_ = INVALID_SOCKET;
    }
  }

  bool Win32Acceptor::ensure_registered_child(SOCKET socket)
  {
    auto* reactor = scheduler_.io_reactor();
    if (!reactor)
    {
      return false;
    }

    return reactor->register_handle(reinterpret_cast<std::uintptr_t>(socket), make_io_handle(socket));
  }

} // namespace net

#endif // _WIN32
