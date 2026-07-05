# ADR-0025: `TcpSocket` / `TcpServer` non-blocking sockets, poll-based readiness, compiled tier

- **Status:** Accepted
- **Date:** 2026-07-05
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #17), §3 (RAII / compiled tier / zero-dependency), §5 (error model),
  roadmap 9.3 (closes Milestone 9),
  [ADR-0004](0004-adopt-hybrid-header-only-plus-static-build-model.md) (the compiled tier),
  [ADR-0023](0023-file-stream-buffered-descriptor-single-direction.md) (the handle-hiding idiom
  and the value-or-error boundary for OS I/O),
  [ADR-0020](0020-logger-async-pump-with-strategy-sinks.md) (the UDP sink's Winsock precedent)

## Context

The spec requires `TcpSocket` / `TcpServer` (component #17): "non-blocking asynchronous classes
based on select/poll or epoll for network communication". It closes Milestone 9, the I/O tier.
Several decisions shape it. **What is wrapped** — the raw OS socket (`int` fd / Winsock
`SOCKET`), as for `FileStream`. **Where it lives** — the compiled STATIC tier (the socket headers
must stay out of consumers). **What readiness primitive** — the spec names "select/poll or
epoll"; epoll/kqueue/IOCP are per-OS. **How errors and the non-blocking third outcome surface** —
a refused connection or a socket that "would block" are ordinary runtime outcomes, not bugs.

Forces: spec §3's RAII pillar and the compiled-tier contract (ADR-0004) that keeps
`<winsock2.h>` / `<sys/socket.h>` out of consumers; the library convention that user-driven
failure is *reported* and only programmer misuse throws (ADR-0023); the zero-dependency rule
(no libuv/asio); and portability across Linux, macOS, and Windows on the CI matrix.

## Decision

**`TcpSocket` and `TcpServer` are move-only RAII wrappers over a raw OS socket, living in the
compiled STATIC tier, non-blocking by default, with a portable one-descriptor `poll` readiness
model and a value-or-error boundary.**

- **Raw socket, type-erased in the header.** The descriptor is a `std::intptr_t` (sentinel `-1`,
  matching POSIX `-1` and Windows `INVALID_SOCKET`); the `.cpp` casts it back to an `int` fd or a
  `SOCKET`. The header names no OS type — the same handle-hiding as `FileStream`/`StackTrace`
  (ADR-0023/0019) — so the socket headers never reach consumers. Placement in the STATIC tier
  needs no new link library on POSIX; Windows links `ws2_32`, already pulled in for the logger's
  UDP sink (ADR-0020).
- **Portable poll readiness (`::poll` / `::WSAPoll`).** A single-descriptor poll backs
  `wait_readable` / `wait_writable` (and `TcpServer::wait_readable` for a pending connection),
  returning `WaitResult` (`ready` / `timed_out` / `error`). This is the portable "select/poll"
  core the spec allows; epoll/kqueue/IOCP are per-OS scaling optimizations deferred to a future
  item (the API — readiness + explicit waits — does not change if they are added later).
- **`select` for connect completion.** Non-blocking `connect` returns immediately (`is_open()`
  true, possibly still completing); `wait_connected` resolves it. It uses `select` rather than
  `poll` here on purpose: Windows' `WSAPoll` famously does **not** report a failed connection,
  whereas `select`'s `exceptfds` does (and a resolved connect shows up in `writefds` on POSIX
  regardless). The caller-visible `SO_ERROR` then separates success from failure.
- **Value-or-error boundary with an explicit non-blocking outcome.** `connect`/`listen` failure
  yields a closed object with `error()` set (native `errno`/`WSAGetLastError()`); `send`/`recv`
  return an `IoResult` whose `IoStatus` separates `ok` / `closed` (orderly peer shutdown) /
  `would_block` / `error`; `accept` returns `std::nullopt` for both "nothing pending"
  (`error() == 0`) and a real error (`error() != 0`). Nothing throws — mirroring
  `FileStream`/`CliParser`/`JsonParser`. Using a closed socket fails gracefully.
- **RAII, move-only.** The destructor closes the descriptor; move transfers it and leaves the
  source closed; copying is deleted. `TcpServer::accept` explicitly sets the accepted socket's
  blocking mode (POSIX does not inherit it) so it matches the listener.
- **Windows Winsock is process-lifetime.** `WSAStartup` is called once via `std::call_once` and
  never paired with `WSACleanup`: its state is released at process exit, and this avoids a
  refcount race across socket moves. The whole init is Windows-only; the sanitizer jobs run the
  POSIX no-op path.
- **Server is IPv4, `SO_REUSEADDR`, ephemeral-aware.** `TcpServer::listen` binds `INADDR_ANY` on
  the given port (0 ⇒ an OS-assigned port, read back via `local_port()`), with `SO_REUSEADDR` so a
  restart need not wait out `TIME_WAIT`. The client `connect` resolves names with `getaddrinfo`
  (IPv4/IPv6). Dual-stack server binding is a straightforward future extension.

No design pattern is adopted: this is the RAII idiom over an OS handle plus a thin readiness
helper. **Reactor is explicitly rejected** (see the catalogue): there is no event demultiplexer
dispatching to registered handlers — `wait_*` are synchronous one-shot readiness checks a caller
drives, not an event loop.

## Alternatives Considered

- **epoll / kqueue / IOCP now** — rejected (deferred): epoll is Linux-only, kqueue BSD/macOS,
  IOCP a different (completion, not readiness) model on Windows. A portable `poll` core covers the
  spec's "select/poll or epoll" and the common case; the per-OS backends are a scaling
  optimization that can be added behind the same readiness API without a break.
- **`poll`/`WSAPoll` for connect completion too** — rejected: `WSAPoll` does not surface a failed
  connect on Windows (a documented limitation), so a refused connection would hang until timeout.
  `select` with `exceptfds` reports it on both platforms.
- **Blocking, thread-per-connection** — rejected as the default: the spec asks for non-blocking
  async classes. Blocking mode is still available (`non_blocking = false` / `set_non_blocking`).
- **Throw on network error** — rejected: a refused connection, a reset, or a would-block are
  expected runtime conditions to branch on, not to unwind. Consistent with the library boundary.
- **`std::optional<std::size_t>` for `recv` (as `FileStream::read`)** — rejected: TCP
  non-blocking I/O has a genuine four-way outcome (bytes / orderly close / would-block / error)
  that an optional cannot express without overloading `error()`; `IoResult` states it plainly.
- **A third-party async library (asio/libuv)** — rejected: zero-dependency contract (spec §3).

## Consequences

- Consumers link the STATIC tier (Windows also gets `ws2_32` transitively); the socket headers
  stay out of their translation units. Header-only consumers see declarations only — calls are a
  link error (same contract as `library_version()`).
- The readiness model is explicit: a caller polls with `wait_*` then does one non-blocking
  `send`/`recv`/`accept`. Building an event loop on top is the caller's choice; the component does
  not impose one (and is not yet a Reactor).
- `WSAPoll`'s connect blindness is contained in `wait_connected` via `select`; the rest of the
  surface uses `poll`.
- IPv6 server binding, dual-stack, and an epoll/kqueue/IOCP fast path are natural future roadmap
  items; none change the public API.
- Windows never calls `WSACleanup`; acceptable because the state is process-scoped and released
  at exit, and the sanitizer/leak jobs exercise the POSIX path.
- Patterns catalogue: a *Rejected* row (Reactor) is added; no adoption.

## References

- Spec §2 component #17, §3 (RAII, compiled tier, zero-dependency), §5 error model.
- ADR-0004 (hybrid tier), ADR-0023 (handle-hiding + value-or-error for OS I/O), ADR-0020 (Winsock
  precedent).
- POSIX `socket`/`connect`/`bind`/`listen`/`accept`/`poll`/`select`/`getsockopt` (POSIX.1-2008);
  Win32 Winsock `WSAPoll` connect-notification limitation, `select` `exceptfds` semantics.
