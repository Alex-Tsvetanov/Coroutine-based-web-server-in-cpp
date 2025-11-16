#pragma once

#ifdef _WIN32

#include "io_reactor.hpp"
#include "win32_iocp.hpp"

#include <memory>

namespace net
{
  class Win32IocpReactor final: public IoReactor
  {
  public:
    Win32IocpReactor() = default;
    ~Win32IocpReactor() override;

    bool start(EventCallback callback) override;
    void stop() override;
    bool register_handle(std::uintptr_t native_handle, std::uintptr_t completion_key) override;
    bool post_event(const Event& event) override;

  private:
    void forward_completion(const IocpService::Completion& completion);

    EventCallback callback_{};
    IocpService service_{};
  };
} // namespace net

#endif // _WIN32
