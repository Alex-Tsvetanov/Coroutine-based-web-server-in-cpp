# Scheduler demo executable demonstrating coroutine scheduler usage
add_executable(scheduler_demo
    main.cpp
    scheduler/thread_pool.hpp
    scheduler/task.hpp
    scheduler/scheduler.hpp
    scheduler/scheduler.cpp
    net/win32_iocp.hpp
    net/win32_iocp.cpp
    net/win32_iocp_reactor.hpp
    net/win32_iocp_reactor.cpp
    net/win32_socket_context.hpp
    net/win32_socket_context.cpp
    net/win32_acceptor.hpp
    net/win32_acceptor.cpp
)

target_compile_features(scheduler_demo PRIVATE cxx_std_20)

if(WIN32)
    target_link_libraries(scheduler_demo PRIVATE ws2_32)
endif()
