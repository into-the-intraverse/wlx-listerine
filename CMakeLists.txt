cmake_minimum_required(VERSION 3.20)
project(wlx-mini-markdown LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(md4c REQUIRED)
find_package(tomlplusplus REQUIRED)

# --- Plugin DLL ---
add_library(wlx-mini-markdown SHARED
    src/host_adapter.cpp
    src/file_service.cpp
    src/markdown_parser.cpp
    src/layout_engine.cpp
    src/render_engine.cpp
    src/interaction_engine.cpp
    src/theme_service.cpp
    src/cache_service.cpp
    src/plugin.def
    src/resource.rc
)

target_include_directories(wlx-mini-markdown PRIVATE include src)
target_link_libraries(wlx-mini-markdown PRIVATE
    md4c::md4c
    tomlplusplus::tomlplusplus
    d2d1
    dwrite
    shell32
)

set_target_properties(wlx-mini-markdown PROPERTIES
    SUFFIX ".wlx64"
    PREFIX ""
)

# --- Tests ---
find_package(doctest REQUIRED)

add_executable(tests
    tests/test_main.cpp
    tests/test_document_model.cpp
    tests/test_theme_service.cpp
    tests/test_file_service.cpp
    tests/test_markdown_parser.cpp
    tests/test_cache_service.cpp
    tests/test_layout_engine.cpp
    src/theme_service.cpp
    src/file_service.cpp
    src/markdown_parser.cpp
    src/cache_service.cpp
    src/layout_engine.cpp
)

target_include_directories(tests PRIVATE include src)
target_link_libraries(tests PRIVATE
    md4c::md4c
    tomlplusplus::tomlplusplus
    dwrite
    doctest::doctest
)

