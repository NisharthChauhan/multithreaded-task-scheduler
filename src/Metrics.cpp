#include "Metrics.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sys/stat.h>

MetricsSummary Metrics::summarize(const std::vector<Task>& tasks,
                                   const std::string& algorithm,
                                   int numWorkers,
                                   int quantum) {
    MetricsSummary s;
    s.algorithm = algorithm;
    s.numWorkers = numWorkers;
    s.quantum = quantum;
    s.numTasks = tasks.size();

    if (tasks.empty()) {
        return s; // all zeros -- an empty run is a valid, tested case
    }

    long long sumWaiting = 0, sumTurnaround = 0, sumResponse = 0;
    long long maxCompletion = 0, minArrival = tasks.front().arrival_time;

    for (const auto& t : tasks) {
        sumWaiting += t.waiting_time;
        sumTurnaround += t.turnaround_time;
        sumResponse += t.response_time;
        maxCompletion = std::max(maxCompletion, t.completion_time);
        minArrival = std::min<long long>(minArrival, t.arrival_time);
    }

    s.avgWaitingTime = static_cast<double>(sumWaiting) / s.numTasks;
    s.avgTurnaroundTime = static_cast<double>(sumTurnaround) / s.numTasks;
    s.avgResponseTime = static_cast<double>(sumResponse) / s.numTasks;

    s.totalExecutionTime = maxCompletion - minArrival;
    s.throughput = s.totalExecutionTime > 0
                       ? static_cast<double>(s.numTasks) / s.totalExecutionTime
                       : static_cast<double>(s.numTasks);
    return s;
}

void Metrics::printTaskTable(const std::vector<Task>& tasks) {
    std::cout << "    ID | Arrival | Burst | Priority | Start | Completion | Waiting | Turnaround | Response\n";
    std::cout << "    " << std::string(88, '-') << "\n";
    for (const auto& t : tasks) {
        std::cout << "    " << t.toString() << "\n";
    }
}

void Metrics::printSummary(const MetricsSummary& s) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Algorithm: " << s.algorithm << "\n";
    std::cout << "Workers: " << s.numWorkers;
    if (s.quantum > 0) std::cout << "   Quantum: " << s.quantum;
    std::cout << "\n\n";
    std::cout << "Average Waiting Time:    " << s.avgWaitingTime << "\n";
    std::cout << "Average Turnaround Time: " << s.avgTurnaroundTime << "\n";
    std::cout << "Average Response Time:   " << s.avgResponseTime << "\n";
    std::cout << "Throughput:              " << s.throughput << " tasks/tick\n";
    std::cout << "Total Execution Time:    " << s.totalExecutionTime << " ticks\n";
}

void Metrics::appendCsv(const std::string& path, const MetricsSummary& s) {
    // Best-effort: create the parent directory (e.g. "results/") if it
    // doesn't exist yet, so a fresh checkout works without manual setup.
    auto slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        std::string dir = path.substr(0, slash);
        mkdir(dir.c_str(), 0755); // ignore errors: dir may already exist
    }

    struct stat buffer;
    bool exists = (stat(path.c_str(), &buffer) == 0);

    std::ofstream out(path, std::ios::app);
    if (!out) {
        std::cerr << "Warning: could not open " << path << " for writing\n";
        return;
    }
    if (!exists) {
        out << "algorithm,num_workers,quantum,num_tasks,avg_waiting_time,"
               "avg_turnaround_time,avg_response_time,throughput,total_execution_time\n";
    }
    out << s.algorithm << ',' << s.numWorkers << ',' << s.quantum << ',' << s.numTasks << ','
        << s.avgWaitingTime << ',' << s.avgTurnaroundTime << ',' << s.avgResponseTime << ','
        << s.throughput << ',' << s.totalExecutionTime << "\n";
}
