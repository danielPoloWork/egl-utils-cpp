// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Milestone 1 benchmark stub: keeps the `bench` preset and the CI benchmark job
// buildable. The reproducible benchmark suite (per-component baselines under
// docs/benchmarks/) is built out in roadmap M10.
#include <it/d4np/util/util.hpp>

#include <chrono>
#include <iostream>

int main() {
    const auto start = std::chrono::steady_clock::now();
    const int sink = it::d4np::util::version_major;
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

    std::cout << "egl-util-cpp benchmark stub: " << it::d4np::util::version_string << " (warm-up " << nanos
              << " ns, sink=" << sink << ")\n";
    return 0;
}
