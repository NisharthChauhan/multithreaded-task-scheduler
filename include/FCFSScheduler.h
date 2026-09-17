#pragma once
#include "Scheduler.h"

// First Come First Serve: tasks are executed in the order they were
// admitted into the ready deque, which -- because the admission producer
// pushes tasks in non-decreasing arrival-time order -- is equivalent to
// arrival order. Non-preemptive: whichever task is popped runs to completion.
class FCFSScheduler : public Scheduler {
public:
    std::string name() const override { return "FCFS"; }
    Task selectNext(std::deque<Task>& ready) override;
};
