cmake_minimum_required(VERSION 3.20)
project(wlx-listerine LANGUAGES C CXX)

set(WLX_VERSION "dev" CACHE STRING "Plugin version string, overridden by CI")

option(WLX_TRACE_ENABLE "Emit OutputDebugStringW traces from WLX entry points and WndProc (capture with DebugView)" OFF)

set(CMAKE_CXX_STANDARD 20)
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
    src/search_engine.cpp
)

target_include_directories(wlx-core PUBLIC include src src/colorizer)
target_link_libraries(wlx-core
    PUBLIC
        md4c::md4c
        d2d1
        dwrite
        colorizer-core
    PRIVATE
        tomlplusplus::tomlplusplus
)

# --- Colorizer core static library ---
find_package(tree-sitter REQUIRED)

add_library(colorizer-core STATIC
    src/colorizer/colorizer.cpp
    src/colorizer/helix_theme.cpp
    src/colorizer/grammar_registry.cpp
    src/colorizer/query_highlighter.cpp
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
    colorizer-core
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

add_custom_command(TARGET wlx-listerine-md POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-md.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-md.toml"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/output/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/themes/default.toml"
        "${CMAKE_SOURCE_DIR}/output/themes/default.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/themes/default_light.toml"
        "${CMAKE_SOURCE_DIR}/output/themes/default_light.toml"
)

# --- Colorizer Plugin DLL ---
add_library(wlx-listerine-colorizer SHARED
    src/colorizer/colorizer_host_adapter.cpp
    src/colorizer/colorizer_layout.cpp
    src/colorizer/colorizer_plugin.def
    src/colorizer/colorizer_resource.rc
)

target_link_libraries(wlx-listerine-colorizer PRIVATE
    wlx-core
    colorizer-core
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

add_custom_command(TARGET wlx-listerine-colorizer POST_BUILD
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
    tests/test_search_engine.cpp
)

target_link_libraries(tests PRIVATE
    wlx-core
    doctest::doctest
)

# --- Grammar DLLs via FetchContent ---
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
# We use FetchContent_Populate (download-only, no add_subdirectory) to avoid
# target name conflicts from grammar repos' own CMakeLists.txt files.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# add_grammar(LANG SOURCE_DIR [QUERY_DIR path])
# Builds a grammar DLL from SOURCE_DIR/src/parser.c and copies highlights.scm.
# Optional QUERY_DIR overrides where to look for highlights.scm (default: SOURCE_DIR/queries).
function(add_grammar LANG SOURCE_DIR)
    cmake_parse_arguments(AG "" "QUERY_DIR" "" ${ARGN})
    if(NOT EXISTS "${SOURCE_DIR}/src/parser.c")
        message(STATUS "Grammar ${LANG}: parser.c not found in ${SOURCE_DIR}/src — skipping")
        return()
    endif()
    set(GRAMMAR_SOURCES "${SOURCE_DIR}/src/parser.c")
    if(EXISTS "${SOURCE_DIR}/src/scanner.c")
        list(APPEND GRAMMAR_SOURCES "${SOURCE_DIR}/src/scanner.c")
    elseif(EXISTS "${SOURCE_DIR}/src/scanner.cc")
        list(APPEND GRAMMAR_SOURCES "${SOURCE_DIR}/src/scanner.cc")
    endif()
    add_library(tree-sitter-${LANG} SHARED ${GRAMMAR_SOURCES})
    target_include_directories(tree-sitter-${LANG} PRIVATE "${SOURCE_DIR}/src")
    target_link_libraries(tree-sitter-${LANG} PRIVATE tree-sitter::tree-sitter)
    set_target_properties(tree-sitter-${LANG} PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/grammars/${LANG}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/grammars/${LANG}"
    )
    # Copy highlights.scm from upstream ONLY if we don't have a local override.
    # Local query files (in grammars/${LANG}/) take precedence — they may contain
    # Helix-compatible queries with ; inherits: directives and richer scope coverage.
    set(_local_query "${CMAKE_SOURCE_DIR}/grammars/${LANG}/highlights.scm")
    if(NOT EXISTS "${_local_query}")
        if(AG_QUERY_DIR)
            set(_query_dir "${AG_QUERY_DIR}")
        else()
            set(_query_dir "${SOURCE_DIR}/queries")
        endif()
        if(EXISTS "${_query_dir}/highlights.scm")
            add_custom_command(TARGET tree-sitter-${LANG} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_query_dir}/highlights.scm"
                    "${_local_query}"
            )
        endif()
    endif()
endfunction()

# fetch_grammar(NAME) — download only, no add_subdirectory (avoids target conflicts).
# Sets ${NAME}_SOURCE_DIR in parent scope.
macro(fetch_grammar NAME)
    FetchContent_GetProperties(${NAME})
    if(NOT ${NAME}_POPULATED)
        FetchContent_Populate(${NAME})
    endif()
endmacro()

# --- Standard grammars (21) ---
FetchContent_Declare(ts-c
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-c.git
    GIT_TAG        v0.24.1
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-cpp
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-cpp.git
    GIT_TAG        v0.23.4
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-python
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-python.git
    GIT_TAG        v0.25.0
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-javascript
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-javascript.git
    GIT_TAG        v0.25.0
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-rust
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-rust.git
    GIT_TAG        v0.24.2
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-go
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-go.git
    GIT_TAG        v0.25.0
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-java
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-java.git
    GIT_TAG        v0.23.5
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-c-sharp
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-c-sharp.git
    GIT_TAG        v0.23.1
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-json
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-json.git
    GIT_TAG        v0.24.8
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-html
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-html.git
    GIT_TAG        v0.23.2
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-css
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-css.git
    GIT_TAG        v0.25.0
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-bash
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-bash.git
    GIT_TAG        v0.25.1
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-toml
    GIT_REPOSITORY git@github.com:tree-sitter-grammars/tree-sitter-toml.git
    GIT_TAG        v0.7.0
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-yaml
    GIT_REPOSITORY git@github.com:tree-sitter-grammars/tree-sitter-yaml.git
    GIT_TAG        v0.7.2
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-lua
    GIT_REPOSITORY git@github.com:MunifTanjim/tree-sitter-lua.git
    GIT_TAG        v0.0.19
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-dockerfile
    GIT_REPOSITORY git@github.com:camdencheek/tree-sitter-dockerfile.git
    GIT_TAG        v0.2.0
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-cmake-lang
    GIT_REPOSITORY git@github.com:uyha/tree-sitter-cmake.git
    GIT_TAG        v0.7.2
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-gitattributes
    GIT_REPOSITORY git@github.com:ObserverOfTime/tree-sitter-gitattributes.git
    GIT_TAG        v0.1.6
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-gitconfig
    GIT_REPOSITORY git@github.com:the-mikedavis/tree-sitter-git-config.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-git-rebase
    GIT_REPOSITORY git@github.com:the-mikedavis/tree-sitter-git-rebase.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)

# --- Special repos ---
FetchContent_Declare(ts-typescript
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-typescript.git
    GIT_TAG        v0.23.2
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-php
    GIT_REPOSITORY git@github.com:tree-sitter/tree-sitter-php.git
    GIT_TAG        v0.24.2
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-vim
    GIT_REPOSITORY git@github.com:tree-sitter-grammars/tree-sitter-vim.git
    GIT_TAG        v0.8.1
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-powershell
    GIT_REPOSITORY git@github.com:airbus-cert/tree-sitter-powershell.git
    GIT_TAG        v0.26.3
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(ts-gitignore
    GIT_REPOSITORY git@github.com:shunsambongi/tree-sitter-gitignore.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)

# Download all grammar sources (no add_subdirectory — we only need the source files)
fetch_grammar(ts-c)
fetch_grammar(ts-cpp)
fetch_grammar(ts-python)
fetch_grammar(ts-javascript)
fetch_grammar(ts-rust)
fetch_grammar(ts-go)
fetch_grammar(ts-java)
fetch_grammar(ts-c-sharp)
fetch_grammar(ts-json)
fetch_grammar(ts-html)
fetch_grammar(ts-css)
fetch_grammar(ts-bash)
fetch_grammar(ts-toml)
fetch_grammar(ts-yaml)
fetch_grammar(ts-lua)
fetch_grammar(ts-dockerfile)
fetch_grammar(ts-cmake-lang)
fetch_grammar(ts-gitattributes)
fetch_grammar(ts-gitconfig)
fetch_grammar(ts-git-rebase)
fetch_grammar(ts-typescript)
fetch_grammar(ts-php)
fetch_grammar(ts-vim)
fetch_grammar(ts-powershell)
fetch_grammar(ts-gitignore)

# --- Build standard grammars ---
add_grammar(c           "${ts-c_SOURCE_DIR}")
add_grammar(cpp         "${ts-cpp_SOURCE_DIR}")
add_grammar(python      "${ts-python_SOURCE_DIR}")
add_grammar(javascript  "${ts-javascript_SOURCE_DIR}")
add_grammar(rust        "${ts-rust_SOURCE_DIR}")
add_grammar(go          "${ts-go_SOURCE_DIR}")
add_grammar(java        "${ts-java_SOURCE_DIR}")
add_grammar(c-sharp     "${ts-c-sharp_SOURCE_DIR}")
add_grammar(json        "${ts-json_SOURCE_DIR}")
add_grammar(html        "${ts-html_SOURCE_DIR}")
add_grammar(css         "${ts-css_SOURCE_DIR}")
add_grammar(bash        "${ts-bash_SOURCE_DIR}")
add_grammar(toml        "${ts-toml_SOURCE_DIR}")
add_grammar(yaml        "${ts-yaml_SOURCE_DIR}")
add_grammar(lua         "${ts-lua_SOURCE_DIR}")
add_grammar(dockerfile  "${ts-dockerfile_SOURCE_DIR}")
add_grammar(cmake       "${ts-cmake-lang_SOURCE_DIR}")
add_grammar(gitattributes "${ts-gitattributes_SOURCE_DIR}")
add_grammar(git-config  "${ts-gitconfig_SOURCE_DIR}")
add_grammar(git_rebase  "${ts-git-rebase_SOURCE_DIR}")

# --- Special grammars ---
# TypeScript: split repo, grammar lives in typescript/ subdir
add_grammar(typescript  "${ts-typescript_SOURCE_DIR}/typescript" QUERY_DIR "${ts-typescript_SOURCE_DIR}/queries")

# PHP: split repo, use php/ subdir (exports tree_sitter_php)
add_grammar(php         "${ts-php_SOURCE_DIR}/php" QUERY_DIR "${ts-php_SOURCE_DIR}/queries")

# Vim: highlights.scm is at queries/vim/highlights.scm in repo root
add_grammar(vim         "${ts-vim_SOURCE_DIR}" QUERY_DIR "${ts-vim_SOURCE_DIR}/queries/vim")

# PowerShell: no upstream highlights.scm — use our bundled one
add_grammar(powershell  "${ts-powershell_SOURCE_DIR}")

# gitignore: no upstream highlights.scm — use our bundled one
add_grammar(gitignore   "${ts-gitignore_SOURCE_DIR}")

# --- Colorizer tests ---
add_executable(colorizer-tests
    tests/test_main.cpp
    tests/test_colorizer.cpp
    tests/test_colorizer_helix_theme.cpp
    tests/test_colorizer_grammar.cpp
    tests/test_colorizer_query_highlighter.cpp
    tests/test_colorizer_grammars.cpp
)

target_link_libraries(colorizer-tests PRIVATE
    colorizer-core
    doctest::doctest
)
