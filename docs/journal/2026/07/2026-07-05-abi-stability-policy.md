# 2026-07-05 — Distribution model & ABI-stability policy (roadmap 11.4)

## What got done

- **ADR-0030 — distribution model & ABI-stability policy.** Closes the review's two §1 gaps:
  the "header-only *and* partial compilation" ambiguity, and the undefined ABI position.
- Reworded **spec §1** (names the single hybrid model — header-only by default + optional STATIC
  superset, additive not exclusive) and **spec §3** (API/source stable under SemVer; no
  cross-toolchain binary ABI). Replaced `maintenance.md`'s vague "ABI where applicable" with a
  pointer to the policy.

## The decision, briefly

One distribution model: **source distribution of a hybrid library.** SemVer protects the
**API/source** surface (public symbols, target names, the `EGL_UTIL_BUILD_STATIC` knob, the
include root). Cross-toolchain **binary ABI is deliberately not guaranteed** — and that is the
honest position, because:

- the header-only tier is compiled into the consumer's TUs (it *has* no library ABI);
- the STATIC tier ships as source and is built in the consumer's environment, so its object ABI
  is a function of *their* compiler/stdlib/flags, not ours;
- there is no SHARED tier — the only artifact for which a binary-ABI promise would be meaningful —
  and adding one later is gated on its own future ADR (symbol visibility + export map + ABI window).

Rule for consumers: **build everything from one version of the source with one toolchain; trust
SemVer for the API; expect no binary compatibility across versions/toolchains.** Header/binary
skew remains detectable via `library_version()` vs `version_string` (ADR-0004).

## Why a new ADR (not editing ADR-0004)

ADR-0004 is an accepted, dated record of the *build* decision. The stability *contract* is a
distinct, later decision; a new ADR referencing 0004 keeps the history honest (AGENTS §7). ADR-0004
already anticipated this — it deferred SHARED-tier ABI obligations "behind its own option and ADR."

## Verification

- `python tools/consistency_lint.py` → OK (ADR index bijection through 0030, sequential).
- Doc-only; no code, no build impact. Version stays `1.0.0`.

## Project state

- Milestones 1–10 complete. **Milestone 11 in progress:** 11.1–11.4 done; **11.5, 11.6, 11.7
  remain.**

## How the next session resumes

- One PR at a time (AGENTS §6.1): after 11.4 merges, start **11.6 — benchmark targets/thresholds
  + methodology** (turn the `docs/benchmarks/2026-07-05-suite-baseline.md` numbers into documented
  targets/regression thresholds on the existing Stopwatch harness — decision locked: keep the
  harness, not Google Benchmark; state warm-up/reps/p99 methodology).
- Then 11.5 (security/threat-model + libFuzzer targets + SHA-256 non-crypto scoping — the only M11
  item with a code component) → 11.7 (§3 example fix + reconcile spec §5 `find` "optional-like"
  wording on line ~95 with the shipped iterator API).
