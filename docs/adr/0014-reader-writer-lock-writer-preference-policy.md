# ADR-0014: `ReaderWriterLock` writer-preference policy over a monitor

- **Status:** Accepted
- **Date:** 2026-07-01
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #8), §3 (TSan-clean concurrency), roadmap 6.2,
  [ADR-0013](0013-semaphore-monitor-based-implementation.md) (monitor-based concurrency
  precedent and the wrap/reimplement/native analysis this ADR extends)

## Context

The spec requires `ReaderWriterLock` (component #8): a "shared-mutex reader/writer lock
optimized for read-mostly access". C++17 already ships `std::shared_mutex`, so — as with
`Semaphore` (ADR-0013) — the question is what this component adds. The answer is the
**fairness contract**. The standard deliberately leaves `std::shared_mutex` scheduling
unspecified; in practice glibc's `pthread_rwlock_t` (what libstdc++ uses) defaults to
*reader preference*, under which a steady stream of readers starves writers indefinitely —
the exact failure mode of the read-mostly workloads this component targets. An "optimized
for read-mostly access" lock that cannot promise a writer ever runs is not enterprise-grade;
the policy must be explicit, documented, and identical on every platform of the matrix.

## Decision

`ReaderWriterLock` is a **writer-preferring** reader/writer lock implemented, like
`Semaphore`, as a Monitor Object over `std::mutex` + two `std::condition_variable`s with
Guarded Suspension waits: any number of readers hold the lock concurrently; a writer holds
it exclusively; and **as soon as a writer is waiting, new readers are held back** until the
writer has run, so writers cannot be starved by a continuous reader stream. The interface
satisfies the standard **SharedTimedMutex** named requirements — `lock`, `try_lock`,
`try_lock_for`, `try_lock_until`, `unlock`, plus the `_shared` counterparts — so
`std::unique_lock`, `std::scoped_lock`, `std::shared_lock`, and `std::condition_variable_any`
compose with it directly and no bespoke RAII guards are needed. Unlock paths are
non-throwing by design: RAII guards call them from destructors, so precondition violations
(unlocking a lock the thread does not hold) are documented preconditions rather than
diagnosed exceptions — unlike `Semaphore::release`, which throws because overflow is a
reachable failure of a *normal* operation, not a precondition breach. The type is
non-copyable and non-movable.

## Alternatives Considered

- **Thin wrapper over `std::shared_mutex`** — near-zero cost. Rejected because the fairness
  policy is the component's entire reason to exist: the standard type's scheduling is
  unspecified and platform-divergent (reader-preferring on glibc, different on MSVC's
  SRWLOCK-based implementation), so the wrapper could not document — let alone guarantee —
  starvation freedom for writers on any cell of the matrix.
- **Native OS read/write locks** (`pthread_rwlock_t` with `PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP`,
  `SRWLOCK`) — rejected for the ADR-0013 reasons (platform code paths in the header-only
  tier) plus a new one: the writer-preference knob is a non-portable GNU extension, absent
  exactly where it would be needed.
- **Phase-fair / FIFO queue lock** — alternates reader phases and writer turns, bounding
  wait times for both sides. Rejected as over-engineering for the spec's stated workload:
  read-mostly traffic has no sustained writer stream to starve readers, and phase-fairness
  costs a ticket queue on every acquisition. Recorded as the upgrade path if a future
  component needs bounded reader latency under heavy writing.
- **Reader preference** (readers always pass while any reader holds) — maximizes reader
  throughput but lets readers starve writers indefinitely; this is the glibc default the
  component exists to escape.

## Consequences

- The fairness contract is explicit and portable: readers batch concurrently, writers are
  never starved, and behavior is identical on Linux/Windows/macOS because scheduling is
  decided by this class, not the platform.
- The documented trade-off of writer preference: a *continuous* stream of writers can
  starve readers. For read-mostly workloads (the spec's target) this is the correct side
  of the trade; the phase-fair alternative is recorded above as the escape hatch.
- Satisfying SharedTimedMutex means zero new vocabulary for consumers (`std::shared_lock`
  works), and the standard's precondition model applies: recursive acquisition and
  unlocking from a non-owning thread are undefined, stated in the API docs.
- A timed writer that gives up must wake the readers it was holding back (they were blocked
  on its account); this subtle path is implemented in `try_lock_for`/`try_lock_until` and
  covered by tests.
- Same performance profile as ADR-0013: a mutexed fast path instead of the futex/SRWLOCK
  fast path of `std::shared_mutex` — acceptable for the library's gating use cases, and the
  engine is swappable behind the API if a benchmark ever demands it.
- **Patterns (AGENTS §8):** second exercise of **Monitor Object** + **Guarded Suspension**;
  the catalogue rows gain this component as an additional location, justified here.

## References

- Spec §2 component #8, §3 non-functional requirements, §5 public interface.
- C++ standard: [thread.sharedmutex.requirements] — SharedTimedMutex named requirements.
- glibc `pthread_rwlock_t` reader-preference default and
  `PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP` (non-portable).
- Brandenburg & Anderson, *Spin-Based Reader-Writer Synchronization for Multiprocessor
  Real-Time Systems* — phase-fair RW locks (the rejected alternative).
