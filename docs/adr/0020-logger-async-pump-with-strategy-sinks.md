# ADR-0020: `Logger` asynchronous single-pump design with Strategy sinks

- **Status:** Accepted
- **Date:** 2026-07-02
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #20), §3 (compiled tier for the async Logger), roadmap 7.3,
  [ADR-0004](0004-adopt-hybrid-header-only-plus-static-build-model.md) (compiled tier),
  [ADR-0012](0012-string-formatter-compile-time-validation.md) (the formatting machinery),
  [ADR-0013](0013-semaphore-monitor-based-implementation.md) /
  [ADR-0016](0016-lock-free-queue-bounded-vyukov-mpmc.md) (queue precedents)

## Context

The spec requires `Logger` (component #20): "asynchronous multi-sink logger (console,
file, UDP) with fast formatting". That one sentence hides five independent design choices:
who formats and when; how records travel from call sites to the writer; who writes and in
what order; how a bounded system behaves when producers outrun the writer; and how the UDP
sink obtains sockets without violating the zero-dependency rule or leaking OS headers.
Forces: call sites must be cheap and type-safe (the library already owns a compile-time
validated formatter, ADR-0012); multiple threads log concurrently but log files must stay
readable (no interleaved partial lines); sinks must be extensible by consumers; a logger
must never take the process down (a diagnostics tool that crashes the diagnosed program is
worse than none); and `<winsock2.h>`/`<netdb.h>` must never reach consumer translation
units (the exact leak ADR-0004 built the compiled tier to prevent).

## Decision

**One dedicated pump thread; producers format eagerly; sinks are a Strategy hierarchy;
the queue is a bounded monitor; overflow policy is explicit; the whole non-template
surface compiles into the static tier.**

- **Eager call-site formatting.** `log(level, fmt, args...)` validates `fmt` at compile
  time (`FormatString<Args...>`, ADR-0012) and formats *after* passing the runtime level
  filter — a filtered record costs one relaxed atomic load and nothing else. Formatting at
  the call site (not in the pump) keeps argument lifetimes trivial: nothing is captured by
  reference into a queue that outlives the call.
- **A dedicated pump thread** drains a bounded, monitor-guarded `std::deque` (one mutex,
  three condition variables) in strict FIFO order, renders each record once into the
  standard line (`[YYYY-MM-DD HH:MM:SS.mmm] [level] [thread] message`, UTC), and fans it
  out to every sink. Sink calls run outside the lock and only on the pump thread, so sinks
  need no internal locking and lines are never interleaved. This is the
  **Producer-Consumer** pattern; the queue-side synchronization is another exercise of
  **Monitor Object** + **Guarded Suspension** (ADR-0013 precedent).
- **`Sink` is a Strategy**: an abstract `write(record, line)`/`flush()` interface with
  three shipped strategies — `ConsoleSink` (stdout/stderr), `FileSink` (buffered binary
  stream), `UdpSink` (one datagram per line) — and open to consumer implementations. A
  sink that throws is absorbed and counted (`sink_failures()`), never killing the pump or
  starving sibling sinks.
- **Explicit `OverflowPolicy`** when the queue is full: `block` (default — lossless,
  unbounded latency) or `drop` (lossy, counted via `dropped()`). Reentrant calls from a
  sink into its own logger that would deadlock (`flush()`, or a blocking `log()` on a full
  queue) are diagnosed with `std::logic_error` — the pump's thread id is cached at
  construction so the guard never races the `join()` in `shutdown()`.
- **`flush()` rides the queue**: a ticket-numbered marker is enqueued (bypassing the
  capacity bound — a blocked flush could never be released by the pump it is about to wait
  for) and the caller waits until the pump completes that ticket, guaranteeing everything
  enqueued before the call was written and the sinks flushed. `shutdown()` (also run by
  the destructor) stops admissions, drains every accepted record, flushes the sinks, joins
  the pump, and completes all outstanding tickets; it is idempotent.
- **Compiled-tier placement (ADR-0004).** Declarations and the templated hot path live in
  `logger.hpp`; every other definition is in `logger.cpp`, so the socket and OS headers
  stay out of consumer TUs. The static tier now links `ws2_32` on Windows (Winsock) and
  `Threads::Threads` (the pump). The `UdpSink` resolves **numeric literals only**
  (`AI_NUMERICHOST | AI_NUMERICSERV`): construction never consults DNS and cannot block;
  hostname resolution belongs to the Milestone-9 socket work, which this sink deliberately
  does not pre-empt with a second resolver.

## Alternatives Considered

- **Reuse `ThreadPool` as the writer** — rejected: the pool's priority queue does not
  preserve FIFO across priorities, a logger wants exactly one writer (ordering, no sink
  locking), and coupling diagnostics to the concurrency module would drag the pool into
  every logging consumer.
- **Format in the pump (capture arguments)** — rejected: type-erasing and owning arbitrary
  argument packs costs an allocation per record anyway, reintroduces lifetime hazards
  (dangling views by the time the pump formats), and moves work from many producer threads
  onto the single pump — the opposite of the scalability goal.
- **`LockFreeQueue` as the record queue** — rejected: it is non-blocking by design
  (ADR-0016), so the `block` policy and the pump's idle wait would need busy-polling or a
  bolted-on eventcount; records (`std::string` payloads) are far from the fixed-slot
  sweet spot; and `flush()` markers need the unbounded-bypass a monitor grants trivially.
  A logging queue is not a hot path at the queue itself — the monitor is simpler and
  sufficient.
- **Observer instead of Strategy for sinks** — rejected as a mislabel: sinks are not
  independent subscribers reacting to state changes; they are interchangeable output
  algorithms invoked by one owner in a fixed order. The taxonomy's Strategy ("encapsulate
  interchangeable algorithms behind a common interface") is the honest fit.
- **Active Object for the whole logger** — rejected as a force-fit: the queue carries
  plain data records, not method-request objects, and callers get no future back —
  labelling this Active Object would stretch the taxonomy (recorded in the patterns
  catalogue).
- **`std::format`** — rejected: absent at the GCC 11 floor; `StringFormatter` (ADR-0012)
  exists precisely to cover this gap and adds compile-time argument-count validation.
- **Third-party (spdlog)** — rejected: the library core has a zero-dependency contract
  (spec §3).

## Consequences

- Producers pay formatting; the pump pays rendering and I/O. A slow sink backpressures
  through the queue: `block` preserves every record at unbounded producer latency, `drop`
  bounds latency and counts losses — the trade-off is the consumer's, per logger.
- Sinks are trivially implementable (no locking contract) but must not call back into
  their owning logger; the two deadlock shapes are diagnosed loudly rather than hung.
- Records below the filter level are *free* apart from one relaxed load; records above it
  allocate (the formatted `std::string` plus queue slot) — an allocation-free fast path
  was traded away for lifetime simplicity and is re-openable if profiling ever demands it.
- The static tier gains its third translation unit and two link requirements (`ws2_32` on
  Windows, `Threads::Threads` everywhere); header-only consumers still link nothing and
  see only declarations — calling them without the tier stays a link-time error (same
  contract as `library_version()`, ADR-0004).
- `UdpSink` accepts only numeric address literals until Milestone 9 lands a real resolver;
  datagram loss in transit is invisible by design and only local send failures are
  observable (absorbed and counted by the pump).
- Rendering failures (allocation) are counted in `sink_failures()` alongside sink throws —
  the pump never lets an exception escape.
- Patterns catalogue: **Strategy** and **Producer-Consumer** adopted; **Monitor Object**
  and **Guarded Suspension** gain another exercise; **Active Object** recorded as
  rejected.

## References

- Spec §2 component #20, §3 non-functional requirements (compiled tier list), §5 error
  model.
- ADR-0004 (tier contract), ADR-0012 (`FormatString`/`Formattable`), ADR-0013 (monitor
  precedent), ADR-0016 (why the lock-free queue stays in messaging).
- MSDN: `WSAStartup`/`WSACleanup` reference counting, `getaddrinfo` numeric flags;
  POSIX `getaddrinfo(3)`, `sendto(2)`.
