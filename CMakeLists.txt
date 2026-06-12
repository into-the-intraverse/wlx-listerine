cmake_minimum_required(VERSION 3.20)
project(wlx-listerine LANGUAGES C CXX)

set(WLX_VERSION "dev" CACHE STRING "Plugin version string, overridden by CI")

# Parse WLX_VERSION into major.minor.patch components for the Win32 VERSIONINFO
# resource. Non-numeric values (e.g. "dev") fall back to 0.0.0.
if(WLX_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(WLX_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(WLX_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(WLX_VERSION_PATCH "${CMAKE_MATCH_3}")
else()
    set(WLX_VERSION_MAJOR 0)
    set(WLX_VERSION_MINOR 0)
    set(WLX_VERSION_PATCH 0)
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/src/plugin_md/resource/resource.rc.in"
    "${CMAKE_BINARY_DIR}/generated/resource.rc"
    @ONLY
)
configure_file(
    "${CMAKE_SOURCE_DIR}/src/plugin_colorizer/resource/colorizer_resource.rc.in"
    "${CMAKE_BINARY_DIR}/generated/colorizer_resource.rc"
    @ONLY
)

option(WLX_TRACE_ENABLE "Emit OutputDebugStringW traces from WLX entry points and WndProc (capture with DebugView)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(md4c REQUIRED)
find_package(tomlplusplus REQUIRED)
find_package(tree-sitter REQUIRED)
find_package(doctest REQUIRED)

include(${CMAKE_SOURCE_DIR}/cmake/grammars.cmake)

add_subdirectory(src/runtime)
add_subdirectory(src/core_dll)
add_subdirectory(src/plugin_md)
add_subdirectory(src/plugin_colorizer)
add_subdirectory(src/tools/screenshot)
add_subdirectory(tests)

# First-party code builds at /W4 with warnings as errors. Third-party code
# (conan imports, the FetchContent grammar DLLs in cmake/grammars.cmake) is
# deliberately not listed. C4251 (std member of a dllexported class needs
# dll-interface) is disabled: the core DLL, both plugins, and the tools ship
# as one lockstep bundle built with a single toolchain/CRT, which is exactly
# the case where that warning is noise. /utf-8: sources are UTF-8 without BOM
# and carry non-ASCII string literals (test inputs like L"café"); without the
# flag MSVC falls back to the host ACP, so the same commit compiles to
# different bytes depending on the machine/runner image.
foreach(t wlx-core wlx-listerine-core wlx-listerine-md wlx-listerine-colorizer
          screenshot_tool tests colorizer-tests)
    target_compile_options(${t} PRIVATE /W4 /WX /wd4251 /utf-8)
endforeach()
