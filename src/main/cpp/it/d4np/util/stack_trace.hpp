// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// StackTrace (component #21): native-API stack capture with on-demand symbolization.
// Declarations only — every member is defined in stack_trace.cpp, compiled into the
// optional STATIC tier (egl-util::egl-util-static, ADR-0004), so the OS headers
// (<windows.h>/<dbghelp.h>, <execinfo.h>/<dlfcn.h>) never leak into consumer translation
// units. See ADR-0019 for the capture/symbolize split and platform choices.
#ifndef IT_D4NP_UTIL_STACK_TRACE_HPP
#define IT_D4NP_UTIL_STACK_TRACE_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace it::d4np::util {

/// A captured call stack: cheap raw-address capture now, expensive best-effort
/// symbolization only when asked (ADR-0019).
///
/// `capture()` walks the current thread's stack via the native API
/// (`CaptureStackBackTrace` on Windows, `backtrace(3)` on POSIX) and stores instruction
/// addresses only. `symbolize()`/`to_string()` resolve names on demand — best-effort by
/// contract: release builds, stripped binaries, and static functions may yield bare hex
/// addresses.
///
/// @note Requires linking `egl-util::egl-util-static`; header-only consumers see these
/// declarations but calls are an unresolved-symbol error at link time (same contract as
/// `library_version()`). Capture is **not async-signal-safe**. A StackTrace value is
/// immutable after capture and safe to copy across threads; `symbolize()` may be called
/// concurrently (DbgHelp access is serialized internally on Windows).
class StackTrace {
  public:
    /// One resolved frame: the instruction address and its best-effort description.
    struct Frame {
        const void *address;
        std::string description; ///< demangled name when available, else the hex address
    };

    /// Default cap on the number of captured frames.
    static constexpr std::size_t default_max_frames = 64;

    /// Captures the current thread's stack. The capture machinery excludes itself;
    /// `skip` drops that many additional top frames (your own wrappers), and at most
    /// `max_frames` frames are collected.
    [[nodiscard]] static StackTrace capture(std::size_t skip = 0, std::size_t max_frames = default_max_frames);

    /// The captured instruction addresses, outermost call last.
    [[nodiscard]] const std::vector<const void *> &addresses() const noexcept { return addresses_; }

    [[nodiscard]] std::size_t size() const noexcept { return addresses_.size(); }
    [[nodiscard]] bool empty() const noexcept { return addresses_.empty(); }

    /// Resolves every address to a Frame (expensive; allocates; best-effort names).
    [[nodiscard]] std::vector<Frame> symbolize() const;

    /// Multi-line rendering: one `#index 0xaddress description` line per frame.
    [[nodiscard]] std::string to_string() const;

  private:
    explicit StackTrace(std::vector<const void *> addresses) : addresses_(std::move(addresses)) {}

    std::vector<const void *> addresses_;
};

} // namespace it::d4np::util

#endif // IT_D4NP_UTIL_STACK_TRACE_HPP
