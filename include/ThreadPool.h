#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// -----------------------------------------------------------------------------
// ThreadPool
// -----------------------------------------------------------------------------
// A general-purpose, reusable pool of worker threads that execute
// arbitrary std::function<void()> jobs pulled from a shared job queue.
// This class is intentionally independent of the Task/Scheduler machinery
// -- it knows nothing about scheduling policies -- so it can (and, in the
// tests, is) exercised on its own.
//
// The scheduling engine (see main.cpp / SchedulingEngine) uses it by
// submitting exactly `numWorkers` long-running jobs, one per worker
// thread, each of which repeatedly pops a Task from the policy-driven
// TaskQueue and executes it. That keeps "how threads are managed"
// (this class) cleanly separated from "which task runs next"
// (Scheduler + TaskQueue).
//
// Synchronization: a mutex protects the job queue; a condition_variable
// lets idle workers block instead of busy-polling; shutdown is graceful
// -- stop() lets already-queued jobs finish before joining every thread.
// -----------------------------------------------------------------------------
class ThreadPool {
public:
    explicit ThreadPool(size_t numWorkers);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submits a job and returns a future for its result. Throws
    // std::runtime_error if called after shutdown() has been invoked.
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<ReturnType> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                throw std::runtime_error("ThreadPool::submit called after shutdown()");
            }
            jobs_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();
        return result;
    }

    // Number of worker threads managed by this pool.
    size_t workerCount() const { return workers_.size(); }

    // Gracefully shuts the pool down: lets already-queued jobs drain,
    // then joins every worker thread. Safe to call multiple times, and
    // called automatically from the destructor if the user forgets.
    void shutdown();

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> jobs_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};
