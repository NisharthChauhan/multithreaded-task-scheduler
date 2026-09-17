#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t numWorkers) {
    workers_.reserve(numWorkers);
    for (size_t i = 0; i < numWorkers; ++i) {
        // Threads are created exactly once here and reused for every
        // job submitted afterwards -- workerLoop() never returns until
        // shutdown, satisfying the "reuse threads, don't spawn per task"
        // requirement.
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // Sleep (no polling) until there's a job or we're stopping.
            cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });

            if (jobs_.empty()) {
                // Only reachable when stopping_ is true (per predicate),
                // and there is nothing left to drain.
                return;
            }

            job = std::move(jobs_.front());
            jobs_.pop();
        }
        // Run the job *outside* the lock so other workers can keep
        // pulling jobs concurrently while this one executes.
        job();
    }
}

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return; // already shut down; make shutdown() idempotent
        }
        stopping_ = true;
    }
    // Graceful shutdown: stopping_ = true does not clear jobs_, so any
    // already-queued jobs are drained by workerLoop() before it exits.
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}
