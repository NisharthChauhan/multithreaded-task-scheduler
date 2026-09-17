#include "SchedulingEngine.h"
#include <algorithm>
#include <thread>

SchedulingEngine::SchedulingEngine(std::vector<Task> tasks, std::shared_ptr<Scheduler> scheduler,
                                    int numWorkers, int tickMillis)
    : pending_(std::move(tasks)),
      scheduler_(std::move(scheduler)),
      numWorkers_(std::max(1, numWorkers)),
      tickMillis_(std::max(1, tickMillis)) {
    // Sort by arrival time so the admission producer can walk the list
    // once, in order. std::stable_sort keeps insertion order for tasks
    // that share an arrival time, which matters for FCFS determinism.
    std::stable_sort(pending_.begin(), pending_.end(),
                      [](const Task& a, const Task& b) { return a.arrival_time < b.arrival_time; });
}

long long SchedulingEngine::ticksSinceStart() const {
    auto elapsed = std::chrono::steady_clock::now() - startTime_;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    return ms / tickMillis_;
}

void SchedulingEngine::admissionLoop() {
    // Producer side of the producer-consumer relationship: releases
    // each task into the shared ready queue once its arrival tick has
    // (simulated-)elapsed, then notifies consumers via TaskQueue::push.
    for (const Task& t : pending_) {
        auto targetTime = startTime_ + std::chrono::milliseconds(
                                            static_cast<long long>(t.arrival_time) * tickMillis_);
        std::this_thread::sleep_until(targetTime);
        readyQueue_.push(t);
    }
    // No more tasks will ever be pushed; wake every blocked worker so
    // they can notice the queue is closed and exit once it drains.
    readyQueue_.closeProduction();
}

void SchedulingEngine::workerLoop() {
    for (;;) {
        // Blocks (no polling) until a task is ready or production is
        // closed and the queue has drained.
        auto maybeTask = readyQueue_.pop(*scheduler_);
        if (!maybeTask) break;

        Task task = *maybeTask;
        task.markStarted(ticksSinceStart());

        const int ticksToRun = scheduler_->isPreemptive()
                                    ? std::min(task.remaining_time, scheduler_->quantum())
                                    : task.remaining_time;

        // Simulate doing `ticksToRun` ticks of work. This is the only
        // place "execution" happens; everything else is bookkeeping.
        std::this_thread::sleep_for(std::chrono::milliseconds(
            static_cast<long long>(ticksToRun) * tickMillis_));

        task.remaining_time -= ticksToRun;

        if (task.remaining_time > 0) {
            // Round Robin: slice used up but work remains -- go to the
            // back of the ready queue for another turn later.
            readyQueue_.requeue(task);
        } else {
            task.markCompleted(ticksSinceStart());
            std::lock_guard<std::mutex> lock(resultMutex_);
            completed_.push_back(task);
        }
    }
}

std::vector<Task> SchedulingEngine::run() {
    startTime_ = std::chrono::steady_clock::now();

    if (pending_.empty()) {
        // Nothing to do -- still a valid run (see "empty task queue" test).
        totalTicks_ = 0;
        return {};
    }

    std::thread admission(&SchedulingEngine::admissionLoop, this);

    {
        ThreadPool pool(static_cast<size_t>(numWorkers_));
        std::vector<std::future<void>> futures;
        futures.reserve(numWorkers_);
        for (int i = 0; i < numWorkers_; ++i) {
            futures.push_back(pool.submit([this] { workerLoop(); }));
        }
        // Wait for every consumer loop to notice the queue is closed
        // and empty, then let `pool` go out of scope, which joins the
        // (already-finished) worker threads -- a graceful shutdown.
        for (auto& f : futures) f.get();
    }

    admission.join();
    totalTicks_ = ticksSinceStart();
    return completed_;
}
