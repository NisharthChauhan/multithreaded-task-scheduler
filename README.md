# Multithreaded Task Scheduling Engine

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![CMake](https://img.shields.io/badge/CMake-3.14%2B-064F8C.svg)

A user-space C++17 task scheduling engine that models classical operating
system scheduling policies on top of a real multithreaded worker pool.

The project supports **FCFS**, **SJF**, **Priority**, and **Round Robin**
scheduling with a shared, mutex- and condition-variable-protected ready
queue. It measures scheduling metrics from actual simulated execution and can
export benchmark results to CSV.

> [!IMPORTANT]
> This project models scheduling concepts in user space. It does not hook into
> the operating-system kernel, control real CPU cores, or claim to be a
> kernel-level scheduler. Task execution is simulated with `sleep_for` using a
> duration proportional to each task's burst time.

## Contents

- [Quick Start](#quick-start)
- [Interactive Usage](#interactive-usage)
- [Project Layout](#project-layout)
- [1. Objective](#1-objective)
- [2. Architecture](#2-architecture)
- [3. Thread-Pool Design](#3-thread-pool-design)
- [4. Producer-Consumer Model](#4-producer-consumer-model)
- [5. Synchronization Strategy](#5-synchronization-strategy)
- [6. Scheduling Algorithms](#6-scheduling-algorithms)
- [7. Data Structures & Complexity](#7-data-structures--complexity)
- [8. Metrics](#8-metrics)
- [9. Benchmark Methodology](#9-benchmark-methodology)
- [10. Test Coverage](#10-test-coverage)
- [11. Example Execution](#11-example-execution)
- [12. Limitations](#12-limitations)
- [13. Future Improvements](#13-future-improvements)

For a deep-dive on design-decision reasoning (deadlock avoidance, why
`sleep_for`, complexity tradeoffs, etc.), see
[docs/INTERVIEW_QNA.md](docs/INTERVIEW_QNA.md).

## Quick Start

### Requirements

- A C++17-compatible compiler
- CMake 3.14 or newer
- POSIX threads support

### Build

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

This creates two executables in `build/`:

- `scheduler_app` - the interactive command-line application
- `scheduler_tests` - the test suite

### Run

```bash
./build/scheduler_app
```

### Test

```bash
ctest --test-dir build --output-on-failure
```

You can also run the test executable directly:

```bash
./build/scheduler_tests
```

## Interactive Usage

When the application starts, it loads a small predefined workload so it can
be run immediately. The menu allows you to:

1. Generate a reproducible random workload.
2. Load one of several predefined workloads.
3. Select FCFS, SJF, Priority, or Round Robin.
4. Configure the number of worker threads.
5. Configure the Round Robin quantum.
6. Run the scheduler.
7. Display per-task execution results.
8. Display aggregate performance metrics.
9. Run the benchmark suite.
10. Export the latest summary to `results/last_run.csv`.

For example, choose option `2` to load the textbook workload, option `4` to
select the worker count, and option `6` to run the scheduler.

## Project Layout

```text
include/    Public headers and scheduler interfaces
src/        Engine, scheduling policies, CLI, metrics, and workload code
tests/      Unit and concurrency tests
results/    Generated CSV output (ignored by Git)
build/      Generated CMake output (ignored by Git)
```

## 1. Objective

The scheduler is designed to:

- Accept/generate tasks with arrival time, burst time, and priority.
- Order their execution using a pluggable scheduling policy.
- Dispatch them to a configurable pool of worker threads.
- Synchronize access to the shared ready queue safely (no races, no
  busy-waiting, no deadlocks).
- Measure real waiting/turnaround/response time and throughput from
  actual execution — nothing is hardcoded.
- Support benchmarking across task counts, worker counts, quanta, and
  workload shapes, exporting results to CSV.

## 2. Architecture

```
                     ┌─────────────────────┐
                     │   WorkloadGenerator   │  (producer of Task specs)
                     └──────────┬───────────┘
                                │ vector<Task>
                                ▼
                     ┌─────────────────────┐
                     │   SchedulingEngine    │
                     │                       │
                     │  ┌─────────────────┐  │
                     │  │ admissionLoop() │──┼──push()──┐
                     │  │  (producer      │  │          │
                     │  │   thread)       │  │          ▼
                     │  └─────────────────┘  │   ┌─────────────┐
                     │                       │   │  TaskQueue   │  mutex +
                     │                       │   │ (ready deque)│  condition_
                     │                       │   └──────┬──────┘  variable
                     │                       │          │ pop() via Scheduler
                     │                       │          │ (FCFS/SJF/Priority/RR)
                     │  ┌─────────────────┐  │          │
                     │  │  ThreadPool     │◀─┼──────────┘
                     │  │ worker worker … │  │  (consumers)
                     │  └─────────────────┘  │
                     └──────────┬───────────┘
                                │ vector<Task> (completed, with metrics)
                                ▼
                     ┌─────────────────────┐
                     │       Metrics         │  → console table + CSV
                     └─────────────────────┘
```

### Key design decision: how scheduling meets multithreading

Classical textbook scheduling algorithms (FCFS/SJF/Priority/RR) describe
**one CPU** deciding which of several ready processes to run next. This
project generalizes that to **N worker threads** competing to pull the
next task from one shared ready queue, where "next" is still decided by
the chosen policy. Concretely:

- **`Task`** (`Task.h`) is a plain data struct: scheduling inputs
  (id, arrival, burst, priority) plus scheduling outputs (start,
  completion, waiting, turnaround, response) and a `remaining_time`
  counter used by Round Robin.
- **`Scheduler`** (`Scheduler.h`) is a tiny abstract interface with one
  real method: `selectNext(ready_deque)` — remove and return the next
  task to run from whatever is currently ready. `FCFSScheduler`,
  `SJFScheduler`, `PriorityScheduler`, and `RoundRobinScheduler` each
  implement just this one method (plus `isPreemptive()`/`quantum()` for
  RR). This is the *only* place algorithm-specific logic lives.
- **`TaskQueue`** (`TaskQueue.h`) owns the shared ready deque, a
  `std::mutex`, and a `std::condition_variable`. It doesn't know or care
  which policy is active — it just calls `scheduler.selectNext()` while
  holding its lock.
- **`SchedulingEngine`** (`SchedulingEngine.h`) is the orchestrator:
  - An **admission thread** (the producer) walks the task list sorted by
    arrival time, sleeps until each task's (simulated) arrival tick, and
    pushes it into `TaskQueue`.
  - `numWorkers` **consumer loops** (submitted as jobs to `ThreadPool`)
    each repeatedly pop a task via the active `Scheduler`, "execute" it
    by sleeping for the appropriate number of ticks, and either mark it
    complete or (for Round Robin) push the unfinished remainder back
    onto the queue.
- **`ThreadPool`** (`ThreadPool.h`) is a fully generic, reusable
  `std::function<void()>` job executor — it has no knowledge of tasks or
  scheduling at all, and is independently unit-tested.
- **`Metrics`** (`Metrics.h`) turns a finished run's `vector<Task>` into
  averages/throughput, prints tables, and appends CSV rows.

This separation means: adding a new scheduling policy = write one new
`Scheduler` subclass. Nothing else changes.

## 3. Thread-Pool Design

`ThreadPool` (see `include/ThreadPool.h`, `src/ThreadPool.cpp`):

- Constructor spawns exactly `numWorkers` `std::thread`s, each running
  `workerLoop()`. **Threads are created once and reused** for every job
  submitted afterwards — this satisfies "reuse threads, don't spawn a
  thread per task."
- `submit(f, args...)` wraps the callable in a `std::packaged_task`,
  pushes a type-erased `std::function<void()>` onto a `std::queue`
  under `mutex_`, and returns a `std::future` so the caller can wait for
  (or retrieve the result of) that specific job.
- Each `workerLoop()` iteration: lock, `cv_.wait()` until there's a job
  or the pool is stopping, pop one job, **unlock before running it** (so
  other workers aren't blocked while this one executes), run it.
- `shutdown()` sets `stopping_ = true`, wakes every worker, and joins all
  threads. Crucially, `stopping_` does **not** clear the job queue, so
  any already-queued jobs are drained before the pool actually stops —
  this is the "graceful shutdown" requirement. `shutdown()` is
  idempotent (safe to call twice) and is also invoked automatically from
  the destructor as a safety net.

The `SchedulingEngine` uses `ThreadPool` by submitting exactly
`numWorkers` long-running jobs — each one *is* a consumer loop that pulls
from `TaskQueue` until the queue is closed and drained. This keeps "how
threads are managed" (`ThreadPool`) cleanly separate from "which task
runs next" (`Scheduler` + `TaskQueue`).

## 4. Producer-Consumer Model

There are two independent producer-consumer relationships in this
project, both using the same pattern (`std::mutex` + `std::condition_variable`,
RAII locking, no polling):

1. **Admission → Ready queue → Workers**
   The admission thread (producer) pushes a `Task` into `TaskQueue` once
   its arrival time has (simulated-)elapsed. Worker threads (consumers)
   block on `TaskQueue`'s condition variable until a task is available,
   pick the next one per the active policy, and run it. When admission
   finishes, it calls `closeProduction()`, which wakes every worker so
   they can notice "closed and empty" and exit their loop instead of
   blocking forever.

2. **`ThreadPool` job queue**
   `submit()` (producer) pushes a job and notifies one worker;
   `workerLoop()` (consumer) blocks until a job exists or the pool is
   stopping.

## 5. Synchronization Strategy

| Shared state | Protected by | Notes |
|---|---|---|
| `TaskQueue::ready_` (the scheduling deque) | `TaskQueue::mutex_` + `cv_` | `Scheduler::selectNext()` is always called *with the lock held*, so policies can safely reorder/erase in the middle of the deque. |
| `ThreadPool::jobs_` | `ThreadPool::mutex_` + `cv_` | Standard bounded-less job queue; job execution happens **outside** the lock. |
| `SchedulingEngine::completed_` | `SchedulingEngine::resultMutex_` | Multiple workers may finish tasks concurrently; guarded with a plain `std::lock_guard`. |

Principles applied throughout:

- **RAII locking only** — every critical section uses `std::lock_guard`
  or `std::unique_lock`; there is no manual `lock()`/`unlock()`, so an
  exception mid-section can never leave a mutex held.
- **No busy-waiting** — every consumer blocks on a `condition_variable`
  with a predicate (`cv_.wait(lock, predicate)`), which also protects
  against spurious wakeups.
- **Minimal critical sections** — data is copied out of a lock (`Task
  next = ready.front(); ...`) rather than doing expensive work while
  holding the mutex; simulated "execution" (`sleep_for`) always happens
  *outside* any lock.
- **No nested locks across the two mutexes** (`TaskQueue`'s and
  `ThreadPool`'s) — each is only ever held independently, which rules
  out the classic circular-wait deadlock pattern by construction.
- **Deterministic shutdown** — `closeProduction()` / `ThreadPool::shutdown()`
  always `notify_all()` (not `notify_one()`) so *every* blocked thread
  gets a chance to re-check its predicate and exit, avoiding stuck
  threads at teardown.

## 6. Scheduling Algorithms

All policies operate on the same shared ready deque; they differ only in
`selectNext()`.

- **FCFS** — pop the front of the deque. Because the admission thread
  pushes tasks in non-decreasing arrival-time order, deque order equals
  arrival order. **O(1)**.
- **SJF (non-preemptive)** — linear scan for the task with the smallest
  `remaining_time`; ties broken by arrival time, then task id. **O(n)**
  per selection (see complexity discussion below).
- **Priority (non-preemptive)** — linear scan for the smallest
  `priority` value. **Convention: smaller value = higher priority**
  (documented in `PriorityScheduler.h`). Same tie-breaking as SJF.
  **O(n)**.
- **Round Robin (preemptive)** — pop the front of the deque (like FCFS),
  **O(1)**. The engine runs it for `min(remaining_time, quantum)` ticks;
  if `remaining_time > 0` afterward, the engine pushes the task back
  onto the *same* deque (now at the back), which is how requeueing is
  implemented.

## 7. Data Structures & Complexity

| Structure | Used for | Complexity |
|---|---|---|
| `std::deque<Task>` (inside `TaskQueue`) | shared ready queue | push_back/pop_front O(1); SJF/Priority scan+erase O(n) |
| `std::queue<std::function<void()>>` (inside `ThreadPool`) | job queue | push/pop O(1) |
| `std::vector<Task>` | workload storage, admission list, completed results | O(1) amortized append |

**Why a `std::deque` + linear scan for SJF/Priority instead of a
`std::priority_queue`?** The ready container is shared between the
*admission producer* (which always appends) and *all four scheduling
policies* (FCFS/RR need FIFO pop-front; SJF/Priority need "find min,
erase arbitrary element"). A `std::priority_queue` gives O(log n)
selection but does not support efficient arbitrary-position erase or
FIFO semantics, so it can't serve all four policies through one
interface. A `std::deque` with an O(n) scan is simple, correct, and fast
enough for realistic ready-queue sizes (typically far smaller than the
whole workload, since tasks leave the deque once dispatched) — and it
keeps every policy implementable in ~10 lines with no separate data
structure per algorithm. **This tradeoff is a great interview talking
point** (see [docs/INTERVIEW_QNA.md](docs/INTERVIEW_QNA.md)).

## 8. Metrics

For every completed task:

```
Waiting Time    = Turnaround Time - Burst Time
Turnaround Time = Completion Time - Arrival Time
Response Time   = First Start Time - Arrival Time
```

Aggregate, computed from the actual completed-task list (`Metrics::summarize`):

- Average Waiting Time, Average Turnaround Time, Average Response Time
- Throughput = completed tasks / total execution ticks
- Total Execution Time = last completion tick − earliest arrival tick

Nothing here is a constant — every number is derived from real
`Task::markStarted()` / `Task::markCompleted()` calls made by worker
threads during the run.

## 9. Benchmark Methodology

`Run benchmark` (CLI option 9) sweeps:

- Task counts: `{10, 50, 100}`
- Worker counts: `{1, 2, 4, 8}`
- Round Robin quanta: `{1, 2, 5, 10}` (FCFS/SJF/Priority have no quantum)

For each `(taskCount, workerCount, algorithm[, quantum])` combination it
generates a **seeded** workload (same seed per task count, so every
algorithm/worker/quantum combination is compared on an identical
workload), runs the engine, and appends one row to
`results/benchmark.csv`.

> The spec's example configuration mentions 10/100/1000 tasks and quanta
> 1/2/5/10 across 1/2/4/8 workers. The shipped defaults (`10, 50, 100`)
> keep a full sweep under ~30 seconds; to reproduce the exact example
> configuration (including 1000 tasks), edit the `taskCounts` vector in
> `Application::runBenchmark()` in `src/main.cpp` — everything else
> (CSV schema, per-run measurement) is unchanged.

CSV schema:

```
algorithm,num_workers,quantum,num_tasks,avg_waiting_time,avg_turnaround_time,avg_response_time,throughput,total_execution_time
```

## 10. Test Coverage

The suite (`tests/test_main.cpp`, run via `ctest` — see [Quick Start](#quick-start))
covers:

- FCFS / SJF / Priority / Round Robin correctness (including tie-breaking)
- Empty task queue, single task
- Identical arrival times, identical priorities, identical burst times
- Very small (1) and very large (1000) Round Robin quanta
- Different worker counts (1/2/4/8)
- `ThreadPool` job execution correctness and graceful/idempotent shutdown

All 15 test cases have been verified to pass repeatably, and the whole
suite has been run clean under **ThreadSanitizer** (`-fsanitize=thread`)
with zero data-race reports.

## 11. Example Execution

```
$ ./scheduler_app
Multithreaded Task Scheduling Engine (user-space, C++17)
Note: this models OS scheduling concepts; it is not a kernel scheduler.

=== Multithreaded Task Scheduling Engine ===
 1. Generate workload
 2. Load predefined workload
 ...
Choose an option (1-11): 2
  Loaded workload 0 (4 tasks).
Choose an option (1-11): 4
  Number of worker threads (1-64): 1
Choose an option (1-11): 6
  Running FCFS with 1 worker(s) on 4 task(s)...
  Done. 4 task(s) completed.
Choose an option (1-11): 8

Algorithm: FCFS
Workers: 1

Average Waiting Time:    5.75
Average Turnaround Time: 11.25
Average Response Time:   5.75
Throughput:              0.18 tasks/tick
Total Execution Time:    22 ticks
```

(Hand-verified: task bursts 5/3/8/6 arriving at 0/1/2/3 with a single
worker produce exactly these numbers — see the FCFS unit test for the
same computation.)

With **4 workers** instead of 1, the same workload finishes with
**zero average waiting time**, because each task gets its own worker
immediately on arrival — a good live demonstration of why worker count
matters as much as scheduling policy.

## 12. Limitations

- Execution is *simulated* via `sleep_for`, not real CPU-bound work;
  burst time is "how long to pretend to work," not floating-point
  operations performed.
- SJF/Priority selection is O(n) per pick (see [Section 7](#7-data-structures--complexity)'s
  tradeoff discussion) — fine for the scale this project targets, but a
  production scheduler serving very large ready queues would want a
  different structure (e.g., an indexed heap supporting decrease-key /
  arbitrary removal).
- Round Robin's fairness guarantees assume all workers pull from one
  shared queue; per-worker run queues (as real multi-core OS schedulers
  often use) are not modeled.
- Timing precision is bounded by OS thread-scheduling jitter and
  `sleep_for`'s guarantees (>= requested duration, not exact); the tick
  size (`kDefaultTickMillis` / `kTestTickMillis`) is chosen large enough
  that this jitter doesn't change measured tick counts in practice, but
  it's not a hard real-time guarantee.
- No priority aging / starvation prevention is implemented for the
  Priority scheduler — a low-priority task can wait indefinitely if
  higher-priority tasks keep arriving.

## 13. Future Improvements

- Aging for Priority scheduling to prevent starvation.
- Preemptive SJF/Priority (shortest-remaining-time-first / preemptive
  priority) alongside the existing non-preemptive versions.
- Multi-level feedback queues.
- Per-worker affinity / work-stealing instead of one global ready queue.
- Replace the O(n) SJF/Priority scan with an intrusive heap supporting
  O(log n) arbitrary removal, once the same container needs to also
  support FCFS/RR-style FIFO semantics cleanly.
- A machine-readable JSON export alongside CSV, for easier downstream
  plotting.
