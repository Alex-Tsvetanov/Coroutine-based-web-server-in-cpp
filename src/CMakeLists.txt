# Scheduler demo executable demonstrating coroutine scheduler usage
add_executable(scheduler_demo
    main.cpp
    scheduler/thread_pool.hpp
    scheduler/task.hpp
    scheduler/scheduler.hpp
    scheduler/scheduler.cpp
)

target_compile_features(scheduler_demo PRIVATE cxx_std_20)
