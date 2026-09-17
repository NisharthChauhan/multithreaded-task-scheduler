// -----------------------------------------------------------------------------
// tests/test_main.cpp
//
// A small, dependency-free test harness (no GoogleTest/Catch2 -- the spec
// asks us to avoid external libraries unless absolutely necessary). Each
// TEST_CASE registers itself; RUN_ALL runs them in order and prints a
// pass/fail summary. main() returns non-zero if any test failed, so this
// integrates fine with `ctest`.
// -----------------------------------------------------------------------------
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "FCFSScheduler.h"
#include "PriorityScheduler.h"
#include "RoundRobinScheduler.h"
#include "SJFScheduler.h"
#include "SchedulingEngine.h"
#include "ThreadPool.h"
#include "WorkloadGenerator.h"

namespace {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

#define TEST_CASE(name)                                                 \
    void name();                                                        \
    Registrar registrar_##name(#name, name);                            \
    void name()

int g_failures = 0;
std::string g_currentTest;

void checkImpl(bool cond, const std::string& expr, const char* file, int line) {
    if (!cond) {
        ++g_failures;
        std::cout << "  [FAIL] " << g_currentTest << " -- " << expr << " (" << file << ":" << line
                  << ")\n";
    }
}

#define CHECK(cond) checkImpl((cond), #cond, __FILE__, __LINE__)

// Small tick size keeps the whole suite fast (~a few hundred ms per test)
// while still leaving enough real wall-clock margin per tick that OS
// scheduling jitter can't push a task's measured tick count across a
// boundary. See SchedulingEngine.h for how ticks map to milliseconds.
constexpr int kTestTickMillis = 15;

// Finds a completed task by id in a results vector; asserts it exists.
const Task& findTask(const std::vector<Task>& results, int id) {
    for (const auto& t : results) {
        if (t.id == id) return t;
    }
    static Task missing;
    throw std::runtime_error("task id " + std::to_string(id) + " not found in results");
}

} // namespace

// -----------------------------------------------------------------------------
// FCFS correctness
// -----------------------------------------------------------------------------
TEST_CASE(FCFS_CompletesInArrivalOrder) {
    std::vector<Task> tasks = {
        Task(1, 0, 5, 0),
        Task(2, 1, 3, 0),
        Task(3, 2, 8, 0),
        Task(4, 3, 6, 0),
    };
    SchedulingEngine engine(tasks, std::make_shared<FCFSScheduler>(), /*workers=*/1,
                             kTestTickMillis);
    auto results = engine.run();
    CHECK(results.size() == 4);
    // With one worker, FCFS must complete tasks in exactly arrival order.
    CHECK(results[0].id == 1);
    CHECK(results[1].id == 2);
    CHECK(results[2].id == 3);
    CHECK(results[3].id == 4);
    // Formula invariants must hold for every task regardless of timing.
    for (const auto& t : results) {
        CHECK(t.turnaround_time == t.completion_time - t.arrival_time);
        CHECK(t.waiting_time == t.turnaround_time - t.burst_time);
        CHECK(t.response_time == t.first_start_time - t.arrival_time);
    }
    // The very first task starts immediately: zero waiting/response time.
    const Task& first = findTask(results, 1);
    CHECK(first.waiting_time == 0);
    CHECK(first.response_time == 0);
}

// -----------------------------------------------------------------------------
// SJF correctness
// -----------------------------------------------------------------------------
TEST_CASE(SJF_PicksShortestRemainingBurstAmongReadyTasks) {
    // Task 1 occupies the single worker from tick 0-10, so by the time it
    // finishes, tasks 2/3/4 have all arrived and are simultaneously ready.
    // SJF must then pick the shortest one first: 3 (burst 1), then 2
    // (burst 3), then 4 (burst 5).
    std::vector<Task> tasks = {
        Task(1, 0, 10, 0),
        Task(2, 1, 3, 0),
        Task(3, 1, 1, 0),
        Task(4, 1, 5, 0),
    };
    SchedulingEngine engine(tasks, std::make_shared<SJFScheduler>(), /*workers=*/1,
                             kTestTickMillis);
    auto results = engine.run();
    CHECK(results.size() == 4);
    CHECK(results[0].id == 1);
    CHECK(results[1].id == 3);
    CHECK(results[2].id == 2);
    CHECK(results[3].id == 4);
}

TEST_CASE(SJF_TiesOnIdenticalBurstTimeBrokenByArrivalThenId) {
    std::vector<Task> tasks = {
        Task(1, 0, 10, 0), // occupies the worker so 2/3 both become ready first
        Task(2, 1, 4, 0),
        Task(3, 1, 4, 0), // identical burst time to task 2
    };
    SchedulingEngine engine(tasks, std::make_shared<SJFScheduler>(), /*workers=*/1,
                             kTestTickMillis);
    auto results = engine.run();
    CHECK(results.size() == 3);
    CHECK(results[0].id == 1);
    // Same burst + same arrival -> tie broken by id: 2 before 3.
    CHECK(results[1].id == 2);
    CHECK(results[2].id == 3);
}

// -----------------------------------------------------------------------------
// Priority scheduling correctness
// -----------------------------------------------------------------------------
TEST_CASE(Priority_SmallerValueRunsFirst) {
    // Same shape as the SJF test: task 1 blocks the worker until tasks
    // 2/3/4 have all arrived. Priority (smaller = higher) should then
    // pick 3 (priority 1), then 4 (priority 2), then 2 (priority 3).
    std::vector<Task> tasks = {
        Task(1, 0, 10, 5),
        Task(2, 1, 3, 3),
        Task(3, 1, 1, 1),
        Task(4, 1, 5, 2),
    };
    SchedulingEngine engine(tasks, std::make_shared<PriorityScheduler>(), /*workers=*/1,
                             kTestTickMillis);
    auto results = engine.run();
    CHECK(results.size() == 4);
    CHECK(results[0].id == 1);
    CHECK(results[1].id == 3);
    CHECK(results[2].id == 4);
    CHECK(results[3].id == 2);
}

TEST_CASE(Priority_IdenticalPrioritiesBrokenByArrivalThenId) {
    std::vector<Task> tasks = {
        Task(1, 0, 10, 5), // blocks the worker so 2/3 become ready together
        Task(2, 1, 4, 7),
        Task(3, 1, 6, 7), // identical priority to task 2
    };
    SchedulingEngine engine(tasks, std::make_shared<PriorityScheduler>(), /*workers=*/1,
                             kTestTickMillis);
    auto results = engine.run();
    CHECK(results.size() == 3);
    CHECK(results[0].id == 1);
    CHECK(results[1].id == 2); // tie broken by id
    CHECK(results[2].id == 3);
}

// -----------------------------------------------------------------------------
// Round Robin correctness
// -----------------------------------------------------------------------------
TEST_CASE(RoundRobin_ShortTasksFinishAheadOfLongTaskDespiteFIFOArrival) {
    // id1 (burst 2) finishes in a single quantum; id2 (burst 6) gets
    // preempted repeatedly; id3 (burst 2) slips in and finishes before
    // id2 because id2 keeps getting re-queued behind it.
    std::vector<Task> tasks = {
        Task(1, 0, 2, 0),
        Task(2, 0, 6, 0),
        Task(3, 0, 2, 0),
    };
    auto scheduler = std::make_shared<RoundRobinScheduler>(/*quantum=*/2);
    SchedulingEngine engine(tasks, scheduler, /*workers=*/1, kTestTickMillis);
    auto results = engine.run();
    CHECK(results.size() == 3);
    CHECK(results[0].id == 1);
    CHECK(results[1].id == 3);
    CHECK(results[2].id == 2);
    // id2's remaining_time must have been correctly decremented across
    // three separate quantum slices (2+2+2=6) down to exactly zero.
    const Task& t2 = findTask(results, 2);
    CHECK(t2.isFinished());
    CHECK(t2.remaining_time == 0);
}

TEST_CASE(RoundRobin_VerySmallQuantumStillCompletesAllTasks) {
    std::vector<Task> tasks = {
        Task(1, 0, 5, 0),
        Task(2, 0, 3, 0),
        Task(3, 0, 4, 0),
    };
    auto scheduler = std::make_shared<RoundRobinScheduler>(/*quantum=*/1);
    SchedulingEngine engine(tasks, scheduler, /*workers=*/1, kTestTickMillis);
    auto results = engine.run();
    CHECK(results.size() == 3);
    for (const auto& t : results) {
        CHECK(t.remaining_time == 0);
        CHECK(t.turnaround_time == t.completion_time - t.arrival_time);
    }
}

TEST_CASE(RoundRobin_LargeQuantumDegeneratesToFCFSOrder) {
    // Quantum bigger than every burst -> every task finishes in a single
    // slice -> completion order collapses to plain FIFO/FCFS order.
    std::vector<Task> tasks = {
        Task(1, 0, 3, 0),
        Task(2, 0, 2, 0),
        Task(3, 0, 4, 0),
    };
    auto scheduler = std::make_shared<RoundRobinScheduler>(/*quantum=*/1000);
    SchedulingEngine engine(tasks, scheduler, /*workers=*/1, kTestTickMillis);
    auto results = engine.run();
    CHECK(results.size() == 3);
    CHECK(results[0].id == 1);
    CHECK(results[1].id == 2);
    CHECK(results[2].id == 3);
}

// -----------------------------------------------------------------------------
// Edge cases
// -----------------------------------------------------------------------------
TEST_CASE(EmptyTaskQueue_ProducesNoResultsAndDoesNotHang) {
    std::vector<Task> tasks; // empty
    SchedulingEngine engine(tasks, std::make_shared<FCFSScheduler>(), /*workers=*/4,
                             kTestTickMillis);
    auto results = engine.run();
    CHECK(results.empty());
}

TEST_CASE(SingleTask_HasZeroWaitingAndResponseTime) {
    std::vector<Task> tasks = {Task(1, 0, 4, 0)};
    SchedulingEngine engine(tasks, std::make_shared<FCFSScheduler>(), /*workers=*/1,
                             kTestTickMillis);
    auto results = engine.run();
    CHECK(results.size() == 1);
    CHECK(results[0].waiting_time == 0);
    CHECK(results[0].response_time == 0);
    CHECK(results[0].remaining_time == 0);
}

TEST_CASE(IdenticalArrivalTimes_AllTasksEventuallyComplete) {
    std::vector<Task> tasks = {
        Task(1, 0, 3, 1),
        Task(2, 0, 2, 2),
        Task(3, 0, 4, 3),
        Task(4, 0, 1, 1),
    };
    std::vector<std::shared_ptr<Scheduler>> schedulers = {
        std::make_shared<FCFSScheduler>(), std::make_shared<SJFScheduler>(),
        std::make_shared<PriorityScheduler>(), std::make_shared<RoundRobinScheduler>(2)};
    for (auto& scheduler : schedulers) {
        SchedulingEngine engine(tasks, scheduler, /*workers=*/2, kTestTickMillis);
        auto results = engine.run();
        CHECK(results.size() == 4);
        for (const auto& t : results) CHECK(t.remaining_time == 0);
    }
}

TEST_CASE(IdenticalBurstTimes_SJF_AllComplete) {
    std::vector<Task> tasks = {
        Task(1, 0, 4, 0),
        Task(2, 0, 4, 0),
        Task(3, 0, 4, 0),
    };
    SchedulingEngine engine(tasks, std::make_shared<SJFScheduler>(), /*workers=*/1,
                             kTestTickMillis);
    auto results = engine.run();
    CHECK(results.size() == 3);
    CHECK(results[0].id == 1);
    CHECK(results[1].id == 2);
    CHECK(results[2].id == 3);
}

TEST_CASE(DifferentWorkerCounts_AllTasksCompleteRegardlessOfPoolSize) {
    for (int workers : {1, 2, 4, 8}) {
        auto tasks = WorkloadGenerator::generate(/*numTasks=*/12, /*seed=*/7);
        SchedulingEngine engine(tasks, std::make_shared<FCFSScheduler>(), workers, kTestTickMillis);
        auto results = engine.run();
        CHECK(results.size() == 12);
        for (const auto& t : results) CHECK(t.remaining_time == 0);
    }
}

// -----------------------------------------------------------------------------
// ThreadPool: reuse + graceful shutdown
// -----------------------------------------------------------------------------
TEST_CASE(ThreadPool_ExecutesAllSubmittedJobsExactlyOnce) {
    ThreadPool pool(4);
    std::mutex m;
    int counter = 0;
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 50; ++i) {
        futures.push_back(pool.submit([&m, &counter] {
            std::lock_guard<std::mutex> lock(m);
            ++counter;
        }));
    }
    for (auto& f : futures) f.get();
    CHECK(counter == 50);
    CHECK(pool.workerCount() == 4);
}

TEST_CASE(ThreadPool_ShutdownIsGracefulAndIdempotent) {
    ThreadPool pool(2);
    auto f1 = pool.submit([] { return 1 + 1; });
    CHECK(f1.get() == 2);
    pool.shutdown();
    pool.shutdown(); // must not crash or hang when called twice
    bool threw = false;
    try {
        pool.submit([] {});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw); // submitting after shutdown must be rejected, not silently dropped
}

int main() {
    for (auto& tc : registry()) {
        g_currentTest = tc.name;
        std::cout << "[RUN ] " << tc.name << "\n";
        try {
            tc.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::cout << "  [FAIL] " << tc.name << " threw exception: " << e.what() << "\n";
        }
    }
    std::cout << "\n"
              << (registry().size() - g_failures) << "/" << registry().size()
              << " checks-clean test cases (" << g_failures << " failed assertions total)\n";
    return g_failures == 0 ? 0 : 1;
}
