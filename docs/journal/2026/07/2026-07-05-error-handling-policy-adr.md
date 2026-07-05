# 2026-07-05 — Error-handling policy ADR + open Milestone 11 (roadmap 11.1)

## What got done

- Opened **Milestone 11 — Specification & Assurance Hardening** in `ROADMAP.md` (items 11.1–11.7)
  to close the gaps from the post-1.0 specification review, and completed the first item.
- **ADR-0029 — library-wide error-handling policy.** Consolidated the value-or-error convention
  that every component had independently converged on (and that ADR-0021/0022/0023/0025 already
  cite as "the library convention") into one authoritative decision backing spec §5.

## Why this ADR, and its shape

The two-tier model, now written down once:

1. **Expected, recoverable outcomes are returned by value, never thrown** — smallest-fitting
   channel first: `std::optional` for absent, `bool`/`try_*` for backpressure, a status-enum
   result type for genuinely N-way outcomes (`IoResult`, `WaitResult`, `ParseResult`), a sticky
   positioned error for single-pass untrusted-stream consumers (`JsonParser`), and raw native
   `errno`/`WSAGetLastError()` surfaced on the I/O types.
2. **Programmer misuse throws a `std::logic_error`-family exception** (`out_of_range`,
   `invalid_argument`, `logic_error`) — the *only* exceptions the library itself raises;
   `std::bad_alloc` is allowed to propagate.

Key recorded decision: **`std::expected` is not adopted** because it is C++23 and the toolchain
floor is C++20 (GCC 11 / Clang 14 / MSVC 19.30 / Apple Clang 14); a vendored polyfill would
violate the zero-dependency contract. The purpose-built result types express the same intent
in-floor and can migrate to `std::expected` if a future major raises the floor — recorded so the
option is not silently forgotten. `noexcept` on moves/observers/destructors is called out as a
tested contract, and `TaskFuture::get()` is named as the one deliberate channel by which an
exception crosses a boundary (capture-and-rethrow, ADR-0015).

## Congruence touch-ups (same PR)

- `docs/adr/README.md` index row for ADR-0029.
- `ROADMAP.md`: new Milestone 11 section (11.1 checked); Spec Coverage Map rows §1/§3/§4/§5/§6
  now reference the relevant 11.x items so the plan is traceable.
- `README.md`: milestone table row 11 = 🚧 in progress.
- `CHANGELOG.md`: `[Unreleased] → Added` note.

## Verification

- `python tools/consistency_lint.py` → OK (all invariants hold: ADR index bijection + sequential
  numbering through 0029, milestone README↔ROADMAP consistency, no dangling spec-map rows).
- Doc-only change: no code touched, no build/test impact. Version stays `1.0.0` (no user-visible
  API change).

## Project state

- Milestones 1–10 complete (v1.0.0 released). **Milestone 11 in progress**: 11.1 done; 11.2–11.7
  remain.

## How the next session resumes

- **One PR at a time (AGENTS §6.1):** wait for the 11.1 PR to merge, then start **11.2 — C4
  component diagram** (25 modules → 7 layers with dependency directions; see spec §4 architecture
  block for the layer grouping).
- Remaining M11 order: 11.2 (C4 diagram) → 11.3 (per-module contract table) → 11.4 (distribution/
  ABI statement) → 11.6 (benchmark targets) → 11.5 (security/threat-model + libFuzzer +
  SHA-256 non-crypto scoping) → 11.7 (§3 example fix + `FlatMap::find` wording reconciliation).
- Decisions already locked with the maintainer: keep the Stopwatch bench harness (add targets,
  not Google Benchmark); SHA-256 is scoped **non-cryptographic**.
