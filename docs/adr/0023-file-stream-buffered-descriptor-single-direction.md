# ADR-0023: `FileStream` buffered OS-descriptor wrapper, single-direction, compiled tier

- **Status:** Accepted
- **Date:** 2026-07-04
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #16), §3 (compiled tier / RAII), §5 (error model), roadmap 9.1,
  [ADR-0004](0004-adopt-hybrid-header-only-plus-static-build-model.md) (the compiled tier),
  [ADR-0019](0019-stack-trace-compiled-tier-capture.md) /
  [ADR-0020](0020-logger-async-pump-with-strategy-sinks.md) (prior OS-boundary components),
  [ADR-0021](0021-cli-parser-typed-binding-value-or-error.md) (the value-or-error convention)

## Context

The spec requires `FileStream` (component #16): "a RAII wrapper over the **OS file descriptors**
with **configurable internal buffering**". It opens Milestone 9, the I/O tier. Four decisions
shape it. **What is wrapped** — a C `std::FILE*` (portable, self-buffering) or the raw OS
descriptor (`int` fd / `HANDLE`) the spec names. **Where it lives** — header-only or the
compiled STATIC tier. **How I/O errors surface** — a file that is missing, full, or
permission-denied is ordinary runtime input, not a programmer bug. **How the buffer stays
coherent** — a single buffer that serves both reads and writes with seeking is the hard part of
stdio, and getting it wrong corrupts data.

Forces: spec §2 says "OS file descriptors" and "configurable internal buffering" — i.e. *we*
own and size the buffer, over the OS primitive, not stdio's opaque one; spec §3's RAII pillar
and the compiled-tier contract (ADR-0004) built to keep OS headers out of consumers; the
library convention that user-driven failure is reported and only programmer misuse throws
(ADR-0021); and the zero-dependency rule.

## Decision

**`FileStream` is a move-only RAII wrapper over a single raw OS descriptor with a
caller-sized owned buffer, opened in one direction (read *or* write/append), living in the
compiled STATIC tier, reporting I/O failure through a value-or-error boundary.**

- **Raw OS descriptor, type-erased in the header.** The descriptor is stored as a
  `std::intptr_t` (sentinel `-1`, matching both POSIX's `-1` and Windows'
  `INVALID_HANDLE_VALUE`); the `.cpp` reinterprets it as an `int` fd (POSIX
  `open`/`read`/`write`/`close`) or a `HANDLE` (Windows
  `CreateFileA`/`ReadFile`/`WriteFile`/`CloseHandle`). The header names no OS type — this is
  exactly the compiled-tier hiding of `StackTrace`/`Logger` (ADR-0019/0020), so `<fcntl.h>` /
  `<windows.h>` never reach consumers. Placement in the STATIC tier follows (requires
  `egl-util::egl-util-static`); file I/O needs no extra link library (libc / kernel32).
- **Owned, configurable buffer.** `open(path, mode, buffer_size)` allocates a
  `std::vector<std::byte>` of the requested size (default 8 KiB, floored at 1). Small reads and
  writes coalesce through it into few syscalls; a write at least as large as the buffer bypasses
  it (flush-then-write-through), so bulk transfers don't double-copy.
- **Single-direction per open.** A stream is read-only or write/append-only (`FileMode`). This
  sidesteps the genuinely hard problem — one buffer reconciling interleaved reads and writes
  with seeks — which is where hand-rolled buffered I/O corrupts data. Each direction has a
  simple, verifiable buffer invariant. Bidirectional/seekable I/O, if ever needed, is a separate
  component, not a bolt-on.
- **Value-or-error boundary.** A normal I/O failure is reported, never thrown: `open` returns a
  stream with `is_open() == false` and `error()` set (the platform-native code — `errno` /
  `GetLastError()`); `read` returns `std::nullopt`, `write`/`flush`/`close` return `false`.
  Reading a write-mode stream (or vice versa) is a programmer error and throws
  `std::logic_error`; operating on a *closed* stream is a graceful no-op failure, not a throw.
  This mirrors `CliParser`/`JsonParser` (ADR-0021/0022) and the library-wide rule.
- **RAII, move-only.** The destructor closes (flushing a write buffer) but cannot report a
  failed final flush — so `close()` is public and returns a status for callers who must observe
  it, and the contract says so. Move transfers the descriptor and buffer and leaves the source
  closed; copying is deleted (a descriptor has one owner).
- **No mandatory locking.** POSIX places no lock on an open file; to behave identically the
  Windows open shares read/write/delete (`FILE_SHARE_*`). The wrapper never locks — concurrent
  access is the caller's contract. Interrupted syscalls (`EINTR`) are retried on POSIX; `close`
  treats `EINTR` as success (POSIX.1-2008: the descriptor is already gone).

No design pattern is adopted: this is the RAII idiom (as with `UniqueRef`/`HeapArray`), thinly
adapting an OS descriptor to a typed C++ surface. The patterns catalogue is unchanged.

## Alternatives Considered

- **Wrap `std::FILE*` (or `std::fstream`)** — rejected: the spec asks for OS file descriptors
  and a *configurable internal buffer*, whereas stdio owns an opaque buffer (`setvbuf` is
  coarse) and iostreams bury errors in stream state. Wrapping the raw descriptor is what
  demonstrates the compiled tier and gives full control of buffering — the component's reason to
  exist.
- **Header-only over `FILE*`** — rejected for the same reason, and because it would forgo the
  ADR-0004 tier that this milestone exists to exercise.
- **A single bidirectional, seekable buffered stream** — rejected (deferred): coherent
  read/write buffering across `seek` is the error-prone core of stdio; single-direction streams
  cover the overwhelming majority of uses with a buffer invariant that is easy to verify.
  Seeking / random access can be a future roadmap item layered on the descriptor.
- **Throw on I/O error** — rejected: a missing or unreadable file is expected at runtime; a tool
  wants to branch on the failure, not unwind. Only wrong-*direction* use (a true bug) throws.
- **Exclusive (locked) open** — rejected as a surprising cross-platform default: POSIX doesn't
  lock, so Windows shares to match. Advisory/mandatory locking, if needed, is separate.
- **A third-party I/O library** — rejected: zero-dependency contract (spec §3).

## Consequences

- Consumers link nothing extra beyond the STATIC tier; the OS I/O headers stay out of their
  translation units. Header-only consumers see declarations only — calls are a link error (same
  contract as `library_version()`).
- Buffering is the caller's to size; small-op-heavy workloads get few syscalls, bulk writes skip
  the copy. The single-direction rule means "open for what you'll do"; mixing throws early and
  loudly.
- The destructor's silent-flush-failure is a real hazard for write streams — mitigated by a
  status-returning `close()` and documented prominently. `flush()` lets a long-lived writer
  checkpoint durably.
- Cross-platform behaviour is uniform: `-1` sentinel, POSIX-style sharing, `EINTR` retries. The
  Windows and POSIX branches are exercised only on their own CI cells; the OS-boundary casts
  carry the same NOLINT rationale as `StackTrace`.
- Text vs binary: the wrapper is byte-oriented (no newline translation); `read_line` folds a
  trailing `\r` so CRLF files parse cleanly, but writes are verbatim.
- Patterns catalogue: no change (RAII idiom, not a GoF pattern).

## References

- Spec §2 component #16, §3 (RAII, compiled tier, zero-dependency), §5 error model.
- ADR-0004 (hybrid tier), ADR-0019/0020 (OS-boundary components and the handle-hiding idiom),
  ADR-0021/0022 (value-or-error boundary).
- POSIX `open(2)`/`read(2)`/`write(2)`/`close(2)` and `EINTR` semantics (POSIX.1-2008);
  Win32 `CreateFileA`/`ReadFile`/`WriteFile`/`CloseHandle` and `FILE_SHARE_*`.
