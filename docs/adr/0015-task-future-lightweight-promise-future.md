# ADR-0015: `TaskFuture<T>` lightweight promise/future design

- **Status:** Accepted
- **Date:** 2026-07-02
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #6), §5 (error model), roadmap 6.3,
  [ADR-0013](0013-semaphore-monitor-based-implementation.md) (monitor-based shared state),
  roadmap 6.5 (`ThreadPool`, the designated consumer)

## Context

The spec requires `TaskFuture<T>` (component #6): a "lightweight future/promise for
cooperative tasks". The spec itself frames the component as an alternative to
`std::future`/`std::promise`, so the design question is *what "lightweight" keeps and what
it drops*. `std::future` carries machinery a cooperative-task handoff does not need:
`std::async` integration (deferred states, `future_status::deferred`), allocator-aware
shared states, `std::shared_future` conversion, and the `std::future_error`/`error_code`
taxonomy. It also *lacks* small ergonomics that task code wants: a `ready()` probe (the
idiom is `wait_for(0s)`), and a value-or-error surface aligned with this library's error
model (spec §5). The eventual consumer is `ThreadPool` (roadmap 6.5), which needs exactly:
hand a result (or an exception) from a worker thread to a waiter, once.

## Decision

`TaskFuture<T>` / `TaskPromise<T>` are a move-only, single-shot promise/future pair over
**one shared state allocation** (`std::make_shared` of a monitor-protected state: mutex +
condition variable + a `std::variant` holding *empty | value | exception*). The scope is
exactly the cooperative-task handoff: `TaskPromise` offers `get_future()` (once),
`set_value(...)` (perfect-forwarding, single overload) and `set_exception(...)`;
`TaskFuture` offers `valid()`, `ready()`, `wait()`, `wait_for`/`wait_until` (returning
`bool` readiness, not a status enum), and a blocking single-shot `get()` that moves the
value out and invalidates the future. `T = void` is supported through an internal empty
value type, not a separate specialization. The error contract follows the house rules:
**misuse throws `std::logic_error`** (double set, second `get_future`, operations on an
invalid future), and **an abandoned promise** (destroyed before satisfying its future) is
recorded in the shared state as a plain flag and surfaces from `get()` as a dedicated
`BrokenTaskPromise` exception, so consumers have exactly one failure surface. The flag —
rather than an eagerly stored `exception_ptr` or an extra variant alternative — keeps the
promise's teardown path provably nothrow: the exception object is constructed in the
getter's thread, and no `variant::emplace` (whose libstdc++ return path can theoretically
throw `bad_variant_access`, tripping `bugprone-exception-escape` inside `noexcept`
functions) runs during abandonment. Dropped relative to `std`: `std::async` coupling, `shared_future` semantics,
allocator support, and continuations (`then`).

## Alternatives Considered

- **Thin wrapper over `std::promise`/`std::future`** — rejected because it delivers the
  weight without the ergonomics: the deferred-state machinery and `future_error` taxonomy
  come along for the ride, `ready()` still needs the `wait_for(0s)` idiom, and the error
  surface would be `std::future_error` codes rather than the spec's documented-exception
  model. The spec names a *lightweight* component; aliasing the heavyweight one does not
  implement it.
- **Lock-free shared state** (atomic state word + `std::atomic::wait`) — rejected for the
  ADR-0013 reason: C++20 atomic wait/notify at the toolchain floor is the same immature
  machinery this milestone deliberately avoids; a once-per-task mutex handoff is not a
  bottleneck worth that risk.
- **Continuations (`then`) in the first cut** — deferred, not refused: `ThreadPool` (6.5)
  is the component that would give continuations an executor to run on; designing `then`
  before it exists invites a wrong executor model. Recorded as the extension point — the
  shared state is private, so continuations can be added without breaking the API.
- **`set_value` overload pair (`const T&` / `T&&`) like `std`** — rejected for a single
  perfect-forwarding template constrained by `std::convertible_to`: same expressiveness,
  half the surface, and it composes with the `void` case via constraints instead of a
  full class specialization.

## Consequences

- One `make_shared` allocation per task is the entire footprint; no atomics beyond the
  `shared_ptr` control block, no allocator plumbing, no dynamic dispatch.
- Single-shot `get()` (future becomes invalid) makes ownership unambiguous and lets the
  value be *moved* out — move-only result types (`std::unique_ptr`, sockets, buffers) flow
  through naturally. Consumers wanting shared results can layer `shared_ptr` themselves.
- One failure surface: everything a task can do wrong arrives out of `get()` as an
  exception — the task's own exception via `set_exception`, or `BrokenTaskPromise` if the
  promise died unsatisfied. `BrokenTaskPromise` is this library's first custom exception
  type; it exists because callers legitimately `catch` that one condition specifically.
- Individual `TaskFuture`/`TaskPromise` objects are not themselves thread-safe (like
  `std`): the pair may live on two threads, but each end belongs to one thread at a time.
  Documented on both types; the shared state is the synchronized boundary (Monitor Object
  + Guarded Suspension, third exercise — catalogue rows refined).
- `wait_for`/`wait_until` return `bool` (ready or not) instead of a three-state enum —
  there is no deferred state, so a `future_status` clone would have a dead enumerator.
- Testing: the blocking paths reuse the 6.1/6.2 handshake discipline; the move-out path is
  pinned with a move-only payload type.

## References

- Spec §2 component #6, §5 public interface & error model.
- ADR-0013 (monitor engine), ADR-0014 (non-throwing-destructor-path reasoning — the
  promise destructor flips a flag rather than allocating or throwing).
- `std::future`/`std::promise` ([futures] in the C++ standard) — the dropped machinery.
