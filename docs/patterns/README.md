# Design Patterns Catalogue

Living index of every design pattern **adopted**, **planned**, **considered and rejected**,
or **under evaluation** for `egl-util-cpp`. Mandatory reading whenever a PR introduces
or removes a pattern, and updated in the same PR.

- **Rules** — [`AGENTS.md`](../../AGENTS.md) §8.
- **Canonical taxonomy** — [`design-patterns.md`](design-patterns.md). All pattern names
  used here, in ADRs, and in commit messages must match its spelling and categorisation.

## How to use this catalogue

- **Adding a pattern** — when a PR adopts one, add a row to *Adopted* with the ADR link and
  the code location (a real path under `src/main/cpp/...`).
- **Refining** — update the row and link the new ADR.
- **Rejecting** — add it to *Rejected* with the reason; do not silently drop it.
- **Removing** — move the row to *Superseded*, link the superseding ADR, keep the history.

Status vocabulary: `Planned` (decided in an ADR, not yet landed) · `Implemented` (present
in `src/main/...`, ADR `Accepted`) · `Considered` · `Rejected` · `Superseded`.

## Adopted / Planned

| # | Pattern | Status | Problem it addresses | Code location | ADR / PR |
|---|---------|--------|----------------------|---------------|----------|
| 1 | Object Pool | Implemented | Reuse a fixed set of pre-allocated objects so hot paths avoid per-use allocation and construction cost | [`object_pool.hpp`](../../src/main/cpp/it/d4np/util/object_pool.hpp) | [ADR-0008](../adr/0008-adopt-object-pool-pattern.md) |
| 2 | Monitor Object | Implemented | Serialise every operation on the shared state under one internal lock, making the whole public surface thread-safe by construction | [`semaphore.hpp`](../../src/main/cpp/it/d4np/util/semaphore.hpp), [`reader_writer_lock.hpp`](../../src/main/cpp/it/d4np/util/reader_writer_lock.hpp), [`task_future.hpp`](../../src/main/cpp/it/d4np/util/task_future.hpp), [`thread_pool.hpp`](../../src/main/cpp/it/d4np/util/thread_pool.hpp), [`logger.cpp`](../../src/main/cpp/it/d4np/util/logger.cpp) | [ADR-0013](../adr/0013-semaphore-monitor-based-implementation.md), [ADR-0014](../adr/0014-reader-writer-lock-writer-preference-policy.md), [ADR-0015](../adr/0015-task-future-lightweight-promise-future.md), [ADR-0017](../adr/0017-thread-pool-priority-work-pool.md), [ADR-0020](../adr/0020-logger-async-pump-with-strategy-sinks.md) |
| 3 | Guarded Suspension | Implemented | Block until the guard holds (`count > 0`; no writer active/waiting; result present; work queued), via condition-variable predicate waits that absorb spurious wakeups | [`semaphore.hpp`](../../src/main/cpp/it/d4np/util/semaphore.hpp), [`reader_writer_lock.hpp`](../../src/main/cpp/it/d4np/util/reader_writer_lock.hpp), [`task_future.hpp`](../../src/main/cpp/it/d4np/util/task_future.hpp), [`thread_pool.hpp`](../../src/main/cpp/it/d4np/util/thread_pool.hpp), [`logger.cpp`](../../src/main/cpp/it/d4np/util/logger.cpp) | [ADR-0013](../adr/0013-semaphore-monitor-based-implementation.md), [ADR-0014](../adr/0014-reader-writer-lock-writer-preference-policy.md), [ADR-0015](../adr/0015-task-future-lightweight-promise-future.md), [ADR-0017](../adr/0017-thread-pool-priority-work-pool.md), [ADR-0020](../adr/0020-logger-async-pump-with-strategy-sinks.md) |
| 4 | Future / Promise | Implemented | Hand a not-yet-available result (or exception) from a producing thread to a consuming one, exactly once | [`task_future.hpp`](../../src/main/cpp/it/d4np/util/task_future.hpp) | [ADR-0015](../adr/0015-task-future-lightweight-promise-future.md) |
| 5 | Thread Pool | Implemented | Reuse a bounded set of worker threads to run submitted tasks, avoiding per-task thread creation cost | [`thread_pool.hpp`](../../src/main/cpp/it/d4np/util/thread_pool.hpp) | [ADR-0017](../adr/0017-thread-pool-priority-work-pool.md) |
| 6 | Strategy | Implemented | Encapsulate interchangeable output algorithms (console, file, UDP, consumer-defined) behind one `Sink` interface the logger fans out to | [`logger.hpp`](../../src/main/cpp/it/d4np/util/logger.hpp) | [ADR-0020](../adr/0020-logger-async-pump-with-strategy-sinks.md) |
| 7 | Producer-Consumer | Implemented | Decouple many logging threads from one writing thread via a bounded queue with an explicit overflow policy | [`logger.hpp`](../../src/main/cpp/it/d4np/util/logger.hpp), [`logger.cpp`](../../src/main/cpp/it/d4np/util/logger.cpp) | [ADR-0020](../adr/0020-logger-async-pump-with-strategy-sinks.md) |

## Rejected

| # | Pattern | Considered for | Rejected because | ADR / PR |
|---|---------|----------------|------------------|----------|
| 1 | Builder | `StringBuilder` (component #10) | `StringBuilder` is a fluent string accumulator, not the GoF Builder — there is no director, no product family, and no separation of construction from representation. Labelling it "Builder" would be a force-fit (AGENTS §8); its fluent `append` chain is an idiom, not a pattern adoption. | roadmap 5.2 |
| 2 | Interpreter | `StringFormatter` (component #12) | The `{}` format-string mini-language has no grammar-as-object-structure and no evaluator hierarchy — validation and rendering are single-pass scans over a `string_view`. Building an AST of terminal/non-terminal expression nodes for a two-token grammar would be a force-fit (AGENTS §8). | [ADR-0012](../adr/0012-string-formatter-compile-time-validation.md) |
| 3 | Active Object | `Logger` (component #20) | The pump's queue carries plain data records, not method-request objects, and callers get no future back — the logger is Producer-Consumer, and labelling it Active Object would stretch the taxonomy (AGENTS §8). | [ADR-0020](../adr/0020-logger-async-pump-with-strategy-sinks.md) |
| 4 | Memento | `BinarySerializer` (component #18) | The codec externalizes caller-supplied *primitives* into a flat byte cursor; there is no originator/caretaker separation and no opaque, encapsulation-preserving state object. Calling it Memento would be a force-fit (AGENTS §8) — it is the value-or-error byte-cursor idiom. | [ADR-0024](../adr/0024-binary-serializer-endianness-aware-header-only-codec.md) |
| 5 | Reactor | `TcpSocket` / `TcpServer` (component #17) | The poll-based `wait_*` calls are synchronous, one-shot readiness checks the caller drives; there is no event demultiplexer dispatching to registered event handlers and no dispatch loop. Labelling it Reactor would overstate the design (AGENTS §8) — an epoll/kqueue/IOCP reactor is a possible future layer on top. | [ADR-0025](../adr/0025-tcp-socket-nonblocking-poll-readiness-compiled-tier.md) |

## Superseded

_No superseded patterns yet._

| # | Pattern | Superseded by | When | ADR / PR |
|---|---------|---------------|------|----------|
| — | —       | —             | —    | —        |

## Candidate patterns to consider

The taxonomy in [`design-patterns.md`](design-patterns.md) lists every pattern in scope. As
the architecture takes shape, narrow that universe to the patterns plausibly applicable to
*this* artifact and list them here by category, each with a one-line "possible application".
A candidate remains a candidate until adopted (own ADR) or explicitly rejected.

## Out-of-scope categories

Record here any taxonomy category pre-classified as not applicable to this artifact (with a
one-line reason), so the policy of explicit rejection is honoured without filling the
*Rejected* table with N/A noise.
