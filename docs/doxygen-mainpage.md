# egl-util-cpp API Reference {#mainpage}

`egl-util-cpp` is a **header-only C++20 toolkit for high-performance systems** — controlled
allocation, advanced concurrency, zero-copy strings, contiguous containers, I/O & networking,
diagnostics, and parsing — with an optional compiled tier for the OS-boundary components. Every
type lives in the `it::d4np::util` namespace; include the whole surface with
`#include <it/d4np/util/util.hpp>` or a single component header directly.

This page is the entry point to the **frozen 1.0 public API**. For the project overview, build
instructions, roadmap, and change history, see the
[project repository](https://github.com/danielPoloWork/egl-util-cpp).

## Public API by module

### Foundations
Compile-time type utilities in `it/d4np/util/type_traits.hpp` and constexpr hashing
(`fnv1a_64`, `fnv1a_32`, `murmur3_x86_32`, `sha256`) in `it/d4np/util/hash.hpp`.

### Memory & resource management
- it::d4np::util::UniqueRef — non-null unique-ownership pointer.
- it::d4np::util::HeapArray — fixed-size, bounds-checked heap array.
- it::d4np::util::StackAllocator — stack-backed bump allocator.
- it::d4np::util::ObjectPool — thread-safe O(1) object pool.

### Contiguous containers
- it::d4np::util::FlatSet — sorted-vector set.
- it::d4np::util::FlatMap — sorted contiguous-array map (constexpr-capable).
- it::d4np::util::CircularBuffer — fixed circular byte buffer.

### Zero-copy strings
- it::d4np::util::StringSplitter — `string_view`-based, zero-allocation splitter.
- it::d4np::util::StringBuilder — preallocated fluent builder.
- it::d4np::util::StringFormatter — compile-time type-safe formatter.

### Concurrency & multithreading
- it::d4np::util::Semaphore — portable counting semaphore.
- it::d4np::util::ReaderWriterLock — read-optimized shared mutex.
- it::d4np::util::TaskFuture — lightweight future/promise.
- it::d4np::util::LockFreeQueue — bounded MPMC lock-free queue.
- it::d4np::util::ThreadPool — priority-queue work pool.

### Diagnostics & instrumentation
- it::d4np::util::Stopwatch — high-resolution profiler.
- it::d4np::util::StackTrace — native-API stack capture (compiled tier).
- it::d4np::util::Logger — asynchronous multi-sink logger (compiled tier).

### Parsing & input
- it::d4np::util::CliParser — typed CLI parser with auto-generated help.
- it::d4np::util::JsonParser — non-allocating pull JSON parser.

### I/O & networking
- it::d4np::util::FileStream — buffered RAII file wrapper (compiled tier).
- it::d4np::util::BinarySerializer / it::d4np::util::BinaryDeserializer — endianness-aware binary codec.
- it::d4np::util::TcpSocket / it::d4np::util::TcpServer — non-blocking TCP sockets (compiled tier).

## Tiers

The default target `egl-util::egl-util` is header-only. The OS-boundary components
(`StackTrace`, `Logger`, `FileStream`, `TcpSocket`/`TcpServer`) additionally require linking the
optional compiled tier `egl-util::egl-util-static`; their headers expose only declarations, so a
header-only consumer never pays for what it does not use.
