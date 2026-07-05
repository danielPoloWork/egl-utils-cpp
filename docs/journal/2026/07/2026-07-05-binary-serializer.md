# 2026-07-05 — BinarySerializer (roadmap 9.2)

## What got done

- Implemented `it::d4np::util::BinarySerializer` and `BinaryDeserializer` (component #18): an
  endianness-aware, zero-allocation binary codec over caller-owned byte buffers — the second
  Milestone-9 component, and a **return to header-only** after the compiled-tier `FileStream`.
- **Design (ADR-0024): header-only, span-based, `constexpr`, value-or-error.** Serialization is
  pure byte manipulation (`std::bit_cast` + a conditional reverse), touches no OS API, so there
  is nothing to hide in a compiled tier — it stays header-only (ADR-0004) unlike its sibling
  `FileStream`. Every method is `constexpr`, so a `std::array`-backed buffer round-trips at
  compile time (proved by a `static_assert` in the test).
- **Endianness.** The wire order is a constructor argument (`std::endian`, default
  `std::endian::little`). When it equals `std::endian::native` bytes are copied verbatim;
  otherwise the fixed-width byte array is reversed. A `static_assert` rejects mixed-endian hosts
  (a whole-word reverse would be wrong there). `std::byteswap` is C++23, so the reverse is done
  with `std::ranges::reverse` on the `bit_cast` byte array — no hand-rolled swap needed.
- **Surface.** `write<T>`/`read<T>` accept `TriviallySerializable` (`std::integral ||
  std::floating_point`; floats `static_assert` an IEC-559 layout). `write_bytes`/`read_bytes`
  carry raw blocks; `read_bytes` returns a zero-copy `std::span` view aliasing the source buffer
  (spec §3 zero-copy). Aggregates are the caller's to compose field-by-field (no portable C++20
  reflection).
- **Error model.** Overflow is reported, never thrown: a write that would exceed the buffer
  writes nothing, returns `false`, and latches `overflowed()` (sticky — batch then check once);
  a read past the end returns `std::nullopt` with the cursor unmoved. Same boundary as
  `CliParser`/`JsonParser`; no programmer-error throw path (no misuse mode like a wrong-direction
  stream).

## Gotchas folded in

- **`bugprone-unchecked-optional-access` in the constexpr test.** The compile-time round-trip
  first used `read<T>().value()`; CI-grade clang-tidy flags any post-check dereference. Rewrote
  the helper to return a bool and compare whole optionals
  (`read<...>() == std::optional<...>{expected}`) — the known-reliable idiom for this repo.
- **`cppcoreguidelines-pro-bounds-pointer-arithmetic`.** The zero-copy assertion originally read
  `view->data() == buf.data() + 1`; replaced the pointer arithmetic with `&buf[1]`.
- **`modernize-use-ranges`.** CI tidy wants the ranges overloads — switched the header's
  `std::copy`/`std::copy_n`/`std::reverse` to `std::ranges::copy`/`std::ranges::reverse` over
  spans and arrays.

## Test notes

- 7 `TEST_CASE`s / 55 assertions: scalar round-trip in both byte orders, byte-for-byte wire
  layout (a known value → known bytes, big and little), float/double fidelity, `write_bytes` +
  `read_bytes` zero-copy view, writer overflow (no partial write, sticky flag, later fitting
  write still succeeds), reader truncation (`nullopt`, cursor unmoved), plus a `constexpr`
  round-trip via `static_assert`. No temp files, no Date/random.
- Local gate green: MSVC build + full suite (**200 cases / 3701 assertions** including the new
  9), `clang-format` clean, `clang-tidy` clean on the header (as a TU) and the test TU with the
  repo config. No `_WIN32`-only code, so the whole component is visible to local tidy this time.
  ASan/UBSan/TSan/Valgrind and the non-Windows cells run in CI.

## Project state

- Milestones 1–8 complete; Milestone 9 in progress (9.1 + 9.2 done; 9.3 `TcpSocket`/`TcpServer`
  remains). Version `0.0.0`; the release PR (M5–M8) is still on offer.
- CI note: Actions minutes were exhausted on this account, so recent PR checks failed at startup
  (billing, not code). Verified locally per the machine's toolchain.

## How the next session resumes

- Next roadmap item: **9.3 — `TcpSocket` / `TcpServer`** (component #17), "non-blocking async
  sockets over select/poll/epoll". Back to the compiled STATIC tier (OS sockets — `<sys/socket.h>`
  / Winsock `<winsock2.h>`, needs `ws2_32` on Windows and a `WSAStartup`/`WSACleanup` session like
  the logger's UDP sink already does). Design questions for its ADR: the readiness model
  (select/poll portable core, epoll/kqueue as Linux/BSD optimizations, IOCP is a different
  completion model — likely a portable `poll`-based readiness reactor to start), blocking vs
  non-blocking + timeouts, the value-or-error boundary (native error codes like `FileStream`), and
  how `TcpServer` accepts (one component with two roles vs two types). Reuse the handle-hiding
  idiom (`std::intptr_t` in the header) from `FileStream`/`StackTrace`.
- One PR at a time: wait for the 9.2 PR to merge before branching 9.3. Confirm the maintainer has
  merged #… (BinarySerializer) first.
