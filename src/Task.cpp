#include "Task.h"
#include <sstream>
#include <iomanip>

std::string Task::toString() const {
    std::ostringstream oss;
    oss << std::setw(6) << id << " | "
        << std::setw(7) << arrival_time << " | "
        << std::setw(5) << burst_time << " | "
        << std::setw(8) << priority << " | "
        << std::setw(5) << first_start_time << " | "
        << std::setw(10) << completion_time << " | "
        << std::setw(7) << waiting_time << " | "
        << std::setw(10) << turnaround_time << " | "
        << std::setw(8) << response_time;
    return oss.str();
}
