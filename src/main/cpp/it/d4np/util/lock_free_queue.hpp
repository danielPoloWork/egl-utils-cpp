// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// LockFreeQueue (component #7): a bounded MPMC queue — Vyukov's per-slot sequence design.
// Producers and consumers claim positions with a relaxed CAS on their ticket counter; each
// slot's atomic sequence number (acquire/release) is the handshake that publishes the payload
// and the vacancy. The memory-ordering argument lives in ADR-0016 and is normative: keep the
// table there and this code in lockstep. Header-only.
#ifndef IT_D4NP_UTIL_LOCK_FREE_QUEUE_HPP
#define IT_D4NP_UTIL_LOCK_FREE_QUEUE_HPP

#include <atomic>
#include <bit>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace it::d4np::util {

/// A bounded multi-producer/multi-consumer queue over a pre-sized ring of slots.
///
/// Non-blocking value-or-error API in the `CircularBuffer` vocabulary: `try_push` returns
/// `false` when the queue is full (backpressure — normal flow control, not an error) and
/// `try_pop` returns an empty optional when it is empty. Capacity is fixed at construction,
/// rounded up to the next power of two (observable via `capacity()`). FIFO is guaranteed
/// per producer; the global order under contention is decided by ticket interleaving, the
/// standard MPMC contract. Undelivered elements are destroyed with the queue.
///
/// @note Thread-safe: any number of concurrent pushers and poppers. Progress caveat
/// (ADR-0016): the algorithm is *lockless* — no thread ever blocks in a kernel primitive
/// and the fast path is a handful of atomics — but not formally lock-free: a thread
/// suspended between claiming its ticket and releasing the slot sequence stalls that one
/// slot's counterpart. Non-copyable and non-movable.
template <typename T> class LockFreeQueue {
    static_assert(std::is_nothrow_move_constructible_v<T>,
                  "LockFreeQueue<T> requires a nothrow-move-constructible T: a throwing move inside a claimed "
                  "slot would strand the slot's sequence and wedge the ring");

  public:
    /// Creates a queue holding at least `min_capacity` elements (rounded up to a power of
    /// two). @throws std::invalid_argument for zero or for capacities whose power-of-two
    /// rounding is not representable.
    explicit LockFreeQueue(std::size_t min_capacity)
        : slots_(rounded_capacity(min_capacity)), mask_(slots_.size() - 1) {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    LockFreeQueue(const LockFreeQueue &) = delete;
    LockFreeQueue &operator=(const LockFreeQueue &) = delete;
    LockFreeQueue(LockFreeQueue &&) = delete;
    LockFreeQueue &operator=(LockFreeQueue &&) = delete;
    ~LockFreeQueue() = default; // slots destroy any undelivered elements

    /// The actual (power-of-two) capacity.
    [[nodiscard]] std::size_t capacity() const noexcept { return mask_ + 1; }

    /// Enqueues `value` if a slot is free right now; never blocks.
    /// @return true on success; false when the queue is full (backpressure).
    [[nodiscard]] bool try_push(T value) {
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Slot &slot = slots_[pos & mask_];
            const std::size_t seq = slot.sequence.load(std::memory_order_acquire); // sync with consumer's release
            const auto gap = static_cast<std::ptrdiff_t>(seq - pos);
            if (gap == 0) { // the slot is vacant for exactly this ticket
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    slot.value.emplace(std::move(value));
                    slot.sequence.store(pos + 1, std::memory_order_release); // publish the payload
                    return true;
                }
                // CAS lost: `pos` was refreshed with the current ticket; retry with it.
            } else if (gap < 0) {
                return false; // the slot still holds an unconsumed element from the previous lap: full
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed); // another producer advanced; catch up
            }
        }
    }

    /// Dequeues the oldest element if one is available right now; never blocks.
    /// @return the element, or an empty optional when the queue is empty.
    [[nodiscard]] std::optional<T> try_pop() {
        std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Slot &slot = slots_[pos & mask_];
            const std::size_t seq = slot.sequence.load(std::memory_order_acquire); // sync with producer's release
            const auto gap = static_cast<std::ptrdiff_t>(seq - (pos + 1));
            if (gap == 0) { // the slot holds the element for exactly this ticket
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    std::optional<T> result{std::move(slot.value)};
                    slot.value.reset(); // moving the optional leaves it engaged; destroy the husk

                    slot.sequence.store(pos + mask_ + 1, std::memory_order_release); // publish the vacancy (next lap)
                    return result;
                }
                // CAS lost: `pos` was refreshed with the current ticket; retry with it.
            } else if (gap < 0) {
                return std::nullopt; // no producer has filled this slot yet: empty
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed); // another consumer advanced; catch up
            }
        }
    }

  private:
    struct Slot {
        std::atomic<std::size_t> sequence{0};
        std::optional<T> value; // touched only by the one thread whose ticket owns the slot
    };

    static constexpr std::size_t cache_line_size = 64;

    static std::size_t rounded_capacity(std::size_t min_capacity) {
        constexpr std::size_t largest = std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1);
        if (min_capacity == 0 || min_capacity > largest) {
            throw std::invalid_argument("LockFreeQueue: capacity must be positive and representable");
        }
        return std::bit_ceil(min_capacity);
    }

    std::vector<Slot> slots_;
    std::size_t mask_ = 0;
    /// The ticket counters are the two guaranteed contention hot spots; keep them on
    /// separate cache lines (slots are deliberately dense — ADR-0016).
    alignas(cache_line_size) std::atomic<std::size_t> enqueue_pos_{0};
    alignas(cache_line_size) std::atomic<std::size_t> dequeue_pos_{0};
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_LOCK_FREE_QUEUE_HPP
