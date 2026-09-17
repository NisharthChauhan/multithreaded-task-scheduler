// -----------------------------------------------------------------------------
// Multithreaded Task Scheduling Engine -- CLI entry point
//
// This is a *user-space* task scheduling engine built for learning/demo
// purposes. It models classical OS scheduling policies (FCFS, SJF,
// Priority, Round Robin) on top of a real multithreaded worker pool with
// a shared, mutex+condition_variable-protected ready queue. It does not
// claim to be, and is not, a kernel-level CPU scheduler.
// -----------------------------------------------------------------------------
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "FCFSScheduler.h"
#include "Metrics.h"
#include "PriorityScheduler.h"
#include "RoundRobinScheduler.h"
#include "SJFScheduler.h"
#include "SchedulingEngine.h"
#include "WorkloadGenerator.h"

namespace {

constexpr int kDefaultTickMillis = 20; // 1 simulated tick == 20ms wall clock

enum class Algorithm { FCFS, SJF, Priority, RoundRobin };

std::string algorithmName(Algorithm a) {
    switch (a) {
        case Algorithm::FCFS: return "FCFS";
        case Algorithm::SJF: return "SJF";
        case Algorithm::Priority: return "Priority";
        case Algorithm::RoundRobin: return "RoundRobin";
    }
    return "?";
}

std::shared_ptr<Scheduler> makeScheduler(Algorithm a, int quantum) {
    switch (a) {
        case Algorithm::FCFS: return std::make_shared<FCFSScheduler>();
        case Algorithm::SJF: return std::make_shared<SJFScheduler>();
        case Algorithm::Priority: return std::make_shared<PriorityScheduler>();
        case Algorithm::RoundRobin: return std::make_shared<RoundRobinScheduler>(quantum);
    }
    return nullptr;
}

int readInt(const std::string& prompt, int lo, int hi) {
    while (true) {
        std::cout << prompt;
        int value;
        if (std::cin >> value && value >= lo && value <= hi) {
            return value;
        }
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "  Please enter a number between " << lo << " and " << hi << ".\n";
    }
}

// -----------------------------------------------------------------------------
// Application: holds CLI-configurable state and dispatches menu actions.
// -----------------------------------------------------------------------------
struct Application {
    std::vector<Task> workload;
    Algorithm algorithm = Algorithm::FCFS;
    int numWorkers = 4;
    int quantum = 2;
    std::vector<Task> lastResults;
    MetricsSummary lastSummary;
    bool hasResults = false;

    void generateWorkload() {
        int n = readInt("  Number of tasks to generate: ", 0, 100000);
        int seed = readInt("  Random seed (0 for default 42): ", 0, 1000000);
        workload = WorkloadGenerator::generate(n, seed == 0 ? 42u : static_cast<uint32_t>(seed));
        std::cout << "  Generated " << workload.size() << " tasks (seed="
                  << (seed == 0 ? 42 : seed) << ").\n";
    }

    void loadPredefinedWorkload() {
        std::cout << "  Predefined workloads:\n";
        std::cout << "    0. Textbook example (4 tasks, staggered arrivals)\n";
        std::cout << "    1. Identical arrival times (4 tasks)\n";
        std::cout << "    2. Identical burst times (4 tasks, good for Round Robin)\n";
        std::cout << "    3. Single task\n";
        std::cout << "    4. Empty workload\n";
        int idx = readInt("  Choose 0-4: ", 0, WorkloadGenerator::predefinedCount() - 1);
        workload = WorkloadGenerator::predefined(idx);
        std::cout << "  Loaded workload " << idx << " (" << workload.size() << " tasks).\n";
    }

    void selectAlgorithm() {
        std::cout << "  1. FCFS\n  2. SJF\n  3. Priority\n  4. Round Robin\n";
        int choice = readInt("  Choose 1-4: ", 1, 4);
        algorithm = static_cast<Algorithm>(choice - 1);
        std::cout << "  Algorithm set to " << algorithmName(algorithm) << ".\n";
    }

    void configureWorkers() {
        numWorkers = readInt("  Number of worker threads (1-64): ", 1, 64);
    }

    void configureQuantum() {
        quantum = readInt("  Round Robin quantum (ticks, 1-1000): ", 1, 1000);
    }

    void runScheduler() {
        if (workload.empty()) {
            std::cout << "  Warning: workload is empty -- running anyway (0 tasks).\n";
        }
        auto scheduler = makeScheduler(algorithm, quantum);
        std::cout << "  Running " << algorithmName(algorithm) << " with " << numWorkers
                  << " worker(s)"
                  << (algorithm == Algorithm::RoundRobin ? ", quantum=" + std::to_string(quantum) : "")
                  << " on " << workload.size() << " task(s)...\n";

        SchedulingEngine engine(workload, scheduler, numWorkers, kDefaultTickMillis);
        lastResults = engine.run();
        lastSummary = Metrics::summarize(lastResults, algorithmName(algorithm), numWorkers,
                                          algorithm == Algorithm::RoundRobin ? quantum : 0);
        hasResults = true;
        std::cout << "  Done. " << lastResults.size() << " task(s) completed.\n";
    }

    void displayResults() const {
        if (!hasResults) {
            std::cout << "  No results yet -- run the scheduler first (option 6).\n";
            return;
        }
        // Show tasks sorted by id for readability, regardless of completion order.
        std::vector<Task> sorted = lastResults;
        std::sort(sorted.begin(), sorted.end(), [](const Task& a, const Task& b) { return a.id < b.id; });
        Metrics::printTaskTable(sorted);
    }

    void displayMetrics() const {
        if (!hasResults) {
            std::cout << "  No results yet -- run the scheduler first (option 6).\n";
            return;
        }
        Metrics::printSummary(lastSummary);
    }

    void runBenchmark() const {
        std::cout << "  Running benchmark suite (this simulates real time, please wait)...\n";
        const std::vector<int> taskCounts = {10, 50, 100};
        const std::vector<int> workerCounts = {1, 2, 4, 8};
        const std::vector<int> quanta = {1, 2, 5, 10};
        const std::string csvPath = "results/benchmark.csv";

        for (int n : taskCounts) {
            auto tasks = WorkloadGenerator::generate(n, 42);
            for (int w : workerCounts) {
                for (Algorithm algo : {Algorithm::FCFS, Algorithm::SJF, Algorithm::Priority,
                                        Algorithm::RoundRobin}) {
                    if (algo == Algorithm::RoundRobin) {
                        for (int q : quanta) {
                            auto sched = makeScheduler(algo, q);
                            SchedulingEngine engine(tasks, sched, w, 2 /* fast tick for benchmarking */);
                            auto results = engine.run();
                            auto summary = Metrics::summarize(results, algorithmName(algo), w, q);
                            Metrics::appendCsv(csvPath, summary);
                        }
                    } else {
                        auto sched = makeScheduler(algo, 0);
                        SchedulingEngine engine(tasks, sched, w, 2);
                        auto results = engine.run();
                        auto summary = Metrics::summarize(results, algorithmName(algo), w, 0);
                        Metrics::appendCsv(csvPath, summary);
                    }
                }
            }
            std::cout << "    Completed configurations for " << n << " tasks.\n";
        }
        std::cout << "  Benchmark complete. Results appended to " << csvPath << "\n";
    }

    void exportResults() const {
        if (!hasResults) {
            std::cout << "  No results yet -- run the scheduler first (option 6).\n";
            return;
        }
        const std::string path = "results/last_run.csv";
        Metrics::appendCsv(path, lastSummary);
        std::cout << "  Exported summary to " << path << "\n";
    }
};

void printMenu() {
    std::cout << "\n=== Multithreaded Task Scheduling Engine ===\n"
               << " 1. Generate workload\n"
               << " 2. Load predefined workload\n"
               << " 3. Select scheduling algorithm\n"
               << " 4. Configure worker threads\n"
               << " 5. Configure Round Robin quantum\n"
               << " 6. Run scheduler\n"
               << " 7. Display task execution results\n"
               << " 8. Display performance metrics\n"
               << " 9. Run benchmark\n"
               << "10. Export results\n"
               << "11. Exit\n";
}

} // namespace

int main() {
    std::cout << "Multithreaded Task Scheduling Engine (user-space, C++17)\n";
    std::cout << "Note: this models OS scheduling concepts; it is not a kernel scheduler.\n";

    Application app;
    app.workload = WorkloadGenerator::predefined(0); // sensible default so option 6 works immediately

    bool running = true;
    while (running) {
        printMenu();
        int choice = readInt("Choose an option (1-11): ", 1, 11);
        std::cout << "\n";
        switch (choice) {
            case 1: app.generateWorkload(); break;
            case 2: app.loadPredefinedWorkload(); break;
            case 3: app.selectAlgorithm(); break;
            case 4: app.configureWorkers(); break;
            case 5: app.configureQuantum(); break;
            case 6: app.runScheduler(); break;
            case 7: app.displayResults(); break;
            case 8: app.displayMetrics(); break;
            case 9: app.runBenchmark(); break;
            case 10: app.exportResults(); break;
            case 11: running = false; break;
        }
    }
    std::cout << "Goodbye.\n";
    return 0;
}
