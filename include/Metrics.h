#pragma once
#include <string>
#include <vector>
#include "Task.h"

// -----------------------------------------------------------------------------
// Metrics
// -----------------------------------------------------------------------------
// Aggregates per-task timing data (already computed on each Task by
// Task::markCompleted) into summary statistics, and handles printing /
// CSV export. Nothing here is hardcoded: every number is derived from
// the `tasks` vector passed in, which holds the actual results of a run.
// -----------------------------------------------------------------------------
struct MetricsSummary {
    std::string algorithm;
    int numWorkers = 0;
    int quantum = 0;               // 0 / unused for non-RR algorithms
    size_t numTasks = 0;

    double avgWaitingTime = 0.0;
    double avgTurnaroundTime = 0.0;
    double avgResponseTime = 0.0;
    double throughput = 0.0;       // tasks completed per tick
    long long totalExecutionTime = 0; // ticks from engine start to last completion
};

class Metrics {
public:
    // Computes aggregate statistics from a completed run's tasks.
    static MetricsSummary summarize(const std::vector<Task>& tasks,
                                     const std::string& algorithm,
                                     int numWorkers,
                                     int quantum);

    // Pretty-prints the per-task table to stdout.
    static void printTaskTable(const std::vector<Task>& tasks);

    // Pretty-prints the aggregate summary to stdout.
    static void printSummary(const MetricsSummary& summary);

    // Appends one row per run to a CSV file, writing a header first if
    // the file does not already exist. Used by the benchmark harness.
    static void appendCsv(const std::string& path, const MetricsSummary& summary);
};
