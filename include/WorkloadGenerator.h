#pragma once
#include <cstdint>
#include <vector>
#include "Task.h"

// -----------------------------------------------------------------------------
// WorkloadGenerator
// -----------------------------------------------------------------------------
// The "task producer" required by the spec. It can either fabricate a
// randomized workload (with a fixed seed for reproducibility) or hand
// back one of a few small predefined workloads useful for manual testing
// and demos.
// -----------------------------------------------------------------------------
class WorkloadGenerator {
public:
    // Generates `numTasks` tasks with:
    //   arrival times  in [0, maxArrival]
    //   burst times    in [minBurst, maxBurst]
    //   priorities     in [minPriority, maxPriority] (smaller = higher priority)
    // Using a fixed seed makes the workload fully reproducible across runs.
    static std::vector<Task> generate(int numTasks,
                                       uint32_t seed = 42,
                                       int maxArrival = 20,
                                       int minBurst = 1,
                                       int maxBurst = 10,
                                       int minPriority = 0,
                                       int maxPriority = 9);

    // A handful of small, hand-written workloads for deterministic
    // testing / demos (indices 0..N-1). Throws std::out_of_range if the
    // index doesn't exist.
    static std::vector<Task> predefined(int index);

    // Number of predefined workloads available.
    static int predefinedCount();
};
