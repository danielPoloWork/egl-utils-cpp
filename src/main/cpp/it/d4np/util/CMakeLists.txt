# egl-util-cpp library targets — the hybrid model (ADR-0004):
#
#   * egl-util         — INTERFACE (header-only) tier. The default surface; it carries the
#                        include root and the C++20 requirement and has zero link-time cost.
#   * egl-util-static  — optional STATIC tier (opt-in via EGL_UTIL_BUILD_STATIC) that
#                        compiles the translation units which require separate compilation
#                        (the OS-API-heavy components in roadmap M9, plus library_version()).
#
# The static tier links the interface tier PUBLICly, so it is a strict superset: anything
# that compiles against egl-util::egl-util also compiles against egl-util::egl-util-static.

# --- Header-only INTERFACE tier (always defined) -------------------------------------------
add_library(egl-util INTERFACE)
add_library(egl-util::egl-util ALIAS egl-util)

target_include_directories(
    egl-util INTERFACE "$<BUILD_INTERFACE:${EGL_UTIL_SRC_MAIN}>"
                       "$<INSTALL_INTERFACE:include>")
target_compile_features(egl-util INTERFACE cxx_std_20)

# --- Optional compiled STATIC tier ---------------------------------------------------------
# Gated behind EGL_UTIL_BUILD_STATIC so a header-only consumer pays for nothing. The archive
# is named libegl-util.a regardless of the CMake target name.
if(EGL_UTIL_BUILD_STATIC)
    add_library(egl-util-static STATIC version.cpp stack_trace.cpp logger.cpp file_stream.cpp
                                       tcp_socket.cpp)
    add_library(egl-util::egl-util-static ALIAS egl-util-static)
    target_link_libraries(egl-util-static PUBLIC egl-util)
    set_target_properties(egl-util-static PROPERTIES OUTPUT_NAME egl-util)

    # StackTrace (ADR-0019) symbolization backends and the Logger UDP sink's Winsock
    # (ADR-0020). PRIVATE: header-only consumers link nothing; static-tier consumers get
    # them transitively through the archive.
    if(WIN32)
        target_link_libraries(egl-util-static PRIVATE dbghelp ws2_32)
    else()
        target_link_libraries(egl-util-static PRIVATE ${CMAKE_DL_LIBS})
    endif()

    # Logger's pump thread needs the platform threads library (POSIX: -pthread).
    find_package(Threads REQUIRED)
    target_link_libraries(egl-util-static PRIVATE Threads::Threads)
endif()
