cmake_minimum_required(VERSION 3.20)
project(wlx-mini-markdown LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(md4c REQUIRED)
find_package(tomlplusplus REQUIRED)

# --- Plugin DLL ---
add_library(wlx-mini-markdown SHARED
    src/plugin.cpp
    src/rtf_builder.cpp
    src/config.cpp
    src/plugin.def
    src/resource.rc
)

target_include_directories(wlx-mini-markdown PRIVATE include src)
target_link_libraries(wlx-mini-markdown PRIVATE md4c::md4c tomlplusplus::tomlplusplus)

set_target_properties(wlx-mini-markdown PROPERTIES
    SUFFIX ".wlx64"
    PREFIX ""
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# --- Tests ---
find_package(doctest REQUIRED)

add_executable(tests
    tests/test_main.cpp
    tests/test_config.cpp
    tests/test_rtf_builder.cpp
    src/rtf_builder.cpp
    src/config.cpp
)

target_include_directories(tests PRIVATE include src)
target_link_libraries(tests PRIVATE md4c::md4c tomlplusplus::tomlplusplus doctest::doctest)

set_target_properties(tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
