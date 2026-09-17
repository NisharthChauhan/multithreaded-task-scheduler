#pragma once
#include "Scheduler.h"

// Shortest Job First (non-preemptive): among all currently ready tasks,
// picks the one with the smallest remaining burst time. Ties are broken
// by arrival time, then by task id, to keep results deterministic.
class SJFScheduler : public Scheduler {
public:
    std::string name() const override { return "SJF"; }
    Task selectNext(std::deque<Task>& ready) override;
};
