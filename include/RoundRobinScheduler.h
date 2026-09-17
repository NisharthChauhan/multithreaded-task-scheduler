#pragma once
#include "Scheduler.h"

// Round Robin (preemptive): tasks are popped from the front of the ready
// deque (FIFO) and run for at most `quantum` ticks by the engine. If a
// task still has remaining_time > 0 after its slice, the engine appends
// it to the back of the ready deque so it gets another turn later --
// selectNext() itself only ever pops the front, exactly like FCFS; the
// re-queueing behaviour lives in the engine because it needs to happen
// *after* execution of the slice, not at selection time.
class RoundRobinScheduler : public Scheduler {
public:
    explicit RoundRobinScheduler(int quantum_ticks) : quantum_ticks_(quantum_ticks) {}

    std::string name() const override { return "RoundRobin"; }
    Task selectNext(std::deque<Task>& ready) override;
    bool isPreemptive() const override { return true; }
    int quantum() const override { return quantum_ticks_; }

private:
    int quantum_ticks_;
};
