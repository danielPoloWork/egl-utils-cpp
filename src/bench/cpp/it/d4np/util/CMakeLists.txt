# Benchmark suite for egl-util-cpp (roadmap 10.2, ADR-0027). A dependency-free harness
# (benchmark_harness.hpp) times header-only hot paths through the library's own Stopwatch.
add_executable(util_bench bench_main.cpp)
target_link_libraries(util_bench PRIVATE egl-util::egl-util)
target_compile_features(util_bench PRIVATE cxx_std_20)
