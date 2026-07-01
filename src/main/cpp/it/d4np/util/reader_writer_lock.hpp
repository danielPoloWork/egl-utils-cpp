// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// ReaderWriterLock (component #8): a writer-preferring reader/writer lock for read-mostly
// workloads. Unlike std::shared_mutex — whose scheduling is unspecified and, on glibc,
// reader-preferring (writers can starve) — the fairness policy here is explicit and identical
// on every platform: once a writer waits, new readers are held back until it has run. The
// interface satisfies the SharedTimedMutex named requirements, so std::unique_lock,
// std::scoped_lock, and std::shared_lock compose with it directly. See ADR-0014.
#ifndef IT_D4NP_UTIL_READER_WRITER_LOCK_HPP
#define IT_D4NP_UTIL_READER_WRITER_LOCK_HPP

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <utility>

namespace it::d4np::util {

/// A writer-preferring reader/writer lock: any number of readers hold it concurrently, a
/// writer holds it exclusively, and a waiting writer blocks new readers so a continuous
/// reader stream can never starve writers (the documented trade-off: a continuous *writer*
/// stream can starve readers — the right side of the trade for read-mostly access, ADR-0014).
///
/// Satisfies the **SharedTimedMutex** named requirements: use `std::scoped_lock` /
/// `std::unique_lock` for writers and `std::shared_lock` for readers. Implemented as a
/// Monitor Object with Guarded Suspension waits (one mutex, one condition variable per
/// side), so the policy is identical on every supported platform.
///
/// @note Thread-safe: every member may be called concurrently from any thread. Standard
/// SharedMutex preconditions apply: acquisition is not recursive, and a thread must only
/// release a mode it holds (unlock paths are non-throwing so RAII guards stay safe;
/// precondition violations are undefined, not diagnosed). Non-copyable and non-movable.
class ReaderWriterLock {
  public:
    ReaderWriterLock() = default;
    ReaderWriterLock(const ReaderWriterLock &) = delete;
    ReaderWriterLock &operator=(const ReaderWriterLock &) = delete;
    ReaderWriterLock(ReaderWriterLock &&) = delete;
    ReaderWriterLock &operator=(ReaderWriterLock &&) = delete;
    ~ReaderWriterLock() = default;

    // --- Exclusive (writer) side -------------------------------------------------------------

    /// Acquires exclusive ownership, blocking until no reader or writer holds the lock.
    /// While this call waits, new readers are held back (writer preference).
    void lock() {
        std::unique_lock<std::mutex> lock{mutex_};
        ++waiting_writers_;
        writer_turn_.wait(lock, [this] { return !writer_active_ && active_readers_ == 0; });
        --waiting_writers_;
        writer_active_ = true;
    }

    /// Acquires exclusive ownership if the lock is free right now; never blocks.
    /// @return true if exclusive ownership was taken.
    [[nodiscard]] bool try_lock() {
        const std::scoped_lock lock{mutex_};
        if (writer_active_ || active_readers_ > 0) {
            return false;
        }
        writer_active_ = true;
        return true;
    }

    /// Acquires exclusive ownership, blocking up to `timeout`.
    /// @return true if exclusive ownership was taken; false on timeout.
    template <typename Rep, typename Period>
    [[nodiscard]] bool try_lock_for(const std::chrono::duration<Rep, Period> &timeout) {
        return writer_wait([&](std::unique_lock<std::mutex> &lock) {
            return writer_turn_.wait_for(lock, timeout, [this] { return !writer_active_ && active_readers_ == 0; });
        });
    }

    /// Acquires exclusive ownership, blocking until `deadline` at the latest.
    /// @return true if exclusive ownership was taken; false once the deadline passes.
    template <typename Clock, typename Duration>
    [[nodiscard]] bool try_lock_until(const std::chrono::time_point<Clock, Duration> &deadline) {
        return writer_wait([&](std::unique_lock<std::mutex> &lock) {
            return writer_turn_.wait_until(lock, deadline, [this] { return !writer_active_ && active_readers_ == 0; });
        });
    }

    /// Releases exclusive ownership. A waiting writer is preferred; otherwise all held-back
    /// readers are admitted. @pre the calling thread holds exclusive ownership.
    void unlock() {
        bool writers_waiting = false;
        {
            const std::scoped_lock lock{mutex_};
            writer_active_ = false;
            writers_waiting = waiting_writers_ > 0;
        }
        if (writers_waiting) {
            writer_turn_.notify_one();
        } else {
            readers_allowed_.notify_all();
        }
    }

    // --- Shared (reader) side ----------------------------------------------------------------

    /// Acquires shared ownership, blocking while a writer holds the lock **or is waiting
    /// for it** (writer preference).
    void lock_shared() {
        std::unique_lock<std::mutex> lock{mutex_};
        readers_allowed_.wait(lock, [this] { return !writer_active_ && waiting_writers_ == 0; });
        ++active_readers_;
    }

    /// Acquires shared ownership if no writer holds or awaits the lock; never blocks.
    /// @return true if shared ownership was taken.
    [[nodiscard]] bool try_lock_shared() {
        const std::scoped_lock lock{mutex_};
        if (writer_active_ || waiting_writers_ > 0) {
            return false;
        }
        ++active_readers_;
        return true;
    }

    /// Acquires shared ownership, blocking up to `timeout`.
    /// @return true if shared ownership was taken; false on timeout.
    template <typename Rep, typename Period>
    [[nodiscard]] bool try_lock_shared_for(const std::chrono::duration<Rep, Period> &timeout) {
        std::unique_lock<std::mutex> lock{mutex_};
        if (!readers_allowed_.wait_for(lock, timeout, [this] { return !writer_active_ && waiting_writers_ == 0; })) {
            return false;
        }
        ++active_readers_;
        return true;
    }

    /// Acquires shared ownership, blocking until `deadline` at the latest.
    /// @return true if shared ownership was taken; false once the deadline passes.
    template <typename Clock, typename Duration>
    [[nodiscard]] bool try_lock_shared_until(const std::chrono::time_point<Clock, Duration> &deadline) {
        std::unique_lock<std::mutex> lock{mutex_};
        if (!readers_allowed_.wait_until(lock, deadline, [this] { return !writer_active_ && waiting_writers_ == 0; })) {
            return false;
        }
        ++active_readers_;
        return true;
    }

    /// Releases shared ownership; the last departing reader hands the lock to a waiting
    /// writer. @pre the calling thread holds shared ownership.
    void unlock_shared() {
        bool wake_writer = false;
        {
            const std::scoped_lock lock{mutex_};
            --active_readers_;
            wake_writer = active_readers_ == 0 && waiting_writers_ > 0;
        }
        if (wake_writer) {
            writer_turn_.notify_one();
        }
    }

  private:
    /// Shared engine of the timed writer acquisitions. A timed writer that gives up must
    /// admit the readers it was holding back — they were blocked on its account.
    template <typename Wait> bool writer_wait(Wait &&wait) {
        bool wake_readers = false;
        {
            std::unique_lock<std::mutex> lock{mutex_};
            ++waiting_writers_;
            const bool acquired = std::forward<Wait>(wait)(lock);
            --waiting_writers_;
            if (acquired) {
                writer_active_ = true;
                return true;
            }
            wake_readers = waiting_writers_ == 0 && !writer_active_;
        }
        if (wake_readers) {
            readers_allowed_.notify_all();
        }
        return false;
    }

    mutable std::mutex mutex_;
    std::condition_variable readers_allowed_; ///< readers wait here while a writer holds or awaits
    std::condition_variable writer_turn_;     ///< writers wait here for exclusivity
    std::size_t active_readers_ = 0;
    std::size_t waiting_writers_ = 0;
    bool writer_active_ = false;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_READER_WRITER_LOCK_HPP
