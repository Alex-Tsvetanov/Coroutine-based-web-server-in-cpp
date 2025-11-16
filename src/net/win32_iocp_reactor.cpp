#include "win32_iocp_reactor.hpp"

#ifdef _WIN32

#include <system_error>

namespace net
{

  Win32IocpReactor::~Win32IocpReactor()
  {
    stop();
  }

  bool Win32IocpReactor::start(EventCallback callback)
  {
    callback_ = std::move(callback);
    if (!callback_)
    {
      return false;
    }

    return service_.initialize([this](const IocpService::Completion& completion) { forward_completion(completion); });
  }

  void Win32IocpReactor::stop()
  {
    service_.shutdown();
    callback_ = nullptr;
  }

  bool Win32IocpReactor::register_handle(std::uintptr_t native_handle, std::uintptr_t completion_key)
  {
    return service_.associate_handle(reinterpret_cast<HANDLE>(native_handle), completion_key);
  }

  bool Win32IocpReactor::post_event(const Event& event)
  {
    OVERLAPPED overlapped{};
    return service_.post_status(event.key, static_cast<DWORD>(event.bytes_transferred), &overlapped);
  }

  void Win32IocpReactor::forward_completion(const IocpService::Completion& completion)
  {
    if (!callback_)
    {
      return;
    }

    Event event;
    event.bytes_transferred = completion.bytes_transferred;
    event.error = completion.error;
    event.context = completion.overlapped;
    event.key = completion.completion_key;

    callback_(event);
  }

} // namespace net

#endif // _WIN32
