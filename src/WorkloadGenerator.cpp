#include "WorkloadGenerator.h"
#include <random>
#include <stdexcept>

std::vector<Task> WorkloadGenerator::generate(int numTasks, uint32_t seed, int maxArrival,
                                               int minBurst, int maxBurst, int minPriority,
                                               int maxPriority) {
    // A fixed seed (default 42) makes every generated workload
    // reproducible: the same seed + same parameters always yields the
    // same tasks, which is what lets benchmark experiments be repeated
    // and compared fairly across algorithms.
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> arrivalDist(0, maxArrival);
    std::uniform_int_distribution<int> burstDist(minBurst, maxBurst);
    std::uniform_int_distribution<int> priorityDist(minPriority, maxPriority);

    std::vector<Task> tasks;
    tasks.reserve(numTasks);
    for (int i = 0; i < numTasks; ++i) {
        tasks.emplace_back(i + 1, arrivalDist(rng), burstDist(rng), priorityDist(rng));
    }
    return tasks;
}

std::vector<Task> WorkloadGenerator::predefined(int index) {
    // id, arrival, burst, priority
    switch (index) {
        case 0: // classic FCFS/SJF textbook example
            return {
                Task(1, 0, 5, 2),
                Task(2, 1, 3, 1),
                Task(3, 2, 8, 4),
                Task(4, 3, 6, 3),
            };
        case 1: // identical arrival times
            return {
                Task(1, 0, 4, 1),
                Task(2, 0, 2, 2),
                Task(3, 0, 6, 3),
                Task(4, 0, 1, 1),
            };
        case 2: // identical burst times, varying priority (good for RR demo)
            return {
                Task(1, 0, 4, 3),
                Task(2, 1, 4, 1),
                Task(3, 2, 4, 2),
                Task(4, 3, 4, 4),
            };
        case 3: // single task
            return {Task(1, 0, 5, 1)};
        case 4: // empty workload
            return {};
        default:
            throw std::out_of_range("WorkloadGenerator::predefined: no workload at index " +
                                     std::to_string(index));
    }
}

int WorkloadGenerator::predefinedCount() { return 5; }
