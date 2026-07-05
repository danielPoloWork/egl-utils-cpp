// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Reproducible micro-benchmark suite for egl-util-cpp (roadmap 10.2, ADR-0027). Each scenario
// exercises one hot path of a header-only, performance-critical component through the Stopwatch
// harness and prints a per-operation median/p99. Baselines captured from a release build are
// published under docs/benchmarks/. The CI `benchmark` job builds this target (micro-benchmark
// timings on shared runners are not reproducible enough to gate on); numbers are measured on a
// recorded reference machine.
#include "benchmark_harness.hpp"

#include <it/d4np/util/binary_serializer.hpp>
#include <it/d4np/util/circular_buffer.hpp>
#include <it/d4np/util/flat_map.hpp>
#include <it/d4np/util/hash.hpp>
#include <it/d4np/util/lock_free_queue.hpp>
#include <it/d4np/util/object_pool.hpp>
#include <it/d4np/util/stack_allocator.hpp>
#include <it/d4np/util/string_builder.hpp>
#include <it/d4np/util/version.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace it::d4np::util;
using it::d4np::util::bench::do_not_optimize;
using it::d4np::util::bench::measure;
using it::d4np::util::bench::report;

constexpr int k_reps = 64;

void bench_fnv1a() {
    // Slide a 64-byte window over a larger blob so each hash sees distinct input (no hoisting).
    std::string blob(4096, 'x');
    for (std::size_t i = 0; i < blob.size(); ++i) {
        blob[i] = static_cast<char>('a' + (i % 26));
    }
    constexpr std::size_t window = 64;
    const std::size_t slack = blob.size() - window;
    const auto stats = measure(1U << 16U, k_reps, [&](std::size_t ops) {
        std::uint64_t acc = 0;
        for (std::size_t i = 0; i < ops; ++i) {
            acc ^= fnv1a_64(std::string_view{blob}.substr(i % slack, window));
        }
        do_not_optimize(acc);
    });
    report("hash::fnv1a_64 (64B window)", stats);
}

void bench_flat_map_lookup() {
    constexpr int entries = 1024;
    FlatMap<int, int> map;
    std::vector<int> keys;
    keys.reserve(entries);
    for (int i = 0; i < entries; ++i) {
        static_cast<void>(map.insert(i * 2, i));
        keys.push_back(i * 2);
    }
    const auto stats = measure(1U << 16U, k_reps, [&](std::size_t ops) {
        std::size_t hits = 0;
        for (std::size_t i = 0; i < ops; ++i) {
            hits += map.contains(keys[i % static_cast<std::size_t>(entries)]) ? 1U : 0U;
        }
        do_not_optimize(hits);
    });
    report("FlatMap<int,int>::contains (1024)", stats);
}

void bench_object_pool() {
    ObjectPool<int> pool(256);
    const auto stats = measure(1U << 16U, k_reps, [&](std::size_t ops) {
        for (std::size_t i = 0; i < ops; ++i) {
            auto handle = pool.try_acquire(); // released at end of iteration (RAII)
            do_not_optimize(handle);
        }
    });
    report("ObjectPool<int> acquire+release", stats);
}

void bench_lock_free_queue() {
    LockFreeQueue<int> queue(1024);
    const auto stats = measure(1U << 16U, k_reps, [&](std::size_t ops) {
        for (std::size_t i = 0; i < ops; ++i) {
            static_cast<void>(queue.try_push(static_cast<int>(i)));
            auto value = queue.try_pop();
            do_not_optimize(value);
        }
    });
    report("LockFreeQueue<int> push+pop (1t)", stats);
}

void bench_circular_buffer() {
    CircularBuffer<int> buffer(1024);
    const auto stats = measure(1U << 16U, k_reps, [&](std::size_t ops) {
        for (std::size_t i = 0; i < ops; ++i) {
            static_cast<void>(buffer.try_push(static_cast<int>(i)));
            auto value = buffer.pop();
            do_not_optimize(value);
        }
    });
    report("CircularBuffer<int> push+pop", stats);
}

void bench_stack_allocator() {
    // Stack-backed, so keep it modest (reset() before each allocation means 64 B is enough).
    StackAllocator<1U << 12U> allocator;
    const auto stats = measure(1U << 16U, k_reps, [&](std::size_t ops) {
        for (std::size_t i = 0; i < ops; ++i) {
            allocator.reset();
            void *block = allocator.allocate(64);
            do_not_optimize(block);
        }
    });
    report("StackAllocator reset+allocate(64)", stats);
}

void bench_string_builder() {
    const auto stats = measure(1U << 14U, k_reps, [&](std::size_t ops) {
        StringBuilder builder{ops * 4};
        for (std::size_t i = 0; i < ops; ++i) {
            static_cast<void>(builder.append(std::string_view{"abcd"}));
        }
        do_not_optimize(builder.size());
    });
    report("StringBuilder::append(4B)", stats);
}

void bench_binary_serializer() {
    std::array<std::byte, 32> buf{};
    const auto stats = measure(1U << 16U, k_reps, [&](std::size_t ops) {
        std::uint64_t acc = 0;
        for (std::size_t i = 0; i < ops; ++i) {
            BinarySerializer out{buf};
            static_cast<void>(out.write<std::uint64_t>(static_cast<std::uint64_t>(i)));
            static_cast<void>(out.write<double>(1.5));
            BinaryDeserializer in{buf};
            acc ^= in.read<std::uint64_t>().value_or(0);
            do_not_optimize(in.read<double>());
        }
        do_not_optimize(acc);
    });
    report("BinarySerializer u64+f64 round-trip", stats);
}

} // namespace

// The try/catch reports any benchmark failure best-effort to stderr; a throw from the stderr
// write itself is not worth guarding against in a dev-only benchmark binary.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    try {
        std::cout << "egl-util-cpp benchmark suite v" << version_string << " (release, single-threaded)\n\n";
        it::d4np::util::bench::report_header();
        bench_fnv1a();
        bench_flat_map_lookup();
        bench_object_pool();
        bench_lock_free_queue();
        bench_circular_buffer();
        bench_stack_allocator();
        bench_string_builder();
        bench_binary_serializer();
    } catch (const std::exception &error) {
        std::cerr << "benchmark failed: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "benchmark failed: unknown error\n";
        return 1;
    }
    return 0;
}
