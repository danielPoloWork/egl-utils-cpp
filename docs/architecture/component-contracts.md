# Per-Component Contract Table

The behavioral contract for every one of the 25 components, in four dimensions:
**thread-safety**, **exception-safety**, **allocation behavior**, and **algorithmic
complexity**. This is the consolidated, at-a-glance form of the per-type Doxygen contracts;
where a value here needs the *why*, the linked ADR carries it. The error model these rows
assume is [ADR-0029](../adr/0029-error-handling-policy.md); the module/dependency structure is
the [C4 component diagram](c4-component-diagram.md); the timing numbers behind the complexity
column are in [`docs/benchmarks/`](../benchmarks/).

## Reading the columns

- **Thread-safety** — *Thread-safe*: safe to call from multiple threads concurrently (mechanism
  noted). *Not thread-safe*: one instance is driven by one thread at a time; several note that
  concurrent **const** reads are fine.
- **Exception-safety** — the guarantee on the hot operations, using the standard ladder
  (*noexcept* ⊂ *strong* commit-or-rollback ⊂ *basic* valid-but-changed). Per ADR-0029, a
  **runtime** failure is reported by value (never thrown) and only **programmer misuse** throws a
  `std::logic_error`-family exception; the "throws" notes below are therefore all misuse paths
  (plus `std::bad_alloc`, which may always propagate).
- **Allocation** — *None*, *One-time at construction*, *Per-operation*, *Amortized-growth*, or
  *On-demand*, with where the storage lives.
- **Complexity** — Big-O of the primary operation(s); `n` = element/byte count unless noted.

## memory/

| Component | Thread-safety | Exception-safety | Allocation | Complexity |
|---|---|---|---|---|
| `ObjectPool<T>` | Thread-safe (mutex-guarded free list) | Strong; `try_acquire`/`release` `noexcept`; throws only on misuse (releasing a foreign slot) | One-time at construction (fixed `std::vector`; slots reused, never freed until dtor) | O(1) acquire / release |
| `UniqueRef<T>` | Not thread-safe | Strong; move / dtor / observers `noexcept`; may throw only at construction (allocation) | One-time (single heap object; no null state) | O(1) all ops |
| `StackAllocator<Size>` | Not thread-safe | Basic; `allocate` throws `std::bad_alloc` on exhaustion; `reset`/`rewind`/`mark` `noexcept` | None — in-object `std::array<std::byte, Size>` (on the stack while the instance is) | O(1) allocate / reset |
| `HeapArray<T>` | Not thread-safe (concurrent const reads OK) | Strong; `at()` throws `std::out_of_range` (misuse); `operator[]` `noexcept` | One-time single heap block; size immutable after construction | O(n) construct, O(1) indexed access |

## concurrency/

| Component | Thread-safety | Exception-safety | Allocation | Complexity |
|---|---|---|---|---|
| `ThreadPool` | Thread-safe (mutex + condition variable) | Strong `submit`; `noexcept` shutdown/dtor (drains then joins); workers catch task exceptions; throws `std::logic_error` on submit-after-shutdown | Per-`submit` (task shared state + heap push); threads + task heap at construction | O(log n) submit / worker dequeue (binary heap) |
| `TaskFuture<T>` | Thread-safe shared state (mutex + condition variable) | Strong `get()`; `noexcept` moves; `get()` **re-throws the task's captured exception**; throws `std::logic_error` on invalid use (e.g. double `get`) | One-time (shared state via `shared_ptr`) | O(1) get / ready / wait |
| `LockFreeQueue<T>` | Thread-safe, **lockless** MPMC — *not formally lock-free* (ADR-0016) | `try_push`/`try_pop`/dtor `noexcept` (requires nothrow-move `T`); throws `std::invalid_argument` on bad capacity (misuse) | One-time (pre-sized power-of-two slot ring) | O(1) try_push / try_pop |
| `ReaderWriterLock` | Thread-safe, writer-preferring (mutex + two condition variables) | `noexcept` unlock paths; `lock`/`try_lock`/shared variants do not throw | None (counters + flags only) | O(1) lock / unlock (shared & exclusive) |
| `Semaphore` | Thread-safe, monitor (mutex + condition variable) | `noexcept` acquire / try_acquire / dtor; `release` throws `std::invalid_argument` / `std::overflow_error` on misuse/overflow | None (count only) | O(1) acquire / release |

## strings/

| Component | Thread-safety | Exception-safety | Allocation | Complexity |
|---|---|---|---|---|
| `StringBuilder` | Not thread-safe | Strong; fluent chained `append`; integer append via `std::to_chars` | Amortized-growth `std::string` (call `reserve()` to prealloc) | Amortized O(1) per char, O(k) per k-byte chunk |
| `StringSplitter` | Thread-safe (stateless view; concurrent const reads OK) | `noexcept`; `constexpr` iteration | None — zero-allocation over `std::string_view` (source must outlive) | O(1) per step; O(n) total walk |
| `StringFormatter` | Thread-safe (stateless, pure) | Strong; **format string validated at compile time (`consteval`)** | Per-format via `StringBuilder`; the parse itself is allocation-free | O(n) in format length + arguments |

## containers/

| Component | Thread-safety | Exception-safety | Allocation | Complexity |
|---|---|---|---|---|
| `CircularBuffer<T>` | Not thread-safe | Strong; `try_push` fails cleanly when full; `push_overwrite` drops the oldest | One-time fixed capacity at construction; never reallocates | O(1) push / pop / front / back |
| `FlatMap<Key,Value>` | Not thread-safe (concurrent const reads OK) | Lookups `noexcept` (strong); insert/erase **basic** (element-move throw may leave a shifted vector); `at()` throws `std::out_of_range` (misuse) | Contiguous `std::vector`; insert/erase shift; `constexpr` sorted insertion | O(log n) find / contains; O(n) insert / erase; O(n log n) bulk build |
| `FlatSet<T>` | Not thread-safe (concurrent const reads OK) | Lookups `noexcept` (strong); insert/erase **basic** | Contiguous `std::vector`; `std::sort` at init | O(log n) find / contains; O(n) insert / erase; O(n log n) init |

## io/

| Component | Thread-safety | Exception-safety | Allocation | Complexity |
|---|---|---|---|---|
| `FileStream` | Not thread-safe (one stream per thread) | Runtime I/O returns `std::optional`/`bool` (no throw); wrong-direction use throws `std::logic_error`; move/dtor `noexcept` | One-time configurable buffer at open | O(n) read / write (buffer fill / flush) |
| `TcpSocket` / `TcpServer` | Not thread-safe (one socket per thread) | Runtime failures via `IoResult`/`IoStatus`/`error()` (no throw); closed-socket ops are graceful no-ops; move/dtor `noexcept` | One-time at connect/listen; per-op `poll` (no heap) | O(1) send / recv / poll; O(n) name resolution on connect |
| `BinarySerializer` | Not thread-safe | No-throw; overflow latches a flag and returns `false`; fully `constexpr` | None — writes into a caller-owned `std::span` (deserializer reads a span) | O(n) over bytes; O(1) per scalar |

## diagnostics/

| Component | Thread-safety | Exception-safety | Allocation | Complexity |
|---|---|---|---|---|
| `Stopwatch` | Not thread-safe | Strong; `start`/`stop` throw `std::logic_error` on misuse; `reset`/`elapsed` `noexcept` | None (value type, on the stack) | O(1) start / stop / elapsed |
| `Logger` | **Thread-safe** (`log`, per-level helpers, `flush`, `set_level`, `level`, `dropped` from any thread) | `log()` never throws (enqueues; the pump catches sink/format errors); `shutdown` idempotent | Per-log (bounded queue node + render/format string) | O(1) enqueue; O(m) format an m-byte message |
| `StackTrace` | Thread-safe (a captured trace is value-copyable; Windows symbolization is mutex-serialized) | `capture()` no-throw; `symbolize()` allocates per frame | Per-capture + per-frame on `symbolize` | O(n) capture (n frames); O(n·m) symbolize |

## parsing/ (incl. foundations)

| Component | Thread-safety | Exception-safety | Allocation | Complexity |
|---|---|---|---|---|
| `CliParser` | Not thread-safe (register + parse on one thread) | Never throws on bad input (`ParseResult`); `std::logic_error` on programmer error | On-demand help string (via `StringBuilder`) | O(n) parse; O(k) per option registration |
| `JsonParser` | Not thread-safe (one parser per thread) | Never throws — sticky, positioned `error` event (ADR-0022) | Non-allocating walk (fixed `MaxDepth` array); `decode_string` allocates **only** on demand | O(n) single-pass walk; O(m) to decode an m-byte string |
| `HashAlgorithms` | Thread-safe (pure, stateless) | `noexcept` + `constexpr` | None (pure functions) | O(n) over input bytes / blocks |
| `TypeTraits` | Thread-safe (compile-time only) | `noexcept` (compile-time) | None (compile-time only) | O(1) compile-time evaluation |

## Notes

- **Every runtime-failure path is a return value, not an exception** — the "throws" entries above
  are exclusively programmer-misuse guards (`std::logic_error` family) or `std::bad_alloc`. This is
  the single boundary rule recorded in [ADR-0029](../adr/0029-error-handling-policy.md).
- **`FlatMap`/`FlatSet` insert/erase give the *basic* guarantee**, not strong: a throwing element
  move during the vector shift can leave the container in a valid-but-changed state. Lookups are
  `noexcept`. Use nothrow-movable value types for a de-facto strong guarantee.
- **`LockFreeQueue` requires a nothrow-move-constructible `T`** and is *lockless*, not formally
  lock-free — the honest progress guarantee is spelled out in
  [ADR-0016](../adr/0016-lock-free-queue-bounded-vyukov-mpmc.md).
- This table is part of the deliverable: a change to any component's thread-safety,
  exception-safety, allocation, or complexity updates the matching row in the same PR.
