# Test suite for egl-util-cpp. doctest is fetched at configure time and stays strictly in
# test scope — the library itself remains zero-dependency.
include(FetchContent)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(doctest)

add_executable(util_tests smoke_test.cpp)
target_link_libraries(util_tests PRIVATE egl-util::egl-util doctest::doctest)
target_compile_features(util_tests PRIVATE cxx_std_20)

add_test(NAME util_tests COMMAND util_tests)
