// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// ThreadPool (component #5): a fixed-size priority work pool — the Milestone 6 closer,
// composing the milestone's primitives. Tasks are type-erased move-only callables in a
// monitor-guarded binary heap ordered by (priority desc, sequence asc), so equal-priority
// tasks run in submission order; submit() returns a TaskFuture carrying the task's result
// or exception. Shutdown drains: queued work finishes before the workers join. ADR-0017.
#ifndef IT_D4NP_UTIL_THREAD_POOL_HPP
#define IT_D4NP_UTIL_THREAD_POOL_HPP

#include <it/d4np/util/task_future.hpp>

#include <algorithm>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace it::d4np::util {

namespace detail {

/// Type-erased move-only nullary callable. std::function requires copyable callables — a
/// submitted task owns a TaskPromise (move-only) — and std::move_only_function is C++23,
/// beyond the toolchain floor (ADR-0017).
class MoveOnlyTask {
    struct Base {
        Base() = default;
        Base(const Base &) = delete;
        Base &operator=(const Base &) = delete;
        Base(Base &&) = delete;
        Base &operator=(Base &&) = delete;
        virtual ~Base() = default;
        virtual void run() = 0;
    };

    template <typename F> struct Runner final : Base {
        explicit Runner(F function) : function_(std::move(function)) {}
        void run() override { function_(); }
        F function_;
    };

  public:
    MoveOnlyTask() = default;
    template <typename F>
        requires std::invocable<F &>
    explicit MoveOnlyTask(F function) : impl_(std::make_unique<Runner<F>>(std::move(function))) {}

    /// Runs the wrapped callable. The pool only invokes tasks it has assigned (never empty).
    void operator()() { impl_->run(); }

  private:
    std::unique_ptr<Base> impl_;
};

} // namespace detail

/// A fixed-size pool of worker threads draining a stable priority queue of tasks.
///
/// `submit(priority, callable)` (higher priority runs first; equal priorities run in
/// submission order) returns a `TaskFuture` that delivers the callable's return value or
/// rethrows what it threw — a throwing task never kills a worker. Callables are nullary;
/// capture arguments in the closure. `shutdown()` stops admissions and **drains**: every
/// task already queued still runs before the workers join; the destructor calls it, so
/// destroying a pool blocks until queued work finishes (a task that never returns hangs
/// teardown — task discipline is the caller's, ADR-0017).
///
/// @note Thread-safe: `submit`, `pending`, and `thread_count` may be called from any
/// thread, including from inside tasks. `shutdown` is idempotent but must not be invoked
/// concurrently from multiple threads. Non-copyable and non-movable.
class ThreadPool {
  public:
    /// Starts `thread_count` workers. @throws std::invalid_argument if it is zero.
    explicit ThreadPool(std::size_t thread_count) {
        if (thread_count == 0) {
            throw std::invalid_argument("ThreadPool: thread count must be positive");
        }
        workers_.reserve(thread_count);
        try {
            for (std::size_t i = 0; i < thread_count; ++i) {
                workers_.emplace_back([this] { worker_loop(); });
            }
        } catch (...) {
            shutdown(); // a worker failed to spawn: join the ones that did start, then rethrow
            throw;
        }
    }

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
    ThreadPool(ThreadPool &&) = delete;
    ThreadPool &operator=(ThreadPool &&) = delete;

    /// Drains and joins (see shutdown()).
    ~ThreadPool() { shutdown(); }

    /// Enqueues `task` at `priority` (higher runs first; ties run in submission order).
    /// @return the future delivering the task's return value or exception.
    /// @throws std::logic_error once the pool is shut down.
    template <typename F>
        requires std::invocable<F &>
    [[nodiscard]] TaskFuture<std::invoke_result_t<F &>> submit(int priority, F task) {
        using Result = std::invoke_result_t<F &>;
        static_assert(!std::is_reference_v<Result>, "ThreadPool: tasks must return a value or void, not a reference");

        TaskPromise<Result> promise;
        TaskFuture<Result> future = promise.get_future();
        detail::MoveOnlyTask erased{[task = std::move(task), promise = std::move(promise)]() mutable {
            try {
                if constexpr (std::is_void_v<Result>) {
                    task();
                    promise.set_value();
                } else {
                    promise.set_value(task());
                }
            } catch (...) {
                promise.set_exception(std::current_exception()); // a throwing task never kills the worker
            }
        }};
        {
            const std::scoped_lock lock{mutex_};
            if (stopping_) {
                throw std::logic_error("ThreadPool: submit after shutdown");
            }
            heap_.push_back(QueuedTask{.priority = priority, .sequence = next_sequence_++, .run = std::move(erased)});
            // Iterator form on purpose: libc++ at the Apple Clang 14 floor has no ranges
            // algorithms (same precedent as flat_set/flat_map).
            // NOLINTNEXTLINE(modernize-use-ranges)
            std::push_heap(heap_.begin(), heap_.end(), runs_later);
        }
        work_available_.notify_one();
        return future;
    }

    /// Enqueues `task` at the default priority 0.
    template <typename F>
        requires std::invocable<F &>
    [[nodiscard]] TaskFuture<std::invoke_result_t<F &>> submit(F task) {
        return submit(0, std::move(task));
    }

    /// Stops admissions, lets the workers finish every queued task, and joins them.
    /// Idempotent; must not be called concurrently from multiple threads.
    void shutdown() {
        {
            const std::scoped_lock lock{mutex_};
            stopping_ = true;
        }
        work_available_.notify_all();
        for (std::thread &worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    /// The number of worker threads.
    [[nodiscard]] std::size_t thread_count() const noexcept { return workers_.size(); }

    /// A snapshot of the tasks queued but not yet started (stale the moment it returns).
    [[nodiscard]] std::size_t pending() const {
        const std::scoped_lock lock{mutex_};
        return heap_.size();
    }

  private:
    struct QueuedTask {
        int priority;
        std::uint64_t sequence;
        detail::MoveOnlyTask run;
    };

    /// Heap "less": true when `left` runs later than `right` — lower priority first, then
    /// later submission. push_heap/pop_heap with this puts the next task to run on top.
    static bool runs_later(const QueuedTask &left, const QueuedTask &right) noexcept {
        if (left.priority != right.priority) {
            return left.priority < right.priority;
        }
        return left.sequence > right.sequence;
    }

    void worker_loop() {
        for (;;) {
            detail::MoveOnlyTask task;
            {
                std::unique_lock<std::mutex> lock{mutex_};
                work_available_.wait(lock, [this] { return stopping_ || !heap_.empty(); });
                if (heap_.empty()) {
                    return; // stopping and fully drained
                }
                // Iterator form on purpose (no ranges algorithms at the libc++ floor); pop_heap
                // moves the top task to the back, where a move-only task can be moved out.
                // NOLINTNEXTLINE(modernize-use-ranges)
                std::pop_heap(heap_.begin(), heap_.end(), runs_later);
                task = std::move(heap_.back().run);
                heap_.pop_back();
            }
            task(); // user code runs outside the lock; exceptions were caught at wrap time
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable work_available_;
    std::vector<QueuedTask> heap_; ///< binary heap via std::push_heap/pop_heap (ADR-0017)
    std::uint64_t next_sequence_ = 0;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_THREAD_POOL_HPP
