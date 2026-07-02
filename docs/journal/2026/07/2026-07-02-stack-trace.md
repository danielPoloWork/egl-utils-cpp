# 2026-07-02 — StackTrace (roadmap 7.2)

## What got done

- Implemented `it::d4np::util::StackTrace` (component #21): native-API stack capture, and
  **the first real occupant of the ADR-0004 compiled STATIC tier** — declarations in
  `stack_trace.hpp`, every definition in `stack_trace.cpp`, so `<windows.h>`/`<dbghelp.h>`/
  `<execinfo.h>` never reach consumer TUs. Same consumer contract as `library_version()`:
  header-only users see declarations; calls need `egl-util::egl-util-static` or fail at
  link time (documented on the type).
- **Design (ADR-0019): cheap capture / expensive symbolization split.** `capture(skip,
  max_frames)` stores raw addresses only (one vector allocation; excludes its own frames;
  clamped at 1024 against native API limits); `symbolize()`/`to_string()` resolve names on
  demand, best-effort. Windows: `CaptureStackBackTrace` + DbgHelp `SymFromAddr` — DbgHelp
  is process-global and **not thread-safe**, so one internal mutex serializes all use.
  POSIX: `backtrace(3)` + `dladdr` + `abi::__cxa_demangle` (malloc'd result adopted into a
  `unique_ptr` with `std::free` deleter), hex-address fallback. `std::stacktrace` is C++23
  (beyond the floor — the component's reason to exist); Boost violates zero-dependency.
- Build wiring: `stack_trace.cpp` joins `version.cpp` in `egl-util-static`; the tier now
  links `dbghelp` (Windows) / `${CMAKE_DL_LIBS}` (POSIX) as PRIVATE deps. Tests join
  `library_version_test.cpp` behind `EGL_UTIL_BUILD_STATIC` (ON in project presets/CI).
- Not async-signal-safe — documented; a signal-safe crash reporter would be a separate
  component. No pattern catalogue change (noted in the ADR).

## Test notes

- **Assertions are structural only** (counts, caps, skip bounds, `0x` prefixes, one line
  per frame): symbol *names* are best-effort by contract, and the same test binary runs in
  release/stripped CI cells where names may be absent. The two reinterpret_casts required
  by the OS APIs (`uintptr_t` rendering, DbgHelp's byte-buffer-as-SYMBOL_INFO idiom) carry
  targeted NOLINTs with rationale — the banned-cast rule meets its legitimate exception at
  the OS boundary, which is precisely why this code is quarantined in the compiled tier.
- Windows branch is not linted by CI tidy (Linux host) — reviewed by hand; the POSIX
  branch is the linted one.

## Project state

- Milestones 1–6 complete; Milestone 7 in progress (7.1, 7.2 done; only 7.3 Logger
  remains). Version `0.0.0`; release PR (M5+M6) still on offer.

## How the next session resumes

- Next roadmap item: **7.3 — `Logger`** (component #20), "asynchronous multi-sink logger:
  console, file, UDP" — the milestone closer and by far the largest component yet. Spec
  §3 lists the *async Logger* in the compiled tier (UDP sink needs sockets). Design
  questions for the ADR: architecture (a background thread draining a queue — ThreadPool
  or a dedicated thread? dedicated is the norm: ordering + no priority semantics), the
  record pipeline (format at call site with StringFormatter vs in the sink thread),
  sink abstraction (the *Strategy* pattern candidate — check the taxonomy), levels +
  compile-time/runtime filtering, backpressure policy when the queue is full (drop vs
  block — CircularBuffer/LockFreeQueue precedent), UDP sink layering (may want to reuse
  or pre-empt TcpSocket's platform socket plumbing from 9.3 — decide whether a minimal
  internal UDP shim lands now, scoped to the compiled tier), and flush/shutdown semantics
  (drain like ThreadPool). Consider splitting: sinks + sync core vs async pump. Closing
  7.3 completes Milestone 7 → offer the release PR again (M5–M7 would then be pending).
- One PR at a time: wait for the 7.2 PR to merge before branching 7.3.
