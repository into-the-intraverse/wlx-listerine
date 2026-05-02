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
    "${CMAKE_SOURCE_DIR}/src/colorizer/colorizer_resource.rc.in"
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

# --- Core shared library (wlx-listerine-core.dll) ---
add_library(wlx-listerine-core SHARED
    src/colorizer/colorizer.cpp
    src/colorizer/helix_theme.cpp
    src/colorizer/grammar_registry.cpp
    src/colorizer/grammar_cache.cpp
    src/colorizer/query_highlighter.cpp
    src/colorizer/wlx_core_abi.cpp
    src/colorizer/dllmain.cpp
    src/colorizer/core_registry.cpp
    src/colorizer/core_config.cpp
)

target_include_directories(wlx-listerine-core
    PUBLIC src/colorizer src include
)
target_link_libraries(wlx-listerine-core
    PUBLIC
        tree-sitter::tree-sitter
    PRIVATE
        tomlplusplus::tomlplusplus
)
target_compile_definitions(wlx-listerine-core PRIVATE WLX_CORE_BUILDING)

set_target_properties(wlx-listerine-core PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
)

# Mirror the DLL alongside test exes / screenshot_tool so they can find it on
# the loader search path (the exes live in ${CMAKE_BINARY_DIR}/<cfg>/, the DLL
# ships in output/).
add_custom_command(TARGET wlx-listerine-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:wlx-listerine-core>"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/wlx-listerine-core.dll"
)

# After Task 3, the CoreRegistry singleton derives its install dir from the
# core DLL's actual filesystem location (GetModuleFileNameW). Mirror grammars
# and themes alongside the DLL so the test rig running from build/<cfg>/ finds
# them at the same DLL-relative paths the production install uses.
add_custom_command(TARGET wlx-listerine-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/grammars"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/grammars"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/config/themes"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/themes"
)

# Task 4: ship wlx-listerine-core.toml next to the core DLL — both in the
# production output/ install dir AND alongside the test rig DLL mirror in
# build/<cfg>/. The CoreConfig::load() call in the registry singleton looks
# for this file at $<TARGET_FILE_DIR:wlx-listerine-core>/wlx-listerine-core.toml.
add_custom_command(TARGET wlx-listerine-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-core.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-core.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-core.toml"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/wlx-listerine-core.toml"
)

# --- Plugin DLL ---
add_library(wlx-listerine-md SHARED
    src/host_adapter.cpp
    src/search_hud.cpp
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

# --- Colorizer Plugin DLL ---
add_library(wlx-listerine-colorizer SHARED
    src/colorizer/colorizer_host_adapter.cpp
    src/colorizer/colorizer_layout.cpp
    src/search_hud.cpp
    src/colorizer/colorizer_plugin.def
    "${CMAKE_BINARY_DIR}/generated/colorizer_resource.rc"
)
target_include_directories(wlx-listerine-colorizer PRIVATE src/colorizer)

target_link_libraries(wlx-listerine-colorizer PRIVATE
    wlx-core
    wlx-listerine-core
    tomlplusplus::tomlplusplus
    shell32
    dwmapi
    uxtheme
    comctl32
)

set_target_properties(wlx-listerine-colorizer PROPERTIES
    SUFFIX ".wlx64"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
)

target_compile_definitions(wlx-listerine-colorizer PRIVATE WLX_VERSION_STRING="${WLX_VERSION}")
if(WLX_TRACE_ENABLE)
    target_compile_definitions(wlx-listerine-colorizer PRIVATE WLX_TRACE_ENABLE)
endif()

# Lay out the unified output/ directory: themes + grammars + sample TOMLs
# go alongside the two .wlx64 files and the core DLL. Packaging picks up
# from here.
add_custom_command(TARGET wlx-listerine-colorizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-md.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-md.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-colorizer.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-colorizer.toml"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/output/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/themes/default.toml"
        "${CMAKE_SOURCE_DIR}/output/themes/default.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/themes/default_light.toml"
        "${CMAKE_SOURCE_DIR}/output/themes/default_light.toml"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/output/grammars"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/grammars"
        "${CMAKE_SOURCE_DIR}/output/grammars"
)

# --- Screenshot tool ---
add_executable(screenshot_tool
    src/screenshot_main.cpp
)

target_link_libraries(screenshot_tool PRIVATE
    wlx-core
    windowscodecs
    ole32
)
