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
    "${CMAKE_SOURCE_DIR}/src/resource.rc.in"
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

# --- Plugin DLL ---
add_library(wlx-listerine-md SHARED
    src/host_adapter.cpp
    src/runtime/search/search_hud.cpp
    src/plugin.def
    "${CMAKE_BINARY_DIR}/generated/resource.rc"
)
target_include_directories(wlx-listerine-md PRIVATE src)

target_link_libraries(wlx-listerine-md PRIVATE
    wlx-core
    wlx-listerine-core
    shell32
    dwmapi
    uxtheme
    comctl32
)

set_target_properties(wlx-listerine-md PROPERTIES
    SUFFIX ".wlx64"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
)

target_compile_definitions(wlx-listerine-md PRIVATE WLX_VERSION_STRING="${WLX_VERSION}")
if(WLX_TRACE_ENABLE)
    target_compile_definitions(wlx-listerine-md PRIVATE WLX_TRACE_ENABLE)
endif()

# --- Screenshot tool ---
add_executable(screenshot_tool
    src/screenshot_main.cpp
)

target_link_libraries(screenshot_tool PRIVATE
    wlx-core
    windowscodecs
    ole32
)
