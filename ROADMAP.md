# Roadmap — egl-util-cpp

The project's plan as a numbered, checkbox-driven list. When an item completes in a PR,
flip its checkbox (`- [ ]` → `- [x]`) **in the same PR**. New work goes at the bottom of
its section with a fresh `<milestone>.<task>` number; never renumber.

- **Versioning start:** pre-1.0 milestone-driven.
- **Session journal:** see [`docs/journal/`](docs/journal/). Latest checkpoint:
  [2026-07-05 — Release v1.0.0](docs/journal/2026/07/2026-07-05-release-v1.0.0.md).

---

## Milestone 1 — Project bootstrap & CI

The thinnest slice that compiles, tests, and ships under the full quality bar.

- [x] 1.1 Lay down the build system (CMake (>=3.20) + Ninja, CMakePresets.json) and a buildable skeleton under
      `src/main/cpp/it/d4np/util/`.
- [x] 1.2 Wire the test framework (doctest (FetchContent, test scope only)) with one passing smoke test under
      `src/test/cpp/it/d4np/util/`.
- [x] 1.3 Add formatter + linter configs (clang-format (LLVM-derived, 4-space, 120 col), clang-tidy (bugprone/cert/cppcoreguidelines/modernize/performance/portability/readability)) at the repo root.
- [x] 1.4 Stand up the CI matrix (Linux x86_64 (GCC>=11, Clang>=14), Windows x86_64 (MSVC>=19.30), macOS arm64 (Apple Clang>=14)) with build + test + format + lint.
- [x] 1.5 Seed the version constant (D4NP_UTIL_VERSION_{MAJOR,MINOR,PATCH,STRING}) in `src/main/cpp/it/d4np/util/version.hpp`.
- [x] 1.6 Create the aggregate umbrella header util.hpp that includes every module header.
- [x] 1.7 Add the Doxygen configuration and a docs build target.
- [x] 1.8 Establish the hybrid CMake model: an INTERFACE target for header-only use plus an optional STATIC target gating the compiled translation units.


---

## Milestone 2 — Foundations — Type Traits & Hashing

The compile-time building blocks every other module depends on.

- [x] 2.1 Implement it::d4np::util::TypeTraits — constexpr type-property utilities for custom types (component #25).
- [x] 2.2 Implement it::d4np::util::HashAlgorithms — FNV-1a, MurmurHash3, and SHA-256 in constexpr form, validated with static_assert (component #24).


---

## Milestone 3 — Memory & Resource Management

RAII-driven, leak-free allocation primitives, validated under ASan/Valgrind.

- [x] 3.1 Implement UniqueRef<T> — non-null unique-ownership pointer (component #2).
- [x] 3.2 Implement HeapArray<T> — fixed-size, bounds-checked heap array (component #4).
- [x] 3.3 Implement StackAllocator<Size> — stack-backed bump allocator (component #3).
- [x] 3.4 Implement ObjectPool<T> — thread-safe O(1) object pool (component #1).


---

## Milestone 4 — Contiguous Containers

Cache-friendly fixed and contiguous containers.

- [x] 4.1 Implement FlatSet<T> — sorted-vector set (component #15).
- [x] 4.2 Implement FlatMap<Key,Value> — sorted contiguous-array map with constexpr support (component #14).
- [x] 4.3 Implement CircularBuffer<T> — fixed circular buffer for byte streaming (component #13).


---

## Milestone 5 — Zero-Copy Strings

Allocation-light string construction, splitting, and type-safe formatting.

- [x] 5.1 Implement StringSplitter — string_view-based, zero-allocation splitter (component #11).
- [x] 5.2 Implement StringBuilder — preallocated fluent builder (component #10).
- [x] 5.3 Implement StringFormatter — compile-time type-safe formatter (component #12).


---

## Milestone 6 — Concurrency & Multithreading

Thread-safe primitives, all verified under ThreadSanitizer.

- [x] 6.1 Implement Semaphore — portable counting semaphore (component #9).
- [x] 6.2 Implement ReaderWriterLock — read-optimized shared mutex (component #8).
- [x] 6.3 Implement TaskFuture<T> — lightweight future/promise (component #6).
- [x] 6.4 Implement LockFreeQueue<T> — MPMC lock-free queue (component #7).
- [x] 6.5 Implement ThreadPool — priority-queue work pool (component #5).


---

## Milestone 7 — Diagnostics & Instrumentation

Profiling, logging, and crash diagnostics.

- [x] 7.1 Implement Stopwatch — microsecond high-resolution profiler (component #19).
- [x] 7.2 Implement StackTrace — native-API stack capture (component #21).
- [x] 7.3 Implement Logger — asynchronous multi-sink logger: console, file, UDP (component #20).


---

## Milestone 8 — Parsing & Input

Typed CLI parsing and non-allocating JSON.

- [x] 8.1 Implement CliParser — typed CLI parser with auto-generated help (component #22).
- [x] 8.2 Implement JsonParser — non-allocating JSON parser over string views (component #23).


---

## Milestone 9 — I/O & Networking

RAII file I/O, async networking, and binary serialization (the OS-API-heavy compiled tier).

- [x] 9.1 Implement FileStream — RAII buffered file wrapper (component #16).
- [x] 9.2 Implement BinarySerializer — endianness-aware binary serializer (component #18).
- [x] 9.3 Implement TcpSocket / TcpServer — non-blocking async sockets over select/poll/epoll (component #17).


---

## Milestone 10 — Hardening & 1.0

API freeze, full documentation, and the first stable release.

- [x] 10.1 Freeze the public API and document every public type with Doxygen.
- [x] 10.2 Complete the benchmark suite and publish baseline results under docs/benchmarks/.
- [x] 10.3 Achieve >=80% line coverage across all modules.
- [x] 10.4 Tag and release v1.0.0 under SemVer.


---

## Milestone 11 — Specification & Assurance Hardening

Close the gaps surfaced by the post-1.0 specification review: consolidate the error model,
make the architecture and per-module contracts explicit, quantify performance targets, and
document the security posture. Documentation- and assurance-focused; no public API breaks
(the one spec/impl drift in 11.7 is reconciled in favor of the shipped API).

- [x] 11.1 Record the library-wide error-handling policy (value-or-error vs exceptions,
      why not `std::expected` at the C++20 floor) as an ADR, backing spec §5.
- [x] 11.2 Add a C4 component diagram grouping the 25 modules into their seven layers with
      explicit dependency directions.
- [ ] 11.3 Add a per-module contract table for all 25 components: thread-safety guarantee,
      exception-safety level, allocation behavior, and algorithmic complexity.
- [ ] 11.4 State a single distribution model with an explicit ABI-stability policy for the
      compiled tier (augment ADR-0004 / spec §1); remove any header-only vs compiled ambiguity.
- [ ] 11.5 Add a security section: threat model for the untrusted-input components
      (BinarySerializer, CliParser, JsonParser), fuzzing harnesses (libFuzzer targets), and a
      SHA-256 non-cryptographic scoping statement with extended NIST test-vector validation.
- [ ] 11.6 Turn the benchmark baselines into documented numeric targets/regression thresholds
      with a stated methodology (warm-up, reps, p99) on the existing Stopwatch harness.
- [ ] 11.7 Fix the spec §3 example so it compiles and actually demonstrates constexpr `FlatMap`
      construction; reconcile the §5 `find` wording with the shipped iterator-returning API.


---

## Spec Coverage Map

Tracks which spec section is fulfilled by which roadmap item(s). Every spec section has a
row with at least one fulfilling item and a status glyph. Legend: ⏳ not started · 🚧 in
progress · ✅ done · ❎ N/A.

| Spec § | Requirement | Roadmap items | Status |
|--------|-------------|---------------|--------|
| §1 | Objective & business context | 1.1, 10.4, 11.4 | ✅ |
| §2 | Functional requirements | 1.1, 1.2, 3.1, 3.2, 3.3, 3.4, 4.1, 4.2, 4.3, 5.1, 5.2, 5.3, 6.1, 6.2, 6.3, 6.4, 6.5, 7.1, 7.2, 7.3, 8.1, 8.2, 9.1, 9.2, 9.3 | ⏳ |
| §3 | Non-functional requirements | 1.3, 1.4, 10.2, 11.6 | ⏳ |
| §4 | Logical architecture | 1.1, 1.8, 11.2 | ⏳ |
| §5 | Public interface | 1.2, 3.1, 3.2, 3.3, 3.4, 4.1, 4.2, 4.3, 5.1, 5.2, 5.3, 6.1, 6.2, 6.3, 6.4, 6.5, 7.1, 7.2, 7.3, 8.1, 8.2, 9.1, 9.2, 9.3, 10.1, 11.1, 11.3, 11.7 | ⏳ |
| §6 | Verification & test strategy | 1.2, 1.4, 10.2, 10.3, 11.5 | ⏳ |
