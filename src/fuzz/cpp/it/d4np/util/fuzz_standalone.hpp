// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Daniel Polo
//
// Standalone replay entry point shared by the fuzz harnesses (ADR-0031). When a harness is NOT
// built with libFuzzer (i.e. EGL_UTIL_FUZZER_STANDALONE is defined), libFuzzer does not supply
// `main`, so this header provides one that replays each input file once through the harness's
// `LLVMFuzzerTestOneInput`. This keeps the harnesses buildable on the whole CI matrix (incl.
// MSVC), present in the clang-tidy compile database, and usable to reproduce a crashing corpus
// entry locally without libFuzzer. In libFuzzer builds this header is inert.
#ifndef IT_D4NP_UTIL_FUZZ_STANDALONE_HPP
#define IT_D4NP_UTIL_FUZZ_STANDALONE_HPP

#ifdef EGL_UTIL_FUZZER_STANDALONE

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size);

int main(int argc, char **argv) {
    const std::span<char *const> args{argv, static_cast<std::size_t>(argc)};
    for (char *const path : args.subspan(1)) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            continue;
        }
        // Read the file, then copy into a byte buffer element-wise (char -> uint8_t, no cast) to
        // match the libFuzzer entry-point ABI without aliasing.
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const std::vector<std::uint8_t> buffer(bytes.begin(), bytes.end());
        static_cast<void>(LLVMFuzzerTestOneInput(buffer.data(), buffer.size()));
    }
    return 0;
}

#endif // EGL_UTIL_FUZZER_STANDALONE

#endif // IT_D4NP_UTIL_FUZZ_STANDALONE_HPP
