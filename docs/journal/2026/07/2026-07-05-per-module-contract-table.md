# 2026-07-05 — Per-component contract table (roadmap 11.3)

## What got done

- Added `docs/architecture/component-contracts.md`: the consolidated contract for all 25
  components in four dimensions — **thread-safety, exception-safety, allocation behavior,
  algorithmic complexity** — grouped by the seven modules. Linked from spec §5.

## How the rows were derived (evidence-based, not guessed)

Fanned out five parallel readers over the actual headers/`.cpp` (one per module group), each
returning the four attributes with `file:line` evidence, then reconciled into one table. Key
facts pinned:

- **Runtime failure is never thrown.** Across every component the "throws" entries are only
  programmer-misuse guards (`std::logic_error` family) or `std::bad_alloc` — exactly the
  ADR-0029 boundary. The table states this once, up front.
- **Thread-safe set is small and deliberate:** `ObjectPool`, all five concurrency primitives,
  `Logger` (async), `StackTrace` (serialized symbolization), and the pure/stateless
  `HashAlgorithms`/`TypeTraits`/`StringSplitter`/`StringFormatter`. Everything else is
  one-instance-one-thread (several allow concurrent const reads).
- **Exception-safety nuances worth calling out:** `FlatMap`/`FlatSet` insert/erase are **basic**,
  not strong (a throwing element move mid-shift leaves a valid-but-changed vector); lookups are
  `noexcept`. `LockFreeQueue` requires nothrow-move `T` and is *lockless*, not formally lock-free
  (ADR-0016). `TaskFuture::get()` re-throws the task's captured exception (the one deliberate
  cross-boundary channel).
- **Allocation:** 8 components allocate nothing at all (`StackAllocator`, `StringSplitter`,
  `BinarySerializer`, `Stopwatch`, the lock primitives, the pure hashes/traits); the rest are
  one-time-at-construction or on-demand. Only `StringBuilder` (amortized string growth), `Logger`
  (per-log node), `ThreadPool` (per-submit), and `StackTrace`/`FlatMap`/`FlatSet` allocate on the
  hot path, all documented.

## Verification

- `python tools/consistency_lint.py` → OK.
- Doc-only; no code, no build impact. Version stays `1.0.0`.

## Project state

- Milestones 1–10 complete. **Milestone 11 in progress:** 11.1, 11.2, 11.3 done; **11.4–11.7
  remain.**

## How the next session resumes

- One PR at a time (AGENTS §6.1): after the 11.3 PR merges, start **11.4 — distribution model +
  explicit ABI-stability statement** (augment ADR-0004 / spec §1; remove any header-only vs
  compiled ambiguity; state what is ABI-stable in the STATIC tier and the SemVer rules that guard
  it). Note the `Logger`/`StackTrace`/`FileStream`/`TcpSocket` compiled tier is the only ABI
  surface; the header-only tier is source-stable, not ABI-stable, by nature.
- Then 11.6 (benchmark targets) → 11.5 (security/threat-model + libFuzzer + SHA-256 non-crypto
  scoping) → 11.7 (§3 example fix + reconcile the spec §5 `find` "optional-like" wording, still
  present on line ~94, with the shipped iterator API).
