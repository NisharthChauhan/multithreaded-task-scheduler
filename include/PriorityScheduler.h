#pragma once
#include "Scheduler.h"

// Priority Scheduling (non-preemptive).
//
// Convention: SMALLER priority value == HIGHER priority (matches common
// OS convention, e.g. Linux "nice" values and Windows thread priorities
// where lower numbers execute first). Priority 0 runs before priority 5.
//
// Among tasks with equal priority, ties are broken by arrival time, then
// by task id, to keep results deterministic and reproducible.
class PriorityScheduler : public Scheduler {
public:
    std::string name() const override { return "Priority"; }
    Task selectNext(std::deque<Task>& ready) override;
};
