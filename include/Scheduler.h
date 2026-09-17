#pragma once
#include <deque>
#include <string>
#include "Task.h"

// -----------------------------------------------------------------------------
// Scheduler
// -----------------------------------------------------------------------------
// Common interface implemented by every scheduling policy (FCFS, SJF,
// Priority, Round Robin). The engine owns a single "ready deque" of tasks
// that have arrived but not yet finished, protected by TaskQueue's mutex.
// selectNext() is called with that lock already held, so implementations
// must be fast, non-blocking and must not perform I/O.
//
// selectNext() removes and returns exactly one task from `ready`. It is
// the *only* extension point a new policy needs to implement -- everything
// else (thread pool, synchronization, metrics) is policy-agnostic.
// -----------------------------------------------------------------------------
class Scheduler {
public:
    virtual ~Scheduler() = default;

    // Human readable name, used in CLI menus and CSV output.
    virtual std::string name() const = 0;

    // Removes and returns the next task to run from the ready deque.
    // Precondition: !ready.empty() and the ready-deque mutex is held.
    virtual Task selectNext(std::deque<Task>& ready) = 0;

    // Preemptive policies (currently only Round Robin) are dispatched by
    // the engine in quantum-sized slices and re-queued if unfinished.
    // Non-preemptive policies run a task to completion once selected.
    virtual bool isPreemptive() const { return false; }

    // Only meaningful for preemptive schedulers.
    virtual int quantum() const { return 0; }
};
