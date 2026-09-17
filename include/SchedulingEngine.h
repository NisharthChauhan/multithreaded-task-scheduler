#pragma once
#include <chrono>
#include <memory>
#include <vector>
#include "Scheduler.h"
#include "Task.h"
#include "TaskQueue.h"
#include "ThreadPool.h"

// -----------------------------------------------------------------------------
// SchedulingEngine
// -----------------------------------------------------------------------------
// Wires the pieces together for one run:
//
//   1. An "admission" thread (the producer) sleeps until each task's
//      arrival time and then pushes it into the shared TaskQueue.
//   2. `numWorkers` consumer loops -- each submitted as one job to the
//      ThreadPool -- repeatedly pop the next task from TaskQueue
//      (as chosen by the active Scheduler) and execute it.
//   3. Execution is simulated: a worker "runs" a task by sleeping for
//      (ticks_to_run * tickMillis), which stands in for real CPU work
//      while keeping wall-clock runtime short and deterministic-ish.
//   4. Non-preemptive tasks run to completion in one go; Round Robin
//      tasks run for min(remaining, quantum) ticks and are re-queued if
//      not yet finished.
//
// This is explicitly a *user-space, multi-worker* scheduling model, not
// a claim to emulate a real kernel CPU scheduler.
// -----------------------------------------------------------------------------
class SchedulingEngine {
public:
    // tickMillis controls the simulated-time-to-wall-clock-time scale:
    // each tick of burst/quantum time is slept for tickMillis milliseconds.
    SchedulingEngine(std::vector<Task> tasks,
                      std::shared_ptr<Scheduler> scheduler,
                      int numWorkers,
                      int tickMillis);

    // Runs the full simulation to completion (blocks until every task is
    // done) and returns the finished tasks (order is completion order,
    // not input order).
    std::vector<Task> run();

    // Total wall-clock-equivalent ticks the whole run took (for
    // "Total Execution Time" in the summary).
    long long totalTicks() const { return totalTicks_; }

private:
    void admissionLoop();
    void workerLoop();

    // Elapsed wall-clock time since the run started, converted to ticks
    // via tickMillis_. This is how workers/admission agree on "now"
    // without a shared mutable clock variable -- steady_clock is itself
    // thread-safe to read concurrently.
    long long ticksSinceStart() const;

    std::vector<Task> pending_;              // not-yet-arrived tasks, sorted by arrival_time
    std::shared_ptr<Scheduler> scheduler_;
    TaskQueue readyQueue_;
    int numWorkers_;
    int tickMillis_;
    std::chrono::steady_clock::time_point startTime_;

    std::mutex resultMutex_;
    std::vector<Task> completed_;
    long long totalTicks_ = 0;
};
