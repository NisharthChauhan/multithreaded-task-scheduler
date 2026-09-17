#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include "Scheduler.h"
#include "Task.h"

// -----------------------------------------------------------------------------
// TaskQueue
// -----------------------------------------------------------------------------
// The single shared "ready" data structure in the engine. It is a
// producer-consumer queue:
//
//   Producer -> the admission thread, which pushes tasks once their
//               (simulated) arrival time has passed.
//   Consumers -> the worker threads, which pop the next task to run
//                according to whatever Scheduler policy is active.
//
// Ordering among ready tasks is delegated to a Scheduler (FCFS/SJF/
// Priority/RoundRobin) so TaskQueue itself stays policy-agnostic; it only
// owns the mutex/condition_variable and the underlying std::deque.
//
// Workers never busy-wait: pop() blocks on the condition_variable until
// either a task is available or the queue is closed and drained.
// -----------------------------------------------------------------------------
class TaskQueue {
public:
    // Adds a task to the back of the ready deque and wakes one waiting worker.
    void push(Task task);

    // Requeues a partially-executed task (used by Round Robin after a
    // quantum slice). Semantically identical to push(), kept as a
    // separate name purely for readability at call sites.
    void requeue(Task task) { push(std::move(task)); }

    // Blocks until a task is available or the queue has been closed and
    // is empty, in which case it returns std::nullopt so the worker can
    // exit its loop. `scheduler` decides *which* ready task is returned.
    std::optional<Task> pop(Scheduler& scheduler);

    // Marks the queue closed: no more tasks will ever be pushed (all
    // admissions are done). Wakes every worker so they can notice the
    // queue is both closed and empty, and exit.
    void closeProduction();

    // Number of tasks currently ready (for progress reporting / tests).
    size_t size() const;

    bool isClosedAndEmpty() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> ready_;
    bool closed_ = false;
};
