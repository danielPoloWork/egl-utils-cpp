# ADR-0024: `BinarySerializer` endianness-aware, header-only, span-based codec

- **Status:** Accepted
- **Date:** 2026-07-05
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #18), §3 (zero-copy / compile-time / zero-dependency),
  §5 (error model), roadmap 9.2,
  [ADR-0023](0023-file-stream-buffered-descriptor-single-direction.md) (the sibling I/O
  component, compiled tier),
  [ADR-0021](0021-cli-parser-typed-binding-value-or-error.md) /
  [ADR-0022](0022-json-parser-non-allocating-pull-events.md) (the value-or-error convention)

## Context

The spec requires `BinarySerializer` (component #18): an "endianness-aware binary serializer".
Four decisions shape it. **Where it lives** — the compiled STATIC tier (like its Milestone-9
sibling `FileStream`) or header-only. **What byte order it speaks** — fixed, or chosen by the
caller, and detected how. **What it writes into** — an owned growable buffer or a caller-owned
one. **How a full buffer surfaces** — running out of room is ordinary runtime input, not a
programmer bug.

Forces: spec §3's zero-copy and *compile-time optimization* pillars, the zero-dependency rule,
and the library-wide convention that user-driven failure is *reported* and only programmer
misuse throws (ADR-0021/0022). Unlike `FileStream`, serialization touches **no OS API** — it is
pure byte manipulation — so the compiled tier buys nothing here.

## Decision

**`BinarySerializer` (writer) and `BinaryDeserializer` (reader) are a header-only pair over a
caller-owned byte buffer (`std::span`), with a wire byte order chosen at construction and a
value-or-error boundary. Every operation is `constexpr`.**

- **Header-only, not the compiled tier.** The codec is `std::bit_cast` plus a conditional byte
  reverse — no OS headers to hide, so there is no reason to force consumers to link the STATIC
  tier (contrast `FileStream`, ADR-0023). It joins the header-only default (ADR-0004) alongside
  the containers and parsers, and every method is `constexpr`, so a `std::array`-backed buffer
  can be filled and read back at compile time (the §3 compile-time pillar).
- **Endianness via `std::endian` + a conditional reverse.** The target order is a constructor
  argument (`std::endian`, default `std::endian::little` — the common file-format convention).
  When it equals `std::endian::native` the bytes are copied verbatim; otherwise the fixed-width
  byte array is reversed. A single `static_assert` rejects mixed-endian (PDP) hosts, where a
  whole-word reverse would be wrong; on every real little- or big-endian host the reverse is
  exact and branch-predictable.
- **Caller-owned, fixed buffer — zero allocation.** The writer serializes into a
  `std::span<std::byte>` the caller sizes; the reader consumes a `std::span<const std::byte>`.
  Nothing is owned or grown. This matches the controlled-allocation philosophy (`StackAllocator`,
  `HeapArray`) and keeps the type usable in constrained contexts. `read_bytes` returns a
  zero-copy `std::span` view aliasing the source buffer (spec §3 zero-copy), not a copy.
- **Value-or-error boundary.** A write that would exceed the buffer writes nothing, returns
  `false`, and latches `overflowed()` (sticky, so a caller can batch writes and check once at the
  end); a read past the end returns `std::nullopt` with the cursor unmoved, so a truncated buffer
  is branchable. Neither throws — mirroring `CliParser`/`JsonParser`. There is **no** programmer-
  error throw path: the type has no misuse mode analogous to `FileStream`'s wrong-direction use.
- **Scalar surface via a concept.** `write<T>` / `read<T>` accept `TriviallySerializable`
  (`std::integral || std::floating_point`); floats additionally `static_assert` an IEC-559
  layout so the wire format is portable. Aggregates are serialized field by field by the caller
  (deliberately — a reflection-free library cannot walk struct members portably), and raw blocks
  go through `write_bytes` / `read_bytes`.

No design pattern is adopted; **Memento is explicitly rejected** (see the catalogue): the codec
externalizes caller-supplied *primitives* into a flat byte cursor, with no originator/caretaker
roles and no opaque, encapsulation-preserving state object — labelling it Memento would be a
force-fit (AGENTS §8).

## Alternatives Considered

- **Compiled STATIC tier (like `FileStream`)** — rejected: there is no OS header to keep out of
  consumers, and the tier would forfeit the `constexpr` compile-time round-trip. Header-only is
  the default (ADR-0004); the tier is for OS-boundary code only.
- **Fixed byte order (always little, or always network/big)** — rejected: file formats are
  little-endian, network protocols big-endian; a caller-chosen order serves both at no runtime
  cost when it matches the host.
- **Owned, growable buffer (`std::vector`)** — rejected as the primary surface: it forces
  allocation and an ownership story the caller often does not want. A span over caller memory is
  the zero-allocation core; a growable adapter can layer on later if a roadmap item needs it.
- **Throw on overflow / underflow** — rejected: a short buffer is expected runtime input (a
  partial network read, a size-bounded record), to be branched on, not unwound. Consistent with
  the library-wide boundary (ADR-0021/0022).
- **Templating the byte order (`BinarySerializer<std::endian::big>`)** — rejected: it would let
  the swap fold away at compile time, but the swap is already trivial and branch-predictable, and
  a runtime order keeps the type usable when the order is only known at runtime (e.g. read from a
  file header). The `constexpr` methods still constant-fold when the order is a constant.
- **Reflection-based whole-struct serialization** — rejected: not portable in C++20 without
  macros or code generation; explicit field-by-field writes keep the layout auditable and the
  library dependency-free.

## Consequences

- Consumers include one header, link nothing, and can serialize at compile time. The codec is
  usable in `constexpr` and freestanding-ish contexts (no allocation, no exceptions on the hot
  path).
- The wire format is exactly "fields, in call order, each `sizeof(T)` bytes in the chosen order";
  it is the caller's contract to write and read the same sequence and order. There is no
  self-describing framing, versioning, or type tagging — those belong to a higher layer if
  needed.
- Aggregates and length-prefixed strings are the caller's to compose from scalar writes and
  `write_bytes`; the library does not impose a container framing.
- Mixed-endian hosts are unsupported by a compile-time `static_assert` rather than silently
  mis-serialized.
- Patterns catalogue: a *Rejected* row (Memento) is added; no adoption.

## References

- Spec §2 component #18, §3 (zero-copy, compile-time, zero-dependency), §5 error model.
- ADR-0004 (header-only default vs compiled tier), ADR-0023 (`FileStream`, the compiled-tier
  sibling), ADR-0021/0022 (value-or-error boundary).
- `std::endian`, `std::bit_cast` (C++20, `<bit>`); IEC 559 / IEEE 754 floating-point layout.
