# Fuzz harnesses for the untrusted-input components (ADR-0031): JsonParser (#23), CliParser
# (#22), and BinaryDeserializer (#18). Two build modes:
#   * libFuzzer  — Clang + EGL_UTIL_FUZZER_ENGINE=libfuzzer: coverage-guided fuzzing under
#                  ASan/UBSan (the harness's LLVMFuzzerTestOneInput is the entry point).
#   * standalone — the default / any compiler: a replay main() (fuzz_standalone.hpp) that runs
#                  each input file once, so the harnesses build on the whole matrix, stay in the
#                  clang-tidy compile database, and can replay a crashing corpus locally.
set(EGL_UTIL_FUZZ_TARGETS fuzz_json_parser fuzz_cli_parser fuzz_binary_serializer)

set(_egl_fuzz_libfuzzer OFF)
if(EGL_UTIL_FUZZER_ENGINE STREQUAL "libfuzzer")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(_egl_fuzz_libfuzzer ON)
    else()
        message(WARNING
            "EGL_UTIL_FUZZER_ENGINE=libfuzzer requires Clang; "
            "building standalone replay harnesses instead.")
    endif()
endif()

foreach(target IN LISTS EGL_UTIL_FUZZ_TARGETS)
    add_executable(${target} ${target}.cpp)
    target_link_libraries(${target} PRIVATE egl-util::egl-util)
    target_compile_features(${target} PRIVATE cxx_std_20)
    if(_egl_fuzz_libfuzzer)
        target_compile_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined
                                                 -fno-sanitize-recover=all -g)
        target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
    else()
        target_compile_definitions(${target} PRIVATE EGL_UTIL_FUZZER_STANDALONE)
    endif()
endforeach()
