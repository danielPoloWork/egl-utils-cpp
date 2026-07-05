# Threat Model

The security posture of `egl-util-cpp`: what is trusted, what is not, the attack surface of the
components that consume untrusted input, and how each risk is mitigated and tested. Vulnerability
*reporting* is in [`SECURITY.md`](../../SECURITY.md); the testing decisions here are recorded in
[ADR-0031](../adr/0031-security-fuzzing-and-hash-scoping.md).

## Scope & trust boundary

`egl-util-cpp` is a library linked into a consumer's process — it has no network listener, no
privileged mode, and no persistent state of its own. The trust boundary is therefore **the data a
consumer feeds to a component**, not a remote endpoint. Most components operate only on data the
consumer already owns (containers, allocators, strings, concurrency, diagnostics) and are out of
scope for an *input* threat model: their contract is memory-safety and correctness, verified by
tests + ASan/UBSan/TSan/Valgrind.

Three components are explicitly designed to be fed **attacker-controlled bytes** and are the focus
of this model:

| Component | Untrusted input | Trusted |
|---|---|---|
| `JsonParser` (#23) | the JSON document bytes | the `MaxDepth` template parameter, the caller's buffer lifetime |
| `CliParser` (#22) | the argv tokens | the registered option specs |
| `BinaryDeserializer` (#18) | the serialized byte buffer | the read schema the caller applies |

Explicitly **out of cryptographic scope:** the hash functions (see *Hashes* below).

## Attack surface & mitigations

### JsonParser — malformed / adversarial JSON

- **Deep nesting → stack overflow.** *Mitigated by design:* nesting is tracked in a fixed
  `std::array<Frame, MaxDepth>` (no recursion, no heap); input deeper than `MaxDepth` is a
  data-driven `error`, not a crash (ADR-0022).
- **Malformed bytes / bad escapes / truncation → UB or throw.** *Mitigated:* strict RFC 8259
  grammar; a sticky, positioned `error` event; **parsing never throws and never allocates during
  the walk** (ADR-0022, ADR-0029). `decode_string` (the `\uXXXX` + UTF-16 surrogate decoder) is the
  sharpest edge and is the primary fuzz target.
- **Huge input → resource exhaustion.** Bounded by the single pass over the caller's buffer; the
  parser adds no amplification (zero-copy, no DOM).

### CliParser — hostile argv

- **Unknown options, missing/extra values, combined short flags, `--` handling → UB or throw.**
  *Mitigated:* `parse` never throws on user input; it returns a `ParseResult` with a machine code
  and message; bound variables keep their defaults on failure (ADR-0021). The tokenizer
  (`--k=v`, `-abc`, attached `-nvalue`) is the fuzz target.

### BinaryDeserializer — truncated / oversized buffers

- **Read past the end / oversized length prefix → out-of-bounds read.** *Mitigated:* every `read<T>`
  returns `std::nullopt` past the end and `read_bytes(n)` bounds-checks `n` against the remaining
  buffer, returning a view (never a copy, never past-the-end) (ADR-0024). No throw. The mixed
  scalar/`read_bytes` sequence over arbitrary bytes is the fuzz target.

## Hashes — non-cryptographic scope

`fnv1a_32/64`, `murmur3_x86_32`, and **SHA-256** are **integrity/checksum utilities, not
cryptographic primitives** (ADR-0031). SHA-256 is offered for content-addressing, deduplication,
corruption detection, and standards-conformant interop/test vectors. It is **not** for passwords,
MACs/HMAC, signatures, or any secret-dependent decision: the `constexpr`, byte-oriented
implementation is **not constant-time** and is not side-channel hardened. Conformance to NIST
FIPS 180-4 is pinned by test vectors (empty, `"abc"`, the 56-byte two-block message, and the
one-million-`'a'` message).

## Verification

- **Fuzzing.** Coverage-guided libFuzzer harnesses (`src/fuzz/…`) drive each untrusted-input
  component under ASan + UBSan; the asserted invariant is *never crash, never UB, never throw on
  input*. A short smoke run gates every PR (CI `fuzz` job); deeper campaigns run out-of-band and
  found inputs become regression corpus / `docs/bugs/` entries. See
  [ADR-0031](../adr/0031-security-fuzzing-and-hash-scoping.md).
- **Sanitizers.** The full suite runs under ASan, UBSan, TSan, and Valgrind in CI (AGENTS §10).
- **Hash conformance.** NIST FIPS 180-4 vectors as `static_assert` (compile-time) + a runtime test.
- **Reporting.** Private disclosure per [`SECURITY.md`](../../SECURITY.md).
