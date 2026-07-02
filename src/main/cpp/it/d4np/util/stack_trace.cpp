// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Compiled tier of StackTrace (component #21, ADR-0019). This translation unit owns the
// OS-API surface — CaptureStackBackTrace + DbgHelp on Windows, backtrace(3) + dladdr +
// __cxa_demangle on POSIX — so those headers never reach consumers. Windows note: DbgHelp
// is process-global and not thread-safe; every use is serialized by one mutex here.
#include <it/d4np/util/stack_trace.hpp>

#include <it/d4np/util/string_builder.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
// Keep windows.h from defining the min/max macros (they break std::min) and from dragging
// in the rarely-needed subsystem headers. This is a .cpp, so the defines leak to no one.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// dbghelp.h requires windows.h to be included first; keep this order.
#include <dbghelp.h>

#include <mutex>
#else
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>

#include <cstdlib>
#include <memory>
#endif

namespace it::d4np::util {

namespace {

/// Renders `address` as 0x-prefixed lowercase hex into `out`.
void append_address(StringBuilder &out, const void *address) {
    std::array<char, 18> digits{}; // 16 hex digits for 64-bit + margin
    char *const first = digits.data();
    char *const last = std::next(first, static_cast<std::ptrdiff_t>(digits.size()));
    const auto value = reinterpret_cast<std::uintptr_t>(address); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    const std::to_chars_result result = std::to_chars(first, last, value, 16);
    out.append("0x");
    if (result.ec == std::errc{}) {
        out.append(std::string_view(first, static_cast<std::size_t>(std::distance(first, result.ptr))));
    }
}

/// The hex-address fallback used whenever no symbol is available.
std::string address_only(const void *address) {
    StringBuilder builder{20};
    append_address(builder, address);
    return std::move(builder).str();
}

#ifdef _WIN32

/// DbgHelp is process-global and documented not thread-safe: one mutex serializes
/// initialization and every SymFromAddr call (ADR-0019).
std::mutex &dbghelp_mutex() {
    static std::mutex mutex;
    return mutex;
}

/// Initializes the process's symbol handler once. Requires dbghelp_mutex() held.
bool symbols_initialized() {
    static const bool initialized = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
    return initialized;
}

std::string describe_address(const void *address) {
    const std::scoped_lock lock{dbghelp_mutex()};
    if (!symbols_initialized()) {
        return address_only(address);
    }
    constexpr std::size_t max_name_length = 256;
    alignas(SYMBOL_INFO) std::array<unsigned char, sizeof(SYMBOL_INFO) + max_name_length> storage{};
    // SYMBOL_INFO ends in a flexible name array; DbgHelp's documented usage is a byte
    // buffer viewed as SYMBOL_INFO. NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = max_name_length - 1;

    DWORD64 displacement = 0;
    const auto target = reinterpret_cast<DWORD64>(address); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    if (SymFromAddr(GetCurrentProcess(), target, &displacement, symbol) == FALSE) {
        return address_only(address);
    }

    StringBuilder builder{64};
    append_address(builder, address);
    builder.append(' ').append(std::string_view{static_cast<const char *>(symbol->Name)});
    builder.append(" + ").append(static_cast<std::uint64_t>(displacement));
    return std::move(builder).str();
}

std::vector<const void *> capture_addresses(std::size_t skip, std::size_t max_frames) {
    // +1 skips capture_addresses/capture themselves.
    std::vector<void *> raw(max_frames);
    const USHORT collected =
        CaptureStackBackTrace(static_cast<DWORD>(skip + 1), static_cast<DWORD>(max_frames), raw.data(), nullptr);
    return {raw.begin(), std::next(raw.begin(), static_cast<std::ptrdiff_t>(collected))};
}

#else

std::string describe_address(const void *address) {
    Dl_info info{};
    if (dladdr(address, &info) == 0 || info.dli_sname == nullptr) {
        return address_only(address);
    }

    // __cxa_demangle returns a malloc'd buffer; adopt it under RAII immediately.
    int status = 0;
    const std::unique_ptr<char, decltype(&std::free)> demangled{
        abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status), &std::free};
    const std::string_view name =
        (status == 0 && demangled) ? std::string_view{demangled.get()} : std::string_view{info.dli_sname};

    StringBuilder builder{64};
    append_address(builder, address);
    builder.append(' ').append(name);
    return std::move(builder).str();
}

std::vector<const void *> capture_addresses(std::size_t skip, std::size_t max_frames) {
    // Capture enough to drop our own frame (+1) and the caller-requested skip.
    const std::size_t wanted = max_frames + skip + 1;
    std::vector<void *> raw(wanted);
    const int collected = backtrace(raw.data(), static_cast<int>(wanted));
    const std::size_t drop = std::min(static_cast<std::size_t>(collected), skip + 1);
    const std::size_t keep = std::min(static_cast<std::size_t>(collected) - drop, max_frames);
    return {std::next(raw.begin(), static_cast<std::ptrdiff_t>(drop)),
            std::next(raw.begin(), static_cast<std::ptrdiff_t>(drop + keep))};
}

#endif

} // namespace

StackTrace StackTrace::capture(std::size_t skip, std::size_t max_frames) {
    if (max_frames == 0) {
        return StackTrace{{}};
    }
    // Clamp against native-API limits (CaptureStackBackTrace bounds skip+count; backtrace
    // takes an int). Far deeper than any real stack a diagnostics trace wants.
    constexpr std::size_t hard_limit = 1024;
    return StackTrace{capture_addresses(std::min(skip, hard_limit), std::min(max_frames, hard_limit))};
}

std::vector<StackTrace::Frame> StackTrace::symbolize() const {
    std::vector<Frame> frames;
    frames.reserve(addresses_.size());
    for (const void *address : addresses_) {
        frames.push_back(Frame{.address = address, .description = describe_address(address)});
    }
    return frames;
}

std::string StackTrace::to_string() const {
    StringBuilder builder{addresses_.size() * 48};
    std::size_t index = 0;
    for (const Frame &frame : symbolize()) {
        builder.append('#').append(index).append(' ').append(std::string_view{frame.description}).append('\n');
        ++index;
    }
    return std::move(builder).str();
}

} // namespace it::d4np::util
