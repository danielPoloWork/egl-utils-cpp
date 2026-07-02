# Test suite for egl-util-cpp. doctest is fetched at configure time and stays strictly in
# test scope — the library itself remains zero-dependency.
include(FetchContent)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(doctest)

# ObjectPool's concurrency test uses std::thread, which needs the platform threads library.
find_package(Threads REQUIRED)

set(EGL_UTIL_TEST_SOURCES smoke_test.cpp type_traits_test.cpp hash_test.cpp unique_ref_test.cpp
                          heap_array_test.cpp stack_allocator_test.cpp object_pool_test.cpp
                          flat_set_test.cpp flat_map_test.cpp circular_buffer_test.cpp
                          string_splitter_test.cpp string_builder_test.cpp string_formatter_test.cpp
                          semaphore_test.cpp reader_writer_lock_test.cpp task_future_test.cpp
                          lock_free_queue_test.cpp thread_pool_test.cpp stopwatch_test.cpp)

# When the compiled tier is enabled, link against it (a strict superset of the interface
# tier, ADR-0004) and exercise its compiled components: library_version() and StackTrace.
if(EGL_UTIL_BUILD_STATIC)
    list(APPEND EGL_UTIL_TEST_SOURCES library_version_test.cpp stack_trace_test.cpp)
    set(EGL_UTIL_TEST_TIER egl-util::egl-util-static)
else()
    set(EGL_UTIL_TEST_TIER egl-util::egl-util)
endif()

add_executable(util_tests ${EGL_UTIL_TEST_SOURCES})
target_link_libraries(util_tests PRIVATE ${EGL_UTIL_TEST_TIER} doctest::doctest Threads::Threads)
target_compile_features(util_tests PRIVATE cxx_std_20)

# doctest forward-declares std types (e.g. std::basic_ostream) by default; recent standard
# libraries reject that once a TU also pulls in <string_view>/<ostream>. Make doctest include
# the real std headers instead, uniformly across every TU in this binary.
target_compile_definitions(util_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)

add_test(NAME util_tests COMMAND util_tests)
