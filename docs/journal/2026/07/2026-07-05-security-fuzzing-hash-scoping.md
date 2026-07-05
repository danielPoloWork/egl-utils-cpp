# 2026-07-05 — Security: threat model, fuzzing, hash scoping (roadmap 11.5)

## What got done

The one Milestone-11 item with a code component. Closes the review's security gap:

- **Threat model** (`docs/security/threat-model.md`): trust boundary, per-component attack surface
  + mitigations for the three untrusted-input components (`JsonParser`, `CliParser`,
  `BinaryDeserializer`), and the hash non-crypto scope. Linked from `SECURITY.md` and spec §6.
- **libFuzzer harnesses** (`src/fuzz/cpp/it/d4np/util/`): one per untrusted-input component, gated
  by `EGL_UTIL_BUILD_FUZZERS`, with a `fuzz` preset (Clang/libFuzzer) and a CI `fuzz` smoke job.
- **SHA-256 non-cryptographic scoping** (decision locked): corrected `hash.hpp`'s header (it
  literally said "SHA-256 is the cryptographic digest") + a `@warning` on `sha256`; documented in
  the threat model and ADR-0031.
- **Extended NIST FIPS 180-4 vectors** in `hash_test.cpp`: the 56-byte two-block message
  (compile-time `static_assert`) and the one-million-`'a'` message (runtime).
- **ADR-0031** records the fuzzing strategy and the hash-scoping decision.

## Design notes worth keeping

- **Dual-mode harnesses.** Each harness is a real `LLVMFuzzerTestOneInput`, but when NOT built
  with libFuzzer it compiles a standalone replay `main` (`fuzz_standalone.hpp`). So the whole CI
  matrix (incl. MSVC) builds them, they stay in the `clang-tidy` compile database (the base preset
  sets `EGL_UTIL_BUILD_FUZZERS=ON`), and a crashing corpus entry can be replayed locally without
  libFuzzer. The `fuzz` preset turns on `-fsanitize=fuzzer,address,undefined`; non-Clang toolchains
  fall back to standalone with a warning.
- **Tidy-clean without disables.** The libFuzzer ABI hands you `const uint8_t* data, size_t size`;
  the enabled cppcoreguidelines set bans `reinterpret_cast` and raw pointer arithmetic. Bridged it
  cast-free: wrap in `std::span{data,size}`, then range-copy into a `std::string` (JSON/CLI, element
  `uint8_t→char`) or `std::as_bytes` (BinaryDeserializer). `decode_string` is called on the type
  (`JsonParser<>::decode_string`), not the instance, to satisfy
  `readability-static-accessed-through-instance`. No `NOLINT`.
- **What the fuzzers assert:** never crash / never UB / never throw on input — the mechanical
  backstop for the "malformed input is normal input" contract (ADR-0022/0021/0024/0029).

## Verification

- `python tools/consistency_lint.py` → OK (ADR index sequential through 0031).
- Local build/format status recorded in the PR (libFuzzer itself needs Clang, absent on this
  Windows/MSVC box; the standalone harnesses + the extended SHA-256 vectors build under MSVC).

## Project state

- Milestones 1–10 complete. **Milestone 11 in progress:** 11.1–11.6 done; **only 11.7 remains.**

## How the next session resumes

- One PR at a time: after 11.5 merges, do **11.7 — the last M11 item**: fix the spec §3 example so
  it compiles and demonstrates constexpr `FlatMap` construction, and reconcile the spec §5
  "`FlatMap::find` returns an optional-like result" wording (still on ~line 95) with the shipped
  iterator-returning API. That closes Milestone 11; then it's a release PR (MINOR — new fuzz build
  surface + docs) to cut the next version and close the M11 GitHub milestone.
