#include "TaskQueue.h"

void TaskQueue::push(Task task) {
    {
        // RAII lock: guaranteed to unlock even if an exception is thrown
        // between the lock and the end of scope.
        std::lock_guard<std::mutex> lock(mutex_);
        ready_.push_back(std::move(task));
    }
    // Wake exactly one waiting worker -- there is exactly one new task,
    // so waking more would just cause the others to re-check and go
    // back to sleep (harmless, but notify_one is the tighter fit here).
    cv_.notify_one();
}

std::optional<Task> TaskQueue::pop(Scheduler& scheduler) {
    std::unique_lock<std::mutex> lock(mutex_);
    // Block (without spinning) until there is either a task to hand out
    // or the queue has been closed with nothing left in it.
    cv_.wait(lock, [this] { return !ready_.empty() || closed_; });

    if (ready_.empty()) {
        // closed_ must be true here, per the predicate above: no more
        // tasks will ever arrive, so this worker can exit its loop.
        return std::nullopt;
    }

    return scheduler.selectNext(ready_);
}

void TaskQueue::closeProduction() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    // Every worker may currently be blocked in pop(); wake them all so
    // each can re-check the predicate and exit if the queue is empty.
    cv_.notify_all();
}

size_t TaskQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_.size();
}

bool TaskQueue::isClosedAndEmpty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_ && ready_.empty();
}
