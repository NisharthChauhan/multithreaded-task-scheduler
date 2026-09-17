#include "FCFSScheduler.h"

Task FCFSScheduler::selectNext(std::deque<Task>& ready) {
    // The admission producer pushes tasks in non-decreasing arrival-time
    // order, so the front of the deque is always the earliest arrival
    // still waiting -- exactly FCFS semantics. O(1).
    Task next = ready.front();
    ready.pop_front();
    return next;
}
