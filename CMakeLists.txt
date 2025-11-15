cmake_minimum_required(VERSION 3.20)
project(CoroutineWebServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for macOS with C++20 coroutines
if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -Wall -Wextra")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# Multiple-coroutine server (3 coroutines per connection)
add_executable(server
    multiple-coroutine/main.cpp
    multiple-coroutine/connection.cpp
    multiple-coroutine/http_parser.cpp
    multiple-coroutine/kqueue_backend.cpp
    multiple-coroutine/scheduler.cpp
    multiple-coroutine/server.cpp
)

target_include_directories(server PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/multiple-coroutine
)

target_link_libraries(server pthread)

# Single-coroutine server (original implementation)
add_executable(single-coroutine-server
    single-coroutine/main.cpp
    single-coroutine/scheduler.cpp
    single-coroutine/kqueue_backend.cpp
    single-coroutine/connection.cpp
    single-coroutine/http_parser.cpp
    single-coroutine/server.cpp
)

target_include_directories(single-coroutine-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
