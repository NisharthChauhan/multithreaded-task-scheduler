#include "SJFScheduler.h"
#include <algorithm>

Task SJFScheduler::selectNext(std::deque<Task>& ready) {
    // Linear scan for the task with the smallest remaining burst time.
    // The ready deque only ever holds tasks that are simultaneously
    // "available", so this is a correct (if O(n)) SJF selection; a
    // std::priority_queue would be O(log n) but ready tasks also need
    // to support removal-by-arrival for the admission side, so a plain
    // deque + linear scan keeps the container shared and simple. For
    // realistic workload sizes (thousands of ready tasks) this is fine;
    // documented here for interview discussion.
    auto best = std::min_element(ready.begin(), ready.end(), [](const Task& a, const Task& b) {
        if (a.remaining_time != b.remaining_time) return a.remaining_time < b.remaining_time;
        if (a.arrival_time != b.arrival_time) return a.arrival_time < b.arrival_time;
        return a.id < b.id;
    });
    Task next = *best;
    ready.erase(best);
    return next;
}
