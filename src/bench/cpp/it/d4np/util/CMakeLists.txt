# Benchmark suite for egl-util-cpp. The Milestone 1 stub uses only the standard library;
# a benchmark harness is introduced with the reproducible suite in roadmap M10.
add_executable(util_bench bench_main.cpp)
target_link_libraries(util_bench PRIVATE egl-util::egl-util)
target_compile_features(util_bench PRIVATE cxx_std_20)
