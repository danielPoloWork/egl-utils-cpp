// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Tests for StackTrace (component #21, roadmap 7.2). Compiled only when the STATIC tier is
// enabled (like library_version_test). Assertions are structural — frame counts, caps, skip
// behavior, formatting shape — never specific symbol names: symbolization is best-effort by
// contract (ADR-0019) and release/stripped CI cells must pass identically.
#include <doctest/doctest.h>

#include <it/d4np/util/stack_trace.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace {

using it::d4np::util::StackTrace;

} // namespace

TEST_CASE("capture collects a non-empty stack of non-null addresses") {
    const StackTrace trace = StackTrace::capture();
    CHECK_FALSE(trace.empty());
    CHECK(trace.size() >= 1); // at least this test function's frame
    CHECK(trace.size() <= StackTrace::default_max_frames);
    CHECK(trace.addresses().size() == trace.size());
    for (const void *address : trace.addresses()) {
        CHECK(address != nullptr);
    }
}

TEST_CASE("max_frames caps the walk and zero frames is an empty trace") {
    const StackTrace capped = StackTrace::capture(0, 4);
    CHECK_FALSE(capped.empty());
    CHECK(capped.size() <= 4);

    const StackTrace none = StackTrace::capture(0, 0);
    CHECK(none.empty());
    CHECK(none.to_string().empty());
}

TEST_CASE("skip drops top frames") {
    // Same call site, so the deeper capture bounds the skipped one.
    const StackTrace full = StackTrace::capture(0);
    const StackTrace skipped = StackTrace::capture(2);
    CHECK(skipped.size() <= full.size());
    CHECK_FALSE(full.empty());
}

TEST_CASE("symbolize resolves every captured frame to a description") {
    const StackTrace trace = StackTrace::capture(0, 8);
    const std::vector<StackTrace::Frame> frames = trace.symbolize();
    REQUIRE(frames.size() == trace.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        CHECK(frames[i].address == trace.addresses()[i]);
        CHECK_FALSE(frames[i].description.empty()); // at minimum the hex address
        CHECK(frames[i].description.find("0x") == 0);
    }
}

TEST_CASE("to_string renders one indexed line per frame") {
    const StackTrace trace = StackTrace::capture(0, 6);
    const std::string rendered = trace.to_string();
    CHECK_FALSE(rendered.empty());
    CHECK(rendered.find("#0 0x") == 0); // first line starts with the first frame index

    std::size_t lines = 0;
    for (const char ch : rendered) {
        if (ch == '\n') {
            ++lines;
        }
    }
    CHECK(lines == trace.size()); // every frame ends its own line
}

TEST_CASE("traces are plain values: copies are independent") {
    const StackTrace original = StackTrace::capture(0, 8);
    const StackTrace copy = original; // NOLINT(performance-unnecessary-copy-initialization) — the copy is the point
    CHECK(copy.size() == original.size());
    CHECK(copy.addresses() == original.addresses());
}
