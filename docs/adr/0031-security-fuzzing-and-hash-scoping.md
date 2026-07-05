# ADR-0031: Fuzz-testing harnesses & non-cryptographic hash scoping

- **Status:** Accepted
- **Date:** 2026-07-05
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (components #18, #22, #23, #24), §3 (no UB), §6 (verification),
  roadmap 11.5, [ADR-0022](0022-json-parser-non-allocating-pull-events.md) (JsonParser strictness
  & bounded depth), [ADR-0021](0021-cli-parser-typed-binding-value-or-error.md) (CliParser
  value-or-error), [ADR-0024](0024-binary-serializer-endianness-aware-header-only-codec.md)
  (BinarySerializer bounds), [ADR-0029](0029-error-handling-policy.md) (error model),
  [`SECURITY.md`](../../SECURITY.md), [`docs/security/threat-model.md`](../security/threat-model.md)

## Context

The post-1.0 security review flagged that the three components which consume **untrusted input**
— `JsonParser` (#23), `CliParser` (#22), and `BinarySerializer`/`BinaryDeserializer` (#18) — carry
no threat model and no fuzzing plan, and that the `constexpr` SHA-256 (#24) has no test-vector
requirement beyond the single `"abc"` digest and no statement of whether it is offered as a
*cryptographic* primitive. Two decisions follow: **how the untrusted-input boundary is
continuously exercised** (a fuzzing strategy that fits a zero-dependency, header-first library and
the existing CI), and **what security scope the hash functions claim**.

## Decision

**Add coverage-guided fuzz harnesses for the three untrusted-input components, and explicitly
scope every hash in `hash.hpp` — SHA-256 included — as a *non-cryptographic* integrity primitive.**

### Fuzzing

- **One libFuzzer harness per untrusted-input component**, under a new source tier
  `src/fuzz/cpp/it/d4np/util/` (`fuzz_json_parser`, `fuzz_cli_parser`, `fuzz_binary_serializer`),
  gated behind the `EGL_UTIL_BUILD_FUZZERS` CMake option. Each drives the *full* untrusted path:
  the JSON harness walks `next()` to completion and `decode_string()`s every string/key (the
  escape/`\uXXXX`/surrogate decoder is the sharpest edge); the CLI harness splits the buffer into
  argv tokens and runs `parse`; the binary harness reads a mixed scalar/`read_bytes` sequence off
  arbitrary bytes.
- **Dual-mode, so the harnesses build everywhere.** With Clang + `EGL_UTIL_FUZZER_ENGINE=libfuzzer`
  they compile with `-fsanitize=fuzzer,address,undefined`. Otherwise they build in a **standalone
  replay** mode (their own `main` runs each input file once) — so the whole CI matrix (incl. MSVC)
  compiles them, they stay in the `clang-tidy` compile database, and a crashing corpus entry can be
  replayed locally without libFuzzer. The shared body is one cast-free function per harness.
- **CI runs a short fuzz smoke job** (Clang, `fuzz` preset): build all three and run each for a
  bounded `-max_total_time`, so a crasher fails the PR while the run stays minutes-cheap. Longer
  campaigns are run out-of-band; found inputs land as regression corpus / `docs/bugs/` entries.
- **The invariant the fuzzers assert:** these components **never crash, never invoke UB, never
  throw on input** (ADR-0029/0022/0021) — ASan/UBSan under libFuzzer turn any violation into a
  failure. This is the mechanical backstop for the "malformed input is normal input" contract.

### Hash scoping

- **`fnv1a`, `murmur3`, and SHA-256 are integrity/checksum utilities, not cryptographic
  primitives.** SHA-256 is provided for content-addressing, deduplication, corruption detection,
  and as a standards-conformant digest for interop/test vectors — **not** for passwords, MACs/HMAC,
  signatures, or any secret-dependent decision. The `constexpr`, byte-oriented implementation is
  **not constant-time** and is not hardened against side channels; secret-dependent use is out of
  scope. This is stated on the header and in the threat model.
- **Conformance is pinned to NIST FIPS 180-4 vectors:** the empty string, `"abc"`, the 56-byte
  two-block message, and the one-million-`'a'` vector — the short ones as compile-time
  `static_assert`, the 1 M one at run time (too large for a comfortable constant-evaluation budget).
  A wrong digest fails the build or the test.

## Alternatives Considered

- **A fuzzing dependency (OSS-Fuzz integration, AFL++ only)** — deferred: libFuzzer ships with the
  Clang already in the matrix, needs no third-party package, and integrates as a normal CMake
  target. OSS-Fuzz onboarding is a good *later* step and is not precluded (the harnesses are the
  standard `LLVMFuzzerTestOneInput` shape it expects).
- **Fuzz-only targets (no standalone mode)** — rejected: they would compile only under Clang, drop
  out of the tidy compile database, and give no local replay path. The dual-mode `#ifdef` costs a
  few lines and keeps the harnesses first-class on the whole matrix.
- **Make SHA-256 a cryptographic primitive** — rejected (the maintainer's explicit scope call): a
  real cryptographic offering demands a constant-time implementation, side-channel review, and
  probably HMAC — a different project. The honest, minimal position is "integrity, not crypto",
  which matches a `constexpr` byte-at-a-time digest.
- **Gate a long fuzz campaign in CI** — rejected: minutes-expensive and flaky; CI runs a short
  smoke, deeper runs are out-of-band.

## Consequences

- The untrusted-input boundary is now continuously, mechanically exercised; a regression that
  crashes or trips ASan/UBSan on malformed input fails CI instead of shipping.
- `hash.hpp`'s header comment is corrected (it previously called SHA-256 "the cryptographic
  digest"); consumers get an unambiguous scope and will not mistake it for a password/MAC hash.
- New build surface: the `EGL_UTIL_BUILD_FUZZERS` option (default ON at top level, standalone mode),
  a `fuzz` preset (Clang/libFuzzer), and a CI `fuzz` job. Header-only consumers are unaffected.
- Patterns catalogue: unchanged (a testing/security decision, not a design pattern).

## References

- Spec §2 (#18/#22/#23/#24), §3 (no UB), §6 (verification); ADR-0029/0022/0021/0024.
- NIST FIPS 180-4 (SHA-256 and its test vectors); LLVM libFuzzer documentation.
- [`docs/security/threat-model.md`](../security/threat-model.md), [`SECURITY.md`](../../SECURITY.md).
