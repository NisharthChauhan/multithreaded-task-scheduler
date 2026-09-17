cmake_minimum_required(VERSION 3.14)
project(multithreaded_task_scheduler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

set(ENGINE_SOURCES
    src/Task.cpp
    src/FCFSScheduler.cpp
    src/SJFScheduler.cpp
    src/PriorityScheduler.cpp
    src/RoundRobinScheduler.cpp
    src/TaskQueue.cpp
    src/ThreadPool.cpp
    src/SchedulingEngine.cpp
    src/WorkloadGenerator.cpp
    src/Metrics.cpp
)

# Static library so both the CLI app and the test binary can link the
# engine without recompiling it twice.
add_library(scheduler_engine STATIC ${ENGINE_SOURCES})
target_include_directories(scheduler_engine PUBLIC include)
target_link_libraries(scheduler_engine PUBLIC Threads::Threads)

add_executable(scheduler_app src/main.cpp)
target_link_libraries(scheduler_app PRIVATE scheduler_engine)

# --- Tests -------------------------------------------------------------
enable_testing()
add_executable(scheduler_tests tests/test_main.cpp)
target_link_libraries(scheduler_tests PRIVATE scheduler_engine)
add_test(NAME scheduler_tests COMMAND scheduler_tests)
