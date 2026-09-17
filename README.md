# Multithreaded Task Scheduling Engine

A **user-space** C++17 task scheduling engine that models classical OS
scheduling policies (FCFS, SJF, Priority, Round Robin) on top of a real
multithreaded worker pool with a shared, mutex/condition_variable-protected
ready queue.

> **Important scope note:** this project models scheduling *concepts*. It
> does not hook into the OS kernel, does not control real CPU cores, and
> does not claim to be a kernel-level scheduler. "Execution" of a task is
> simulated by having a worker thread sleep for an amount of time
> proportional to the task's burst time.

---

## 1. Objective

Build a scheduler that:

- Accepts/generates tasks with arrival time, burst time, and priority.
- Orders their execution using a pluggable scheduling policy.
- Dispatches them to a configurable pool of worker threads.
- Synchronizes access to the shared ready queue safely (no races, no
  busy-waiting, no deadlocks).
- Measures real waiting/turnaround/response time and throughput from
  actual execution — nothing is hardcoded.
- Supports benchmarking across task counts, worker counts, quanta, and
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
                     │  ┌─────────────────┐  │          │ (FCFS/SJF/Priority/RR)
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
point** (see Section 12).

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

## 10. Build Instructions

Requires a C++17 compiler and CMake ≥ 3.14.

```bash
cd multithreaded-task-scheduler
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

This produces two binaries in `build/`:

- `scheduler_app` — the CLI application
- `scheduler_tests` — the test suite

## 11. Test Instructions

```bash
cd build
./scheduler_tests
# or, via CTest:
ctest --output-on-failure
```

Tests cover (see `tests/test_main.cpp`):

- FCFS / SJF / Priority / Round Robin correctness (including tie-breaking)
- Empty task queue, single task
- Identical arrival times, identical priorities, identical burst times
- Very small (1) and very large (1000) Round Robin quanta
- Different worker counts (1/2/4/8)
- `ThreadPool` job execution correctness and graceful/idempotent shutdown

All 15 test cases have been verified to pass repeatably, and the whole
suite has been run clean under **ThreadSanitizer** (`-fsanitize=thread`)
with zero data-race reports.

## 12. Example Execution

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

## 13. Limitations

- Execution is *simulated* via `sleep_for`, not real CPU-bound work;
  burst time is "how long to pretend to work," not floating-point
  operations performed.
- SJF/Priority selection is O(n) per pick (see Section 7's tradeoff
  discussion) — fine for the scale this project targets, but a
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

## 14. Future Improvements

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

---

# 15. Interview Questions & Answers

**Q1: Why does `TaskQueue::pop()` take the `Scheduler` as a parameter
instead of `TaskQueue` owning a `Scheduler` reference?**
A: It keeps `TaskQueue` a pure, policy-agnostic synchronization
primitive. Any code path can hand it a *different* scheduler on a
different call if needed (not used today, but it decouples "shared
thread-safe container" from "current policy," which is a cleaner
separation of concerns and made unit-testing each scheduler trivial —
tests construct a bare deque and call `selectNext()` directly via the
engine without needing a whole `TaskQueue`.

**Q2: How do you guarantee workers never busy-wait?**
A: `TaskQueue::pop()` and `ThreadPool::workerLoop()` both call
`cv_.wait(lock, predicate)`. The predicate form atomically re-checks the
condition under the lock every time the thread wakes (whether from a
real notify or a spurious wakeup), and the thread stays blocked
(consuming no CPU) until the predicate is true. There is no loop that
polls `empty()` and sleeps/retries.

**Q3: Why is `stopping_` in `ThreadPool` checked but the queue not
cleared on shutdown?**
A: That's what makes shutdown *graceful* rather than abrupt. Setting
`stopping_ = true` only stops new jobs from being submitted; the
predicate in `workerLoop()` is `stopping_ || !jobs_.empty()`, so a
worker will keep draining the queue as long as jobs remain, and only
exits once both `stopping_` is true *and* the queue is empty.

**Q4: Where exactly could a deadlock have crept in, and how did you
avoid it?**
A: The two mutexes (`TaskQueue::mutex_` and `ThreadPool::mutex_`) are
never held simultaneously by the same thread — a worker's job function
(the engine's consumer loop) calls `TaskQueue::pop()`, which acquires
and releases `TaskQueue::mutex_` entirely before returning, and that
happens *after* `ThreadPool::workerLoop()` has already released its own
lock (job execution happens outside `ThreadPool`'s lock). With only one
mutex ever held at a time by a given thread, circular-wait — a
necessary condition for deadlock — cannot occur.

**Q5: How does Round Robin correctly track `remaining_time` across
multiple dispatches of the same task?**
A: `Task` is copied out of the queue by value on each `pop()`. The
worker decrements `remaining_time` by the ticks it actually ran
(`min(remaining_time, quantum)`), and if it's still > 0, pushes that
*same, updated* `Task` copy back onto the queue via
`TaskQueue::requeue()`. `Task::markStarted()` is idempotent (only sets
`first_start_time` the first time), so response time still reflects the
task's very first dispatch even though it may run several times.

**Q6: Why is SJF/Priority selection O(n) instead of using a
`std::priority_queue`?**
A: See Section 7 — the ready container has to simultaneously support
FIFO pop-front (for FCFS/RR) and "find-min-and-erase-anywhere" (for
SJF/Priority) through one shared interface, which a
`std::priority_queue` doesn't support (no efficient arbitrary erase). A
`std::deque` with a linear scan is simple, provably correct, and fast
enough given that the ready queue is typically much smaller than the
full workload. A production-grade version could swap in a custom
indexed heap if ready-queue sizes became a bottleneck — this is a
conscious simplicity/performance tradeoff, not an oversight.

**Q7: What happens if `numWorkers` exceeds the number of tasks?**
A: Nothing breaks — extra workers just block in `TaskQueue::pop()`
until either a task appears or `closeProduction()` fires (once
admission has pushed everything and the deque is empty), at which point
they see `closed_ && ready_.empty()` and exit cleanly. This is exercised
directly by the `EmptyTaskQueue_ProducesNoResultsAndDoesNotHang` and
`DifferentWorkerCounts_*` tests.

**Q8: How would you prove there are no data races, beyond "the tests
pass"?**
A: The whole test suite was also run under Clang/GCC's
**ThreadSanitizer** (`-fsanitize=thread`), which instruments every
shared-memory access and flags happens-before violations even if a race
doesn't happen to manifest incorrect output on a given run. It reported
zero issues.

**Q9: Why simulate execution with `sleep_for` instead of a busy loop
doing real work?**
A: A tight busy loop would peg a CPU core per worker for the whole
run, which is wasteful and would also make wall-clock timing far less
predictable (contending with the OS scheduler for real cycles).
`sleep_for` yields the core back to the OS while "executing," so many
more workers/tasks can be simulated cheaply, and the measured tick
counts stay close to the intended values since `sleep_for` is guaranteed
to sleep *at least* the requested duration.

**Q10: This is described as "not a kernel-level CPU scheduler" — what
specifically is different from a real OS scheduler?**
A: A few key things: (1) it operates entirely in one user-space
process using `std::thread`, so the *real* OS scheduler is still the
one deciding which of our worker threads actually gets CPU time and
when — we're scheduling *our own* logical tasks onto *our own* worker
threads, not onto physical cores directly; (2) there's no notion of
interrupts, syscalls, or context-switch cost; (3) "execution" is
simulated via sleeping rather than doing real preemptible work; (4)
there's a single global ready queue rather than the per-core run queues
and load-balancing/migration logic real multi-core schedulers use.
