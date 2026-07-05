# Software Specification: High-Performance C++ Utilities (C++20)

> Rendered from the intake interview (Phase 5). Frozen contract: diverging implementation
> updates this spec in the same PR or adds an ADR superseding the relevant section.

## 1. Objective & Business Context

egl-util-cpp is a header-only (with optional partial compilation) C++20 library for building high-performance systems with controlled resource allocation and advanced concurrency. It targets open-source C++ developers who need a zero-dependency, cache-friendly, leak-free toolkit spanning allocation, concurrency, zero-copy strings, contiguous containers, I/O & networking, diagnostics, and parsing without pulling in a heavyweight framework. The design rests on three pillars: strict RAII for safe resource lifetimes, zero-copy data flow (move semantics, std::string_view, std::span), and compile-time optimization via SFINAE/Concepts and constexpr.

## 2. Functional Requirements

- ObjectPool<T>: thread-safe pool of pre-allocated objects with O(1) acquire/release, reusing storage to avoid runtime allocation (component #1).
- UniqueRef<T>: non-null unique-ownership smart pointer that guarantees an always-valid instance at compile time, with no null state (component #2).
- StackAllocator<Size>: stack-backed bump allocator for temporary dynamic arrays, with zero heap allocation up to Size (component #3).
- HeapArray<T>: fixed-size heap array with no resize overhead and bounds-checked access (component #4).
- ThreadPool: asynchronous worker pool with an internal priority queue for load balancing (component #5).
- TaskFuture<T>: lightweight future/promise for cooperative tasks (component #6).
- LockFreeQueue<T>: lock-free MPMC queue for fast internal messaging (component #7).
- ReaderWriterLock: shared-mutex reader/writer lock optimized for read-mostly access (component #8).
- Semaphore: portable counting semaphore over OS synchronization primitives (component #9).
- StringBuilder: preallocated, fluent string builder that minimizes reallocations (component #10).
- StringSplitter: std::string_view-based splitter with zero runtime allocations (component #11).
- StringFormatter: compile-time type-safe formatting engine, std::format-like (component #12).
- CircularBuffer<T>: fixed circular buffer optimized for byte-stream streaming (component #13).
- FlatMap<Key,Value>: sorted contiguous-array map for cache-friendly lookups (component #14).
- FlatSet<T>: sorted-vector set (component #15).
- FileStream: RAII wrapper over OS file descriptors with configurable buffering (component #16).
- TcpSocket / TcpServer: non-blocking asynchronous socket and server over select/poll/epoll (component #17).
- BinarySerializer: fast binary serializer with hardware-endianness support (component #18).
- Stopwatch: microsecond high-resolution profiler for locating bottlenecks (component #19).
- Logger: asynchronous multi-sink logger (console, file, UDP) with fast formatting (component #20).
- StackTrace: capture the current stack trace via native OS APIs (component #21).
- CliParser: typed command-line argument parser with auto-generated help (component #22).
- JsonParser: lightweight non-allocating JSON parser operating over string views (component #23).
- HashAlgorithms: FNV-1a, MurmurHash3, and SHA-256 implemented in constexpr form for static computation (component #24).
- TypeTraits: compile-time utilities for verifying special properties of custom types (component #25).


## 3. Non-Functional Requirements

- Strict RAII with zero resource leaks: Valgrind --leak-check=full reports zero definite/indirect leaks; ASan clean.
- Zero-copy data flow throughout: move semantics, std::string_view, and std::span; no unnecessary allocations or copies.
- Compile-time optimization via SFINAE/Concepts; constexpr-evaluable where applicable (HashAlgorithms, TypeTraits, StringFormatter).
- Zero runtime dependencies in the library core; test/bench dependencies (doctest) are gated behind CMake test/bench options only.
- Explicit thread-safety contract per component; concurrency primitives carry no data races under ThreadSanitizer.
- No undefined behavior: clean under UBSanitizer; clang-tidy runs warnings-as-errors.
- Cross-platform support: Linux (GCC>=11, Clang>=14), Windows (MSVC>=19.30), macOS arm64 (Apple Clang>=14).
- C++20 standard; header-only by default with opt-in compiled translation units for OS-API-heavy components (networking, async Logger, StackTrace).
- Stable, documented public API (Doxygen); SemVer once 1.0 is reached.


## 4. Logical Architecture & Core Algorithm

The library is organized as seven cohesive modules under the `it::d4np::util` namespace,
each a self-contained set of headers, plus an aggregate umbrella header `util.hpp` that
pulls in every module. Consumers may include the umbrella or a single module header.

Distribution is hybrid: a CMake INTERFACE target exposes the header-only surface, and an
optional STATIC target compiles the OS-API-heavy units (TcpSocket/TcpServer, the async
Logger, StackTrace) when a consumer opts in. The library core carries zero runtime
dependencies; doctest and the benchmark harness are gated behind test/bench CMake options.

```text
it::d4np::util
├── memory/        ObjectPool, UniqueRef, StackAllocator, HeapArray   (RAII, allocation)
├── concurrency/   ThreadPool, TaskFuture, LockFreeQueue, ReaderWriterLock, Semaphore
├── strings/       StringBuilder, StringSplitter, StringFormatter      (zero-copy)
├── containers/    CircularBuffer, FlatMap, FlatSet                     (contiguous, cache-friendly)
├── io/            FileStream, TcpSocket/TcpServer, BinarySerializer    (compiled tier)
├── diagnostics/   Stopwatch, Logger, StackTrace
└── parsing/       CliParser, JsonParser, HashAlgorithms, TypeTraits
```

Core algorithmic stance: contiguous storage for cache locality (FlatMap/FlatSet are sorted
arrays with binary search), lock-free structures for hot messaging paths, and constexpr
evaluation for hashing and type introspection so cost moves from runtime to compile time.

The **C4 Level-3 component view** — the seven modules and the actual internal dependency
edges (a DAG whose cross-module edges terminate in the `strings`/`memory` foundations) — is
maintained in [`docs/architecture/c4-component-diagram.md`](../architecture/c4-component-diagram.md).

## 5. Public Interface

Consumers import via `#include <it/d4np/util/util.hpp>`. The public surface:

- Umbrella header `it/d4np/util/util.hpp` exposes every module; per-module headers are also includable individually.
- Memory: it::d4np::util::ObjectPool<T>, UniqueRef<T>, StackAllocator<Size>, HeapArray<T>.
- Concurrency: it::d4np::util::ThreadPool, TaskFuture<T>, LockFreeQueue<T>, ReaderWriterLock, Semaphore.
- Strings: it::d4np::util::StringBuilder, StringSplitter, StringFormatter.
- Containers: it::d4np::util::CircularBuffer<T>, FlatMap<Key,Value>, FlatSet<T>.
- I/O: it::d4np::util::FileStream, TcpSocket, TcpServer, BinarySerializer.
- Diagnostics: it::d4np::util::Stopwatch, Logger, StackTrace.
- Parsing: it::d4np::util::CliParser, JsonParser, HashAlgorithms, TypeTraits.
- Error model: fallible operations return a value-or-error type (e.g. FlatMap::find returns an optional-like result); no exceptions cross module boundaries except where explicitly documented. The full policy is [ADR-0029](../adr/0029-error-handling-policy.md).
- Per-component contracts (thread-safety, exception-safety, allocation behavior, and algorithmic complexity for all 25 components) are tabulated in [`docs/architecture/component-contracts.md`](../architecture/component-contracts.md).


## 6. Verification & Test Strategy

Correctness and performance are proven mechanically, never asserted. Every component ships doctest unit tests behind a >=80% line-coverage gate (llvm-cov / gcovr). Memory safety is verified under AddressSanitizer and Valgrind (--leak-check=full, zero definite/indirect leaks); undefined behavior under UBSanitizer; the concurrency primitives (ThreadPool, LockFreeQueue, ReaderWriterLock, Semaphore, TaskFuture) under ThreadSanitizer via a dedicated CI job. Compile-time components (HashAlgorithms, TypeTraits, StringFormatter) are validated with constexpr static_asserts so regressions fail at build time. Performance-sensitive components carry a reproducible benchmark suite (cmake --build --preset bench) with baselines published under docs/benchmarks/. clang-format and clang-tidy run warnings-as-errors across the Linux/Windows/macOS matrix.

Toolchain: built with CMake (>=3.20) + Ninja, CMakePresets.json, tested with doctest (FetchContent, test scope only), checked with
ASan, UBSan, TSan, Valgrind, coverage target ≥ 80% line. Every functional and
non-functional requirement above maps to a CI gate (see [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml)).
