# ADR-0029: Library-wide error-handling policy (value-or-error vs exceptions)

- **Status:** Accepted
- **Date:** 2026-07-05
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §5 (error model), §3 (no UB, zero-dependency), roadmap 11.1,
  [ADR-0021](0021-cli-parser-typed-binding-value-or-error.md) (the `CliParser` value-or-error
  boundary), [ADR-0022](0022-json-parser-non-allocating-pull-events.md) (the sticky `error`
  event), [ADR-0023](0023-file-stream-buffered-descriptor-single-direction.md) and
  [ADR-0025](0025-tcp-socket-nonblocking-poll-readiness-compiled-tier.md) (the OS-I/O
  value-or-error boundary), [ADR-0015](0015-task-future-lightweight-promise-future.md)
  (exception capture across a future), [ADR-0016](0016-lock-free-queue-bounded-vyukov-mpmc.md)
  (`try_push`/`try_pop` non-throwing flow control)

## Context

Every component in the library has independently converged on the same error-handling shape,
and several ADRs cite it as "the library convention" (ADR-0021/0022/0023/0025) — but the
convention itself has never been written down as a first-class decision. The spec's §5 states
only that "fallible operations return a value-or-error type … no exceptions cross module
boundaries except where explicitly documented." That is a contract worth making authoritative
in one place, so that (a) new components inherit it by reference rather than re-derivation, and
(b) a reviewer can hold any public boundary against a single rule.

Three forces shape the policy:

- **The C++20 toolchain floor.** `std::expected` is a C++23 facility. The supported matrix is
  GCC ≥ 11, Clang ≥ 14, MSVC ≥ 19.30, Apple Clang ≥ 14 — none of which can be assumed to ship
  a complete, warning-clean `<expected>` at the floor. A value-or-error policy for this library
  therefore cannot be "just return `std::expected`"; it must be expressible in C++20.
- **Untrusted and routine-failure inputs are ordinary, not exceptional.** Parsers
  (`JsonParser`, `CliParser`, `BinarySerializer`) consume adversarial bytes; sockets refuse,
  reset, and would-block; a bounded queue fills; a file open fails. Making the caller wrap each
  of these in `try` is both a performance tax (exceptions on the hot path) and an ergonomic one.
- **Programmer misuse is genuinely exceptional.** Indexing out of bounds, popping semaphore
  count below zero, requesting a queue capacity of zero, reading a `TaskFuture` twice — these
  are contract violations, not runtime conditions to branch on, and should fail loudly.

## Decision

**The library uses a two-tier error model, uniform across every public boundary:**

1. **Expected, caller-recoverable outcomes are reported by value, never by throwing.** The
   vocabulary, smallest-fitting-first:
   - **`std::optional<T>`** when the only failure is "absent / not available" (`FlatMap::find`
     returns a `const_iterator` compared to `end()`; `CircularBuffer::pop`, `LockFreeQueue::try_pop`
     return `std::optional<T>`; `FileStream::read` returns `std::optional<std::size_t>`).
   - **`bool` plus an out-parameter / a `try_*` name** when the operation either succeeds or is
     benign flow control (`LockFreeQueue::try_push` returns `false` when full — backpressure,
     not an error).
   - **A purpose-built result type with a status enum** when the outcome is genuinely N-way and
     an optional would overload one channel: `TcpSocket` `IoResult`/`IoStatus`
     (`ok`/`closed`/`would_block`/`error`), `WaitResult` (`ready`/`timed_out`/`error`),
     `CliParser::ParseResult`.
   - **A sticky, positioned error state** for single-pass consumers of untrusted streams:
     `JsonParser::next()` latches an `error` event and exposes `error_message()`/`error_offset()`
     rather than throwing mid-parse.
   - **Native OS error codes are surfaced, not translated to exceptions:** `error()` carries the
     raw `errno` / `WSAGetLastError()` on the I/O types.

2. **Programmer misuse (precondition violation) throws a `std::logic_error`-family exception.**
   `std::out_of_range` for bounds (`HeapArray::at`), `std::invalid_argument` for illegal
   construction arguments (a zero or over-large `LockFreeQueue` capacity), `std::logic_error`
   for protocol misuse (double-reading a `TaskFuture`, releasing a `Semaphore`/`ObjectPool`
   slot it does not own). These are bugs in the caller; they are documented per type and
   covered by tests, and they are the *only* exceptions the library itself raises.

**Cross-cutting rules:**

- **`std::expected` is not adopted** (C++23, above the floor). If the floor moves to C++23 in a
  future major, migrating the purpose-built result types to `std::expected` is an ABI/source
  break to be weighed then — recorded here so the option is not silently forgotten.
- **`noexcept` is applied to the operations that must not fail:** destructors, move
  constructors/assignment, observers (`size`, `empty`, `capacity`, `is_open`), and `swap`. A
  `noexcept` move is what lets the containers give the strong guarantee cheaply.
- **Exceptions do not cross a `noexcept` boundary or a thread boundary uncaught.** `ThreadPool`
  workers and the `Logger` pump catch and contain; `TaskFuture` *captures* a task's exception
  and re-throws it in the consumer that calls `get()` (ADR-0015) — the one deliberate,
  documented channel by which an exception crosses a boundary.
- **`std::bad_alloc` is allowed to propagate.** Allocation failure is not something the library
  masks; the zero-allocation hot paths simply never trigger it.

## Alternatives Considered

- **`std::expected<T, E>` everywhere** — the modern, composable answer. Rejected: it is C++23
  and the toolchain floor is C++20; adopting it now would either raise the floor (a scope
  decision this ADR is not making) or require a vendored polyfill (a dependency the zero-dependency
  contract forbids). The purpose-built result types express the same "value or error" intent
  in-floor and can migrate later.
- **Exceptions as the primary channel** (throw on bad input, refused connection, full queue) —
  rejected: it pushes `try`/`catch` onto every call site for conditions that are ordinary
  control flow, taxes the hot paths the library exists to keep fast, and is hostile to the
  untrusted-input parsers where malformed data is the common case, not the exception.
- **Error codes only (a global `errno`-style channel, no exceptions at all)** — rejected: it
  cannot distinguish a caller bug from a runtime condition, silently invites ignored returns,
  and is worse than a thrown `logic_error` for genuine precondition violations that should abort
  a test.
- **A single library-wide `Result<T>` type** — rejected as premature uniformity: "absent" is
  best said with `std::optional`, backpressure with `bool`, and a four-way socket outcome with a
  dedicated status enum. Forcing all three into one type would obscure intent more than it would
  unify it (AGENTS §8: never force-fit).

## Consequences

- The spec §5 error model now has an authoritative, enumerated backing decision; new components
  cite this ADR instead of re-litigating the boundary, and the per-module contract table
  (roadmap 11.3) records each type's exact channel.
- Callers can rely on a single mental model: *branch on the return for anything the world can
  cause; a thrown exception means a bug on my side (or `bad_alloc`).* No public API throws on
  well-formed-but-unlucky input.
- `noexcept` on moves/observers/destructors is a tested contract, not incidental — it underpins
  the containers' exception-safety guarantees (also tabulated in 11.3).
- **Known limitation / future work:** the value-or-error result types are hand-rolled; a C++23
  floor would let them become `std::expected`. This is a deliberate, revisitable trade recorded
  above, not an oversight.
- No pattern-catalogue change: this is a boundary/convention decision, not the adoption of a
  classical design pattern.

## References

- Spec §5 (error model), §3 (no UB, zero-dependency), §2 (per-component behavior).
- ADR-0015 (future exception capture), ADR-0016 (`try_*` non-throwing flow control),
  ADR-0021 (`CliParser` value-or-error), ADR-0022 (`JsonParser` sticky error),
  ADR-0023 / ADR-0025 (OS-I/O value-or-error boundary and native error codes).
- ISO/IEC 14882:2020 (C++20, the toolchain floor); `std::expected` is ISO/IEC 14882:2023.
