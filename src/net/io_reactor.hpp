#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <system_error>

namespace net
{
  class IoReactor
  {
  public:
    struct Event
    {
      std::size_t bytes_transferred{0};
      std::error_code error{};
      void* context{nullptr};
      std::uintptr_t key{0};
    };

    using EventCallback = std::function<void(const Event&)>;

    virtual ~IoReactor() = default;

    virtual bool start(EventCallback callback) = 0;
    virtual void stop() = 0;
    virtual bool register_handle(std::uintptr_t native_handle, std::uintptr_t completion_key) = 0;
    virtual bool post_event(const Event& event) = 0;
  };
} // namespace net
