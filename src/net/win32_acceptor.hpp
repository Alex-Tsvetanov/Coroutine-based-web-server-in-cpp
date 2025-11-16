#pragma once

#ifdef _WIN32

#include "../scheduler/scheduler.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include <coroutine>
#include <memory>
#include <system_error>
#include <vector>

namespace net
{
  class Win32Acceptor: public std::enable_shared_from_this<Win32Acceptor>
  {
  public:
    using Ptr = std::shared_ptr<Win32Acceptor>;

    static Ptr create(Scheduler& scheduler, std::uint16_t port);

    ~Win32Acceptor();

    SOCKET listen_socket() const noexcept { return listen_socket_; }
    Scheduler& scheduler() const noexcept { return scheduler_; }

    class AcceptAwaiter
    {
    public:
      explicit AcceptAwaiter(Ptr acceptor);

      bool await_ready() const noexcept { return false; }
      bool await_suspend(std::coroutine_handle<> handle);
      SOCKET await_resume();

    private:
      void on_event(const Scheduler::ReactorEvent& event);
      void cleanup_socket() noexcept;
      bool finalize_socket();

      Ptr acceptor_;
      SOCKET socket_{INVALID_SOCKET};
      std::vector<char> buffer_;
      struct OverlappedWrapper: OVERLAPPED
      {
        AcceptAwaiter* owner{nullptr};
      } overlapped_{};
      std::coroutine_handle<> awaiting_{};
      std::error_code immediate_error_{};
      std::error_code completion_error_{};
      DWORD bytes_{0};
      bool suspended_{false};
    };

    AcceptAwaiter accept();

  private:
    Win32Acceptor(Scheduler& scheduler, SOCKET listen_socket, LPFN_ACCEPTEX accept_ex,
                  LPFN_GETACCEPTEXSOCKADDRS get_addrs, DWORD address_length);

    bool load_extension_functions();
    bool configure_socket(std::uint16_t port);
    bool ensure_registered_child(SOCKET socket);
    DWORD address_length() const noexcept { return address_length_; }
    LPFN_ACCEPTEX accept_ex() const noexcept { return accept_ex_; }

    Scheduler& scheduler_;
    SOCKET listen_socket_{INVALID_SOCKET};
    LPFN_ACCEPTEX accept_ex_{nullptr};
    LPFN_GETACCEPTEXSOCKADDRS get_addrs_{nullptr};
    DWORD address_length_{0};
  };

} // namespace net

#endif // _WIN32
