#pragma once

#include <coroutine>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

constexpr size_t BUFFER_SIZE = 16384;
constexpr int MAX_EVENTS = 256;
constexpr int BACKLOG = 128;

// Forward declarations
class KqueueBackend;
class Scheduler;

// Global pointers
inline KqueueBackend* g_kqueue = nullptr;
inline Scheduler* g_scheduler = nullptr;
