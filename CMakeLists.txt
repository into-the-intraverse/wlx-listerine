cmake_minimum_required(VERSION 3.20)
project(wlx-listerine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(md4c REQUIRED)
find_package(tomlplusplus REQUIRED)

# --- Core static library ---
add_library(wlx-core STATIC
    src/file_service.cpp
    src/markdown_parser.cpp
    src/layout_engine.cpp
    src/render_engine.cpp
    src/interaction_engine.cpp
    src/theme_service.cpp
    src/cache_service.cpp
    src/text_selection.cpp
)

target_include_directories(wlx-core PUBLIC include src)
target_link_libraries(wlx-core
    PUBLIC
        md4c::md4c
        d2d1
        dwrite
    PRIVATE
        tomlplusplus::tomlplusplus
)

# --- Colorizer core static library ---
find_package(tree-sitter REQUIRED)

add_library(colorizer-core STATIC
    src/colorizer/colorizer.cpp
    src/colorizer/theme_loader.cpp
    src/colorizer/grammar_registry.cpp
)

target_include_directories(colorizer-core PUBLIC src/colorizer src)
target_link_libraries(colorizer-core
    PUBLIC
        tree-sitter::tree-sitter
    PRIVATE
        tomlplusplus::tomlplusplus
)

# --- Plugin DLL ---
add_library(wlx-listerine-md SHARED
    src/host_adapter.cpp
    src/plugin.def
    src/resource.rc
)

target_link_libraries(wlx-listerine-md PRIVATE
    wlx-core
    shell32
    dwmapi
    uxtheme
)

set_target_properties(wlx-listerine-md PROPERTIES
    SUFFIX ".wlx64"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
)

add_custom_command(TARGET wlx-listerine-md POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-md.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-md.toml"
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
    tests/test_text_selection.cpp
)

target_link_libraries(tests PRIVATE
    wlx-core
    doctest::doctest
)

# --- Colorizer tests ---
add_executable(colorizer-tests
    tests/test_main.cpp
    tests/test_colorizer.cpp
    tests/test_colorizer_theme.cpp
    tests/test_colorizer_grammar.cpp
)

target_link_libraries(colorizer-tests PRIVATE
    colorizer-core
    doctest::doctest
)
