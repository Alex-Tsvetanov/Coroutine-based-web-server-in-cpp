#include "scheduler.h"
#include "awaitable.h"
#include "channel.h"

void YieldAwaiter::await_suspend(std::coroutine_handle<> h) {
    Scheduler::instance().schedule(h);
}

void IOAwaiter::await_suspend(std::coroutine_handle<> h) {
    // This will be registered with kqueue backend
    extern void register_io_event(int fd, bool is_write, std::coroutine_handle<> h);
    register_io_event(fd, is_write, h);
}

template<typename T>
void Channel<T>::schedule_coroutine(std::coroutine_handle<> h) {
    Scheduler::instance().schedule(h);
}

// Explicit instantiations
#include "common.h"
template class Channel<BufferPtr>;
