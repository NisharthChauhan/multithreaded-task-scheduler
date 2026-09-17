#pragma once
#include <cstddef>
#include <string>

// -----------------------------------------------------------------------------
// Task
// -----------------------------------------------------------------------------
// Plain-data model of a schedulable unit of work. A Task carries both its
// *scheduling inputs* (arrival time, burst time, priority) and its
// *scheduling outputs* (start/completion/waiting/turnaround/response times),
// which are filled in by the engine as the task is admitted, dispatched and
// executed. Time units are abstract "ticks"; the engine maps ticks to real
// wall-clock milliseconds via a configurable scale factor so runs are fast
// and observable without pretending to be a real-time simulation.
// -----------------------------------------------------------------------------
struct Task {
    int id = -1;

    // Scheduling inputs (fixed at creation time).
    int arrival_time = 0;      // tick at which the task becomes ready
    int burst_time = 0;        // total CPU ticks required to finish
    int priority = 0;          // smaller value == higher priority (documented in PriorityScheduler.h)

    // Mutable execution state.
    int remaining_time = 0;    // ticks still left to execute (used by Round Robin)

    // Scheduling outputs, in ticks relative to engine start. -1 == "not set yet".
    long long first_start_time = -1;   // tick of the task's first time slice
    long long completion_time = -1;    // tick the task finished entirely

    // Derived metrics, computed once the task completes (see Metrics.h).
    long long waiting_time = 0;        // turnaround_time - burst_time
    long long turnaround_time = 0;     // completion_time - arrival_time
    long long response_time = 0;       // first_start_time - arrival_time

    Task() = default;

    Task(int id_, int arrival_time_, int burst_time_, int priority_)
        : id(id_),
          arrival_time(arrival_time_),
          burst_time(burst_time_),
          priority(priority_),
          remaining_time(burst_time_) {}

    // True once the task has consumed all of its burst time.
    bool isFinished() const { return remaining_time <= 0; }

    // Records the moment the task is first given the CPU. Idempotent:
    // only the first call actually sets first_start_time (needed for
    // Round Robin, where a task may be dispatched multiple times).
    void markStarted(long long tick) {
        if (first_start_time == -1) {
            first_start_time = tick;
        }
    }

    // Records completion and derives the three headline metrics.
    void markCompleted(long long tick) {
        completion_time = tick;
        turnaround_time = completion_time - arrival_time;
        waiting_time = turnaround_time - burst_time;
        response_time = first_start_time - arrival_time;
    }

    std::string toString() const;
};
