// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Semaphore (component #9): a portable counting semaphore built as a Monitor Object over
// std::mutex + std::condition_variable — the standard veneer over the OS synchronization
// primitives on every supported platform. Chosen over std::counting_semaphore because the
// toolchain floor ships defective implementations of the latter (see ADR-0013). Header-only.
#ifndef IT_D4NP_UTIL_SEMAPHORE_HPP
#define IT_D4NP_UTIL_SEMAPHORE_HPP

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace it::d4np::util {

/// A portable counting semaphore: `acquire` takes one unit of the count, blocking while the
/// count is zero; `release` returns units and wakes waiters.
///
/// The API mirrors `std::counting_semaphore` (`acquire`, `try_acquire`, `try_acquire_for`,
/// `try_acquire_until`, `release`, `max`) and adds an advisory `available()` snapshot.
/// Implemented as a **Monitor Object** with **Guarded Suspension** — one internal mutex
/// serializes the count and a condition-variable predicate wait absorbs spurious wakeups
/// (ADR-0013). Misuse is diagnosed, not undefined: a negative initial count or release
/// update throws `std::invalid_argument`, a release that would overflow the count throws
/// `std::overflow_error`; no other member throws.
///
/// @note Thread-safe: every member may be called concurrently from any thread.
/// Non-copyable and non-movable.
class Semaphore {
  public:
    using count_type = std::ptrdiff_t;

    /// The largest count the semaphore can represent.
    [[nodiscard]] static constexpr count_type max() noexcept { return std::numeric_limits<count_type>::max(); }

    /// Constructs a semaphore holding `initial` units.
    /// @throws std::invalid_argument if `initial` is negative.
    explicit Semaphore(count_type initial) : count_(initial) {
        if (initial < 0) {
            throw std::invalid_argument("Semaphore: initial count must be non-negative");
        }
    }

    Semaphore(const Semaphore &) = delete;
    Semaphore &operator=(const Semaphore &) = delete;
    Semaphore(Semaphore &&) = delete;
    Semaphore &operator=(Semaphore &&) = delete;
    ~Semaphore() = default;

    /// Takes one unit, blocking until one is available.
    void acquire() {
        std::unique_lock<std::mutex> lock{mutex_};
        available_.wait(lock, [this] { return count_ > 0; });
        --count_;
    }

    /// Takes one unit if immediately available; never blocks.
    /// @return true if a unit was taken.
    [[nodiscard]] bool try_acquire() {
        const std::scoped_lock lock{mutex_};
        if (count_ == 0) {
            return false;
        }
        --count_;
        return true;
    }

    /// Takes one unit, blocking up to `timeout` for one to become available.
    /// @return true if a unit was taken; false on timeout.
    template <typename Rep, typename Period>
    [[nodiscard]] bool try_acquire_for(const std::chrono::duration<Rep, Period> &timeout) {
        std::unique_lock<std::mutex> lock{mutex_};
        if (!available_.wait_for(lock, timeout, [this] { return count_ > 0; })) {
            return false;
        }
        --count_;
        return true;
    }

    /// Takes one unit, blocking until `deadline` at the latest.
    /// @return true if a unit was taken; false once the deadline passes.
    template <typename Clock, typename Duration>
    [[nodiscard]] bool try_acquire_until(const std::chrono::time_point<Clock, Duration> &deadline) {
        std::unique_lock<std::mutex> lock{mutex_};
        if (!available_.wait_until(lock, deadline, [this] { return count_ > 0; })) {
            return false;
        }
        --count_;
        return true;
    }

    /// Returns `update` units to the semaphore and wakes waiters. `release(0)` is a no-op.
    /// @throws std::invalid_argument if `update` is negative.
    /// @throws std::overflow_error if the count would exceed `max()`.
    void release(count_type update = 1) {
        if (update < 0) {
            throw std::invalid_argument("Semaphore: release update must be non-negative");
        }
        if (update == 0) {
            return;
        }
        {
            const std::scoped_lock lock{mutex_};
            if (count_ > max() - update) {
                throw std::overflow_error("Semaphore: count would exceed max()");
            }
            count_ += update;
        }
        if (update == 1) {
            available_.notify_one();
        } else {
            available_.notify_all();
        }
    }

    /// An instantaneous snapshot of the count — stale the moment it returns. Meant for
    /// diagnostics and tests, never for synchronization decisions.
    [[nodiscard]] count_type available() const {
        const std::scoped_lock lock{mutex_};
        return count_;
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable available_;
    count_type count_;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_SEMAPHORE_HPP
