#include "kqueue_backend.h"
#include "scheduler.h"

void register_io_event(int fd, bool is_write, std::coroutine_handle<> h) {
    if (is_write) {
        KqueueBackend::instance().register_write(fd, h);
    } else {
        KqueueBackend::instance().register_read(fd, h);
    }
}

void schedule_coroutine(std::coroutine_handle<> h) {
    Scheduler::instance().schedule(h);
}
