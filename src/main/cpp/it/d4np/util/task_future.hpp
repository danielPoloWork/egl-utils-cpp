// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// TaskFuture / TaskPromise (component #6): a lightweight, move-only, single-shot
// promise/future pair for cooperative tasks. One shared-state allocation, a monitor
// (mutex + condvar) for the handoff, and a single failure surface: everything a task can do
// wrong arrives out of get() as an exception — the task's own (via set_exception) or
// BrokenTaskPromise if the promise died unsatisfied. No std::async coupling, no
// shared_future, no allocators, no continuations (deferred until ThreadPool exists to run
// them). See ADR-0015.
#ifndef IT_D4NP_UTIL_TASK_FUTURE_HPP
#define IT_D4NP_UTIL_TASK_FUTURE_HPP

#include <chrono>
#include <concepts>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace it::d4np::util {

/// Thrown by `TaskFuture::get` when the corresponding `TaskPromise` was destroyed (or
/// move-assigned over) before delivering a value or an exception.
class BrokenTaskPromise : public std::runtime_error {
  public:
    BrokenTaskPromise() : std::runtime_error("TaskPromise abandoned before satisfying its TaskFuture") {}
};

namespace detail {

/// `void` has no value to store; an empty tag stands in for it inside the shared state.
struct VoidValue {};

template <typename T> using FutureValue = std::conditional_t<std::is_void_v<T>, VoidValue, T>;

/// The monitor-protected rendezvous between one TaskPromise and one TaskFuture. The variant
/// alternatives, by index: 0 = still empty, 1 = value, 2 = exception. Abandonment is a plain
/// bool beside the variant (instead of an eagerly allocated exception_ptr or a variant
/// alternative) so the promise's abandonment path is provably nothrow — libstdc++'s
/// variant::emplace has a bad_variant_access throw path on its return that trips
/// bugprone-exception-escape inside noexcept functions. The BrokenTaskPromise itself is
/// constructed in the getter's thread.
template <typename T> class SharedTaskState {
  public:
    template <typename... Args> void set_value(Args &&...args) {
        {
            const std::scoped_lock lock{mutex_};
            ensure_unset();
            result_.template emplace<1>(std::forward<Args>(args)...);
        }
        ready_.notify_all();
    }

    void set_exception(std::exception_ptr error) {
        {
            const std::scoped_lock lock{mutex_};
            ensure_unset();
            result_.template emplace<2>(std::move(error));
        }
        ready_.notify_all();
    }

    /// Called when the owning promise dies: an unsatisfied state becomes abandoned; a
    /// satisfied one is left untouched. Never throws (the flag flip cannot fail).
    void abandon() noexcept {
        bool became_abandoned = false;
        {
            const std::scoped_lock lock{mutex_};
            if (result_.index() == 0 && !abandoned_) {
                abandoned_ = true;
                became_abandoned = true;
            }
        }
        if (became_abandoned) {
            ready_.notify_all();
        }
    }

    [[nodiscard]] bool ready() const {
        const std::scoped_lock lock{mutex_};
        return has_outcome();
    }

    void wait() const {
        std::unique_lock<std::mutex> lock{mutex_};
        ready_.wait(lock, [this] { return has_outcome(); });
    }

    template <typename Rep, typename Period>
    [[nodiscard]] bool wait_for(const std::chrono::duration<Rep, Period> &timeout) const {
        std::unique_lock<std::mutex> lock{mutex_};
        return ready_.wait_for(lock, timeout, [this] { return has_outcome(); });
    }

    template <typename Clock, typename Duration>
    [[nodiscard]] bool wait_until(const std::chrono::time_point<Clock, Duration> &deadline) const {
        std::unique_lock<std::mutex> lock{mutex_};
        return ready_.wait_until(lock, deadline, [this] { return has_outcome(); });
    }

    /// Blocks for the result, then delivers it: rethrows a stored exception, throws
    /// BrokenTaskPromise for an abandoned state, or moves the value out.
    T get() {
        std::unique_lock<std::mutex> lock{mutex_};
        ready_.wait(lock, [this] { return has_outcome(); });
        if (result_.index() == 2) {
            std::rethrow_exception(std::get<2>(result_));
        }
        if (abandoned_) {
            throw BrokenTaskPromise{};
        }
        if constexpr (std::is_void_v<T>) {
            return;
        } else {
            return std::move(std::get<1>(result_));
        }
    }

  private:
    [[nodiscard]] bool has_outcome() const { // requires mutex_ held
        return result_.index() != 0 || abandoned_;
    }

    void ensure_unset() const { // requires mutex_ held
        if (has_outcome()) {
            throw std::logic_error("TaskPromise: result already set");
        }
    }

    mutable std::mutex mutex_;
    mutable std::condition_variable ready_;
    std::variant<std::monostate, FutureValue<T>, std::exception_ptr> result_;
    bool abandoned_ = false;
};

} // namespace detail

template <typename T> class TaskPromise;

/// The consuming end of a lightweight, single-shot task handoff (component #6, ADR-0015).
///
/// Obtained from `TaskPromise::get_future`. Move-only. `get()` blocks for the result,
/// delivers it exactly once (moving the value out — move-only payloads flow through), and
/// invalidates the future; a stored exception or an abandoned promise surfaces from `get()`
/// as the task's exception or `BrokenTaskPromise` respectively. `wait_for`/`wait_until`
/// return plain readiness (there is no deferred state). Operations other than `valid()` on
/// an invalid future throw `std::logic_error`.
///
/// @note The promise/future pair may live on different threads (the shared state is the
/// synchronized boundary), but a single `TaskFuture` object belongs to one thread at a time.
template <typename T> class TaskFuture {
  public:
    /// Constructs an invalid future (`valid() == false`); real ones come from TaskPromise.
    TaskFuture() noexcept = default;
    TaskFuture(const TaskFuture &) = delete;
    TaskFuture &operator=(const TaskFuture &) = delete;
    TaskFuture(TaskFuture &&) noexcept = default;
    TaskFuture &operator=(TaskFuture &&) noexcept = default;
    ~TaskFuture() = default;

    /// Whether this future refers to a shared state (it has not been moved from or consumed).
    [[nodiscard]] bool valid() const noexcept { return state_ != nullptr; }

    /// Whether the result is already available (a `get()` would not block).
    [[nodiscard]] bool ready() const { return checked_state().ready(); }

    /// Blocks until the result is available.
    void wait() const { checked_state().wait(); }

    /// Blocks up to `timeout`. @return true once the result is available.
    template <typename Rep, typename Period>
    [[nodiscard]] bool wait_for(const std::chrono::duration<Rep, Period> &timeout) const {
        return checked_state().wait_for(timeout);
    }

    /// Blocks until `deadline` at the latest. @return true once the result is available.
    template <typename Clock, typename Duration>
    [[nodiscard]] bool wait_until(const std::chrono::time_point<Clock, Duration> &deadline) const {
        return checked_state().wait_until(deadline);
    }

    /// Blocks for the result and delivers it exactly once, invalidating the future: returns
    /// the value (moved out), rethrows the task's exception, or throws `BrokenTaskPromise`.
    T get() {
        const std::shared_ptr<detail::SharedTaskState<T>> state = std::move(state_);
        if (!state) {
            throw std::logic_error("TaskFuture: no shared state");
        }
        return state->get();
    }

  private:
    friend class TaskPromise<T>;
    explicit TaskFuture(std::shared_ptr<detail::SharedTaskState<T>> state) noexcept : state_(std::move(state)) {}

    [[nodiscard]] detail::SharedTaskState<T> &checked_state() const {
        if (!state_) {
            throw std::logic_error("TaskFuture: no shared state");
        }
        return *state_;
    }

    std::shared_ptr<detail::SharedTaskState<T>> state_;
};

/// The producing end of the task handoff: delivers exactly one value or exception to the
/// `TaskFuture` obtained from `get_future()` (component #6, ADR-0015).
///
/// Move-only. Misuse throws `std::logic_error` (double set, second `get_future`, operations
/// after being moved from). Destroying — or move-assigning over — a promise that never
/// delivered marks the state abandoned, and the future's `get()` throws `BrokenTaskPromise`.
///
/// @note Same threading contract as TaskFuture: the pair spans threads, each end belongs to
/// one thread at a time.
template <typename T> class TaskPromise {
  public:
    TaskPromise() : state_(std::make_shared<detail::SharedTaskState<T>>()) {}
    TaskPromise(const TaskPromise &) = delete;
    TaskPromise &operator=(const TaskPromise &) = delete;
    TaskPromise(TaskPromise &&) noexcept = default;

    /// Abandons the current state (if unsatisfied) before taking over `other`'s.
    TaskPromise &operator=(TaskPromise &&other) noexcept {
        if (this != &other) {
            abandon_if_owned();
            state_ = std::move(other.state_);
            future_retrieved_ = other.future_retrieved_;
        }
        return *this;
    }

    ~TaskPromise() { abandon_if_owned(); }

    /// Returns the one future paired with this promise.
    /// @throws std::logic_error on a second call, or if the promise was moved from.
    [[nodiscard]] TaskFuture<T> get_future() {
        checked_state();
        if (future_retrieved_) {
            throw std::logic_error("TaskPromise: future already retrieved");
        }
        future_retrieved_ = true;
        return TaskFuture<T>{state_};
    }

    /// Delivers the value and wakes the future (non-void payloads).
    /// @throws std::logic_error if a result was already delivered.
    template <typename U = T>
        requires(!std::is_void_v<T> && std::convertible_to<U &&, T>)
    void set_value(U &&value) {
        checked_state().set_value(std::forward<U>(value));
    }

    /// Delivers completion and wakes the future (void payload).
    /// @throws std::logic_error if a result was already delivered.
    void set_value()
        requires std::is_void_v<T>
    {
        checked_state().set_value();
    }

    /// Delivers `error`; the future's `get()` rethrows it.
    /// @throws std::invalid_argument if `error` is null.
    /// @throws std::logic_error if a result was already delivered.
    void set_exception(const std::exception_ptr &error) {
        if (!error) {
            throw std::invalid_argument("TaskPromise: null exception_ptr");
        }
        checked_state().set_exception(error);
    }

  private:
    void abandon_if_owned() noexcept {
        if (state_) {
            state_->abandon();
        }
    }

    detail::SharedTaskState<T> &checked_state() {
        if (!state_) {
            throw std::logic_error("TaskPromise: no shared state (moved from)");
        }
        return *state_;
    }

    std::shared_ptr<detail::SharedTaskState<T>> state_;
    bool future_retrieved_ = false;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_TASK_FUTURE_HPP
