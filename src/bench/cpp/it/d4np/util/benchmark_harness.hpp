// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Minimal, dependency-free micro-benchmark harness for egl-util-cpp (roadmap 10.2, ADR-0027).
// It dogfoods the library's own Stopwatch for timing rather than pulling in a third-party
// benchmark framework: each scenario runs a warm-up rep, then `reps` timed reps of a body that
// performs `ops_per_rep` operations, and reports the per-operation median and p99 across reps.
// `do_not_optimize` keeps the optimizer from discarding the measured work.
#ifndef IT_D4NP_UTIL_BENCH_BENCHMARK_HARNESS_HPP
#define IT_D4NP_UTIL_BENCH_BENCHMARK_HARNESS_HPP

#include <it/d4np/util/stopwatch.hpp>

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <ios>
#include <iostream>
#include <string_view>
#include <vector>

namespace it::d4np::util::bench {

/// Forces `value` to be materialized in memory so the optimizer cannot elide the work that
/// produced it. Portable (no inline asm): the address is stored through a volatile sink.
inline void sink_pointer(const volatile void *pointer) noexcept {
    static const volatile void *volatile sink;
    sink = pointer;
}
template <class T> inline void do_not_optimize(const T &value) noexcept {
    sink_pointer(static_cast<const volatile void *>(&value));
}

/// Per-operation timing summary for one scenario.
struct Stats {
    double median_ns = 0.0; ///< median nanoseconds per operation across reps.
    double p99_ns = 0.0;    ///< 99th-percentile nanoseconds per operation across reps.
    double ops_per_sec = 0.0;
};

/// Runs `body(ops_per_rep)` once un-timed (warm-up), then `reps` times under a Stopwatch,
/// returning the per-operation median/p99 over the reps. `body` must perform exactly
/// `ops_per_rep` operations and consume its results via `do_not_optimize`.
template <class Body> Stats measure(std::size_t ops_per_rep, int reps, Body body) {
    body(ops_per_rep); // warm-up (not timed)
    std::vector<double> per_op;
    per_op.reserve(static_cast<std::size_t>(reps));
    for (int rep = 0; rep < reps; ++rep) {
        Stopwatch stopwatch = Stopwatch::start_new();
        body(ops_per_rep);
        const auto nanos = static_cast<double>(stopwatch.elapsed().count());
        per_op.push_back(nanos / static_cast<double>(ops_per_rep));
    }
    std::ranges::sort(per_op);
    Stats stats;
    stats.median_ns = per_op[per_op.size() / 2];
    stats.p99_ns = per_op[static_cast<std::size_t>(static_cast<double>(per_op.size() - 1) * 0.99)];
    stats.ops_per_sec = stats.median_ns > 0.0 ? 1e9 / stats.median_ns : 0.0;
    return stats;
}

/// Prints one result row: scenario, median ns/op, p99 ns/op, and throughput in M ops/s.
inline void report(std::string_view name, const Stats &stats) {
    std::cout << std::left << std::setw(34) << name << std::right << std::fixed << std::setprecision(2) << std::setw(12)
              << stats.median_ns << " ns" << std::setw(12) << stats.p99_ns << " ns" << std::setw(12)
              << (stats.ops_per_sec / 1e6) << " M/s\n";
}

/// Prints the fixed table header the `report` rows align under.
inline void report_header() {
    std::cout << std::left << std::setw(34) << "scenario" << std::right << std::setw(15) << "median" << std::setw(15)
              << "p99" << std::setw(15) << "throughput" << "\n"
              << std::string(79, '-') << "\n";
}

} // namespace it::d4np::util::bench

#endif // IT_D4NP_UTIL_BENCH_BENCHMARK_HARNESS_HPP
