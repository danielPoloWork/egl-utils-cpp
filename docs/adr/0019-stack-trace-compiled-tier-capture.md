# ADR-0019: `StackTrace` compiled-tier capture with on-demand symbolization

- **Status:** Accepted
- **Date:** 2026-07-02
- **Deciders:** Daniel Polo (maintainer), agent session
- **Related:** spec §2 (component #21), §3 (zero runtime dependencies), roadmap 7.2,
  [ADR-0004](0004-adopt-hybrid-header-only-plus-static-build-model.md) (the compiled tier
  this component is the first real occupant of)

## Context

The spec requires `StackTrace` (component #21): "native-API stack capture". This is the
first component that genuinely needs the compiled STATIC tier ADR-0004 prepared: capture
and symbolization live on OS APIs (`CaptureStackBackTrace` + DbgHelp on Windows;
`backtrace(3)`, `dladdr`, `abi::__cxa_demangle` on Linux/macOS) whose headers
(`<windows.h>`, `<dbghelp.h>`, `<execinfo.h>`) must never leak into consumer translation
units. Further forces: `std::stacktrace` is C++23 (and needs its own link library) —
beyond the C++20 floor, which is this component's reason to exist; third-party options
(Boost.Stacktrace) violate the zero-dependency rule; capture is typically wanted in
failure paths where cheapness matters, while symbol resolution is expensive and only
needed when a human reads the trace; and DbgHelp is documented as **not thread-safe**.

## Decision

`StackTrace` is a value type declared in `stack_trace.hpp` with **every member defined in
`stack_trace.cpp`**, compiled only into `egl-util::egl-util-static` — the same contract as
`library_version()`: header-only consumers see the declarations, and using them without
the static tier is a documented link-time error. The design splits **cheap capture from
expensive symbolization**: `StackTrace::capture(skip, max_frames)` collects raw
instruction addresses only (one vector allocation; no symbol work), while `symbolize()`
and `to_string()` resolve names on demand, best-effort — a mutex serializes all DbgHelp
use on Windows (it is process-global and not thread-safe); on POSIX each address goes
through `dladdr` and `abi::__cxa_demangle`, falling back to the hex address when the
symbol is unavailable (stripped binaries, static functions). `skip` drops caller-chosen
top frames and the capture machinery always excludes itself; `max_frames` caps the walk
(default 64). Capture is **not async-signal-safe** and this is documented — the component
is a diagnostics tool, not a signal-handler crash reporter.

## Alternatives Considered

- **`std::stacktrace` (C++23)** — the eventual right answer. Rejected: absent at the
  GCC 11 / Clang 14 / MSVC 19.30 floor (and where present it drags `stdc++exp`/
  `stdc++_libbacktrace` link details). The component exists precisely to cover this gap;
  when the floor moves, an ADR can supersede this one and thin the implementation.
- **Boost.Stacktrace** — mature and portable. Rejected: the library core has a
  zero-dependency contract (spec §3).
- **Header-only with inline platform `#ifdef`s** — rejected: it drags `<windows.h>` /
  `<execinfo.h>` into every consumer TU via the umbrella header — exactly the leak
  ADR-0004 built the compiled tier to prevent.
- **Symbolize at capture time** — rejected: symbol resolution is orders of magnitude more
  expensive than the walk and often never needed (traces captured for context that is
  discarded on the happy path); it would also hold the DbgHelp mutex inside every capture.
- **`backtrace_symbols(3)` for POSIX symbolization** — simpler than
  `dladdr` + demangle. Rejected: it returns raw mangled names in one `malloc`'d block
  (manual `free`, no demangling) — worse output and worse resource hygiene than
  `dladdr` + `__cxa_demangle` under RAII.

## Consequences

- The static tier gains its first real translation unit (`stack_trace.cpp` joins
  `version.cpp`) and its first platform link requirements: `dbghelp` on Windows,
  `${CMAKE_DL_LIBS}` elsewhere — carried as PRIVATE link deps of `egl-util-static`, so
  header-only consumers still link nothing.
- Symbol quality is **best-effort by contract**: release builds, stripped binaries, and
  static functions may yield bare hex addresses. Tests therefore assert structure (frame
  counts, caps, skip behavior, formatting shape), never specific symbol names — a
  release-mode CI cell must pass identically.
- Capture cost is one vector allocation plus the OS walk; callers in hot paths can bound
  it with `max_frames`. Not async-signal-safe (documented) — a signal-safe variant would
  need pre-allocated buffers and `SymInitialize`-free formatting, a separate component if
  ever needed.
- The DbgHelp mutex serializes concurrent `symbolize()` calls on Windows — correct first,
  concurrent-fast never needed for a diagnostics path.
- Tests join `library_version_test.cpp` behind `EGL_UTIL_BUILD_STATIC` (ON in the
  project's own presets and CI; OFF for downstream header-only consumers).
- No pattern catalogue change (platform façade over OS APIs; labelling it *Facade* would
  stretch the taxonomy — noted here per AGENTS §8 rather than force-fit).

## References

- Spec §2 component #21, §3 non-functional requirements.
- ADR-0004 — the tier contract; `version.hpp`/`version.cpp` — the wiring precedent.
- MSDN: `CaptureStackBackTrace`, DbgHelp thread-safety notes; POSIX `backtrace(3)`,
  `dladdr(3)`; Itanium ABI `__cxa_demangle`.
