#include "PriorityScheduler.h"
#include <algorithm>

Task PriorityScheduler::selectNext(std::deque<Task>& ready) {
    // Smaller priority value == higher priority (see header comment).
    // O(n) scan for the same reasons documented in SJFScheduler.cpp.
    auto best = std::min_element(ready.begin(), ready.end(), [](const Task& a, const Task& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.arrival_time != b.arrival_time) return a.arrival_time < b.arrival_time;
        return a.id < b.id;
    });
    Task next = *best;
    ready.erase(best);
    return next;
}
