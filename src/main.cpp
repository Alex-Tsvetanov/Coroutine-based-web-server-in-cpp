#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "net/win32_acceptor.hpp"
#include "net/win32_socket_context.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

#ifdef _WIN32

namespace
{
  constexpr std::string_view kHttpResponse =
    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: 12\r\n\r\nHello World";

  Task echo_http_connection(Scheduler& scheduler, SOCKET socket)
  {
    net::Win32SocketContext context(scheduler, socket);
    std::array<char, 4096> buffer{};

    try
    {
      std::size_t total = 0;
      while (total < buffer.size())
      {
        auto bytes = co_await net::async_recv(context, std::span<char>(buffer.data() + total, buffer.size() - total));
        if (bytes == 0)
        {
          break;
        }
        total += bytes;
        auto request = std::string_view(buffer.data(), total);
        if (request.find("\r\n\r\n") != std::string_view::npos)
        {
          break;
        }
      }

      co_await net::async_send(context, std::span<const char>(kHttpResponse.data(), kHttpResponse.size()));
    }
    catch (const std::exception& ex)
    {
      std::cerr << "[connection] error: " << ex.what() << std::endl;
    }

    co_return;
  }

  Task accept_loop(Scheduler& scheduler, net::Win32Acceptor::Ptr acceptor)
  {
    std::cout << "[acceptor] listening..." << std::endl;
    while (true)
    {
      try
      {
        SOCKET socket = co_await acceptor->accept();
        auto task = echo_http_connection(scheduler, socket);
        task.set_scheduler(&scheduler);
        scheduler.schedule(std::move(task));
      }
      catch (const std::exception& ex)
      {
        std::cerr << "[acceptor] error: " << ex.what() << std::endl;
      }
    }
  }
} // namespace

int main()
{
  const auto hardware_threads = std::max(2u, std::thread::hardware_concurrency());
  auto scheduler = std::make_shared<Scheduler>(static_cast<std::size_t>(hardware_threads));

  auto reactor = Scheduler::create_default_reactor();
  if (!reactor)
  {
    std::cerr << "Failed to create IO reactor" << std::endl;
    return 1;
  }
  scheduler->set_io_reactor(std::move(reactor));

  auto acceptor = net::Win32Acceptor::create(*scheduler, 8080);
  auto task = accept_loop(*scheduler, acceptor);
  task.set_scheduler(scheduler.get());
  scheduler->schedule(std::move(task));

  scheduler->wait_idle();
  return 0;
}

#else

int main()
{
  std::cerr << "This demo currently supports only Windows." << std::endl;
  return 1;
}

#endif