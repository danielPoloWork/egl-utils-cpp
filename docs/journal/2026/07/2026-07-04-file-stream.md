# 2026-07-04 — FileStream (roadmap 9.1)

## What got done

- Implemented `it::d4np::util::FileStream` (component #16): a move-only RAII wrapper over a
  single OS file descriptor with a caller-sized owned buffer — the first Milestone-9 component
  and a return to the **compiled STATIC tier** (ADR-0004) after two header-only parsing items.
- **Design (ADR-0023): raw descriptor, single-direction, value-or-error.** The spec says "OS
  file descriptors" + "configurable internal buffering", so this wraps the raw descriptor (POSIX
  `open`/`read`/`write`/`close`, Windows `CreateFileA`/`ReadFile`/`WriteFile`/`CloseHandle`), not
  `std::FILE*`. The descriptor is type-erased behind an `std::intptr_t` in the header (sentinel
  `-1` == POSIX -1 == `INVALID_HANDLE_VALUE`), so the OS headers stay in the `.cpp` — the same
  handle-hiding as `StackTrace`/`Logger`. A stream is opened read-only or write/append-only
  (`FileMode`), which keeps the buffer single-direction and sidesteps the corruption-prone core
  of bidirectional buffered+seekable I/O (deferred as a possible future component).
- **Buffering.** `open(path, mode, buffer_size)` owns a `std::vector<std::byte>` (default 8 KiB).
  Small reads/writes coalesce through it; a write ≥ buffer size flushes then writes through
  (no double copy). `read_line` folds a trailing `\r` so CRLF files parse cleanly.
- **Error model.** Normal I/O failure is reported, never thrown: `open` failure → closed stream
  + `error()` (native `errno`/`GetLastError()`), `read`→`nullopt`, `write`/`flush`/`close`→
  `false`. Wrong-direction use (read a write stream, or vice versa) is a programmer error →
  `std::logic_error`; a *closed* stream fails gracefully. Same boundary as `CliParser`/
  `JsonParser`. Move-only; the destructor closes but can't report a failed final flush — so
  `close()` returns a status and the contract says to call it.
- **Cross-platform.** No mandatory locking (Windows shares read/write/delete to match POSIX);
  `EINTR` retried; `close` treats `EINTR` as success (POSIX.1-2008: fd already gone). Static tier
  gains its fourth TU (`file_stream.cpp`) — no new link library (libc / kernel32).

## Gotchas folded in

- **Windows file sharing surfaced in tests.** Tests that hold a read handle and reopen the same
  path for writing failed on Windows with a sharing violation (works on POSIX — no mandatory
  lock). Fixed at the source: `os_open` now shares read/write/delete, so behaviour matches POSIX;
  also tightened one test to `close()` before reopening. Documented in the ADR.
- **OS-boundary casts.** The three `reinterpret_cast<HANDLE>` sites trip both
  `cppcoreguidelines-pro-type-reinterpret-cast` and `performance-no-int-to-ptr` — unavoidable
  when type-erasing a handle behind an integer field; NOLINT with rationale, as `StackTrace` does.

## Test notes

- 11 `TEST_CASE`s / 62 assertions: RAII/move lifecycle, the three modes, write↔read round-trip,
  missing-file open failure, truncate-vs-append, data larger than the buffer (bypass + multi-fill
  with a 64-byte buffer), exact-length reads short only at EOF, `read_line` (LF, CRLF, last
  unterminated line), wrong-direction throws, closed-stream graceful failure, idempotent close +
  buffered-write flush. Tests write to unique temp files removed on scope exit (a per-run counter,
  no Date/random).
- Local gate green: MSVC build + full suite (200 cases / 3704 assertions), `clang-format` clean,
  `clang-tidy` clean on `file_stream.cpp` + header + test TU. **The POSIX branch is invisible to
  local tidy** (Windows host takes the `_WIN32` path) — hand-reviewed against the known CI-only
  checks (concise `#ifdef`, the `::open` vararg NOLINT, no int-to-ptr on POSIX). ASan/UBSan/
  Valgrind and the POSIX/macOS cells run in CI.

## Project state

- Milestones 1–8 complete; Milestone 9 in progress (9.1 done; 9.2 `BinarySerializer` and 9.3
  `TcpSocket`/`TcpServer` remain). Version `0.0.0`; the release PR (M5–M8) is still on offer.

## How the next session resumes

- Next roadmap item: **9.2 — `BinarySerializer`** (component #18), "endianness-aware binary
  serializer". Design questions for its ADR: header-only vs compiled tier (it's pure computation
  — byte packing — so header-only like the parsers, unless it grows OS deps; likely header-only),
  the byte-order policy (serialize to a fixed on-the-wire endianness — little vs big — converting
  from host; C++20 `std::endian` detects host, `std::byteswap` is C++23 so provide a constexpr
  swap), the sink/source abstraction (write into a growable buffer / `StringBuilder`-like vs a
  `std::span`; read from a `std::span<const std::byte>` with a value-or-error cursor like
  `JsonParser`), which types are supported (integers, floating point via bit_cast, spans/strings
  with a length prefix), and alignment/`bit_cast` for floats. It can reuse the value-or-error
  cursor idiom for the read side. Header-only keeps it out of the static tier.
- One PR at a time: wait for the 9.1 PR to merge before branching 9.2.
