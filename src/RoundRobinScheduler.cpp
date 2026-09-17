#include "RoundRobinScheduler.h"

Task RoundRobinScheduler::selectNext(std::deque<Task>& ready) {
    // Plain FIFO pop, O(1). The quantum-slicing and re-queueing of
    // partially-executed tasks is handled by SchedulingEngine, which
    // knows how to run a slice and push the task back onto the same
    // ready deque via TaskQueue::requeue() -- selectNext() only ever
    // needs to hand back whichever task is currently at the front.
    Task next = ready.front();
    ready.pop_front();
    return next;
}
