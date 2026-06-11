# Tree-sitter grammar fetching and build rules.
# Produces ${CMAKE_SOURCE_DIR}/grammars/<lang>/tree-sitter-<lang>.dll for each declared grammar.
# Included from the root CMakeLists.txt after find_package(tree-sitter).

include(FetchContent)
# QUIET=OFF so grammar downloads print progress on CMake 3.20-3.29, where each
# population runs a slow nested sub-build (otherwise the configure step looks
# frozen after "Target declared 'doctest::doctest'"). CMake 3.30+ with CMP0168
# NEW ignores this and streams download output directly.
set(FETCHCONTENT_QUIET OFF)
# We use FetchContent_Populate (download-only, no add_subdirectory) to avoid
# target name conflicts from grammar repos' own CMakeLists.txt files.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
# Populate directly during configure instead of spawning a nested
# sub-build (configure + build tool) per grammar — 27 of those dominate
# configure time, especially on Windows. Captured at FetchContent_Declare
# time, so this must precede the declares below. CMake 3.20-3.29 keep the
# sub-build path.
if(POLICY CMP0168)
    cmake_policy(SET CMP0168 NEW)
endif()
# Give extracted archive files fresh timestamps. With OLD behavior they keep
# the archive's mtimes, so bumping a grammar version in an existing build dir
# would leave sources older than the previously built .objs and grammar DLLs
# would silently not rebuild.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# CI sets this so a bad archive URL or upstream layout change fails the build
# instead of silently shipping a bundle with missing grammar DLLs
# (add_grammar skips quietly and the grammar tests self-skip when a DLL is
# absent, so nothing else would catch it).
option(WLX_REQUIRE_ALL_GRAMMARS "Fail configure if any grammar source lacks src/parser.c" OFF)

# add_grammar(LANG SOURCE_DIR
#             [QUERY_DIR path]
#             [UPSTREAM_SYMBOL name])
# Builds a grammar DLL from SOURCE_DIR/src/parser.c and copies highlights.scm.
# Optional QUERY_DIR overrides where to look for highlights.scm (default: SOURCE_DIR/queries).
# Optional UPSTREAM_SYMBOL: when the upstream grammar exports `name` rather than
# tree_sitter_<LANG> (with `-` -> `_`), generates an alias translation unit that
# forwards tree_sitter_<LANG>() to `name()`. Used for forks that retain their
# parent's exported symbol (e.g. taku25/tree-sitter-unreal-cpp exports tree_sitter_cpp).
function(add_grammar LANG SOURCE_DIR)
    cmake_parse_arguments(AG "" "QUERY_DIR;UPSTREAM_SYMBOL" "" ${ARGN})
    if(NOT EXISTS "${SOURCE_DIR}/src/parser.c")
        if(WLX_REQUIRE_ALL_GRAMMARS)
            message(FATAL_ERROR "Grammar ${LANG}: parser.c not found in ${SOURCE_DIR}/src")
        endif()
        message(STATUS "Grammar ${LANG}: parser.c not found in ${SOURCE_DIR}/src — skipping")
        return()
    endif()
    set(GRAMMAR_SOURCES "${SOURCE_DIR}/src/parser.c")
    if(EXISTS "${SOURCE_DIR}/src/scanner.c")
        list(APPEND GRAMMAR_SOURCES "${SOURCE_DIR}/src/scanner.c")
    elseif(EXISTS "${SOURCE_DIR}/src/scanner.cc")
        list(APPEND GRAMMAR_SOURCES "${SOURCE_DIR}/src/scanner.cc")
    endif()

    # When upstream exports a different symbol than tree_sitter_<LANG>, generate
    # an alias TU that forwards tree_sitter_<LANG>() to the upstream symbol.
    if(AG_UPSTREAM_SYMBOL)
        string(REPLACE "-" "_" _lang_underscored "${LANG}")
        set(_alias_path "${CMAKE_CURRENT_BINARY_DIR}/grammars/${LANG}_alias.c")
        file(WRITE "${_alias_path}"
"#include \"tree_sitter/api.h\"\n"
"extern const TSLanguage *${AG_UPSTREAM_SYMBOL}(void);\n"
"__declspec(dllexport) const TSLanguage *tree_sitter_${_lang_underscored}(void) {\n"
"    return ${AG_UPSTREAM_SYMBOL}();\n"
"}\n"
        )
        list(APPEND GRAMMAR_SOURCES "${_alias_path}")
    endif()

    add_library(tree-sitter-${LANG} SHARED ${GRAMMAR_SOURCES})
    # Collect every grammar target so consumers whose POST_BUILD mirrors the
    # grammars/ directory can depend on ALL of them — copying that directory
    # while a grammar DLL is still being linked into it fails the build.
    set_property(GLOBAL APPEND PROPERTY WLX_GRAMMAR_TARGETS tree-sitter-${LANG})
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

# All grammars are fetched as GitHub source archives, not git clones: a tarball
# is a single fast HTTP download (no git negotiation, no history — the
# tree-sitter repos carry huge generated parser.c blobs in theirs), and the
# URL_HASH both verifies integrity and lets CMake reuse an already-downloaded
# archive without touching the network. Branch-tracking upstreams are pinned
# to commit SHAs so the archives are reproducible.

# --- Standard grammars (21) ---
FetchContent_Declare(ts-c
    URL      https://github.com/tree-sitter/tree-sitter-c/archive/refs/tags/v0.24.1.tar.gz
    URL_HASH SHA256=25dd4bb3dec770769a407e0fc803f424ce02c494a56ce95fedc525316dcf9b48
)
FetchContent_Declare(ts-cpp
    URL      https://github.com/tree-sitter/tree-sitter-cpp/archive/refs/tags/v0.23.4.tar.gz
    URL_HASH SHA256=7a2c55afe3028f4105f25762ea58cc16537d1f5a1dcd9cca90410b3cd5d46051
)
FetchContent_Declare(ts-python
    URL      https://github.com/tree-sitter/tree-sitter-python/archive/refs/tags/v0.25.0.tar.gz
    URL_HASH SHA256=4609a3665a620e117acf795ff01b9e965880f81745f287a16336f4ca86cf270c
)
FetchContent_Declare(ts-javascript
    URL      https://github.com/tree-sitter/tree-sitter-javascript/archive/refs/tags/v0.25.0.tar.gz
    URL_HASH SHA256=9712fc283d3dc01d996d20b6392143445d05867a7aad76fdd723824468428b86
)
FetchContent_Declare(ts-rust
    URL      https://github.com/tree-sitter/tree-sitter-rust/archive/refs/tags/v0.24.2.tar.gz
    URL_HASH SHA256=061e90a539a55a6aa65dceb0ad6425c50ab1a6e3e6d4ba430e2795ed4550f10e
)
FetchContent_Declare(ts-go
    URL      https://github.com/tree-sitter/tree-sitter-go/archive/refs/tags/v0.25.0.tar.gz
    URL_HASH SHA256=2dc241b97872c53195e01b86542b411a3c1a6201d9c946c78d5c60c063bba1ef
)
FetchContent_Declare(ts-java
    URL      https://github.com/tree-sitter/tree-sitter-java/archive/refs/tags/v0.23.5.tar.gz
    URL_HASH SHA256=cb199e0faae4b2c08425f88cbb51c1a9319612e7b96315a174a624db9bf3d9f0
)
FetchContent_Declare(ts-c-sharp
    URL      https://github.com/tree-sitter/tree-sitter-c-sharp/archive/refs/tags/v0.23.1.tar.gz
    URL_HASH SHA256=c0b008dca3c6820604bf0853b9668ba034f9750d89d170ba834261e94e2cd917
)
FetchContent_Declare(ts-json
    URL      https://github.com/tree-sitter/tree-sitter-json/archive/refs/tags/v0.24.8.tar.gz
    URL_HASH SHA256=acf6e8362457e819ed8b613f2ad9a0e1b621a77556c296f3abea58f7880a9213
)
FetchContent_Declare(ts-html
    URL      https://github.com/tree-sitter/tree-sitter-html/archive/refs/tags/v0.23.2.tar.gz
    URL_HASH SHA256=21fa4f2d4dcb890ef12d09f4979a0007814f67f1c7294a9b17b0108a09e45ef7
)
FetchContent_Declare(ts-xml
    URL      https://github.com/tree-sitter-grammars/tree-sitter-xml/archive/refs/tags/v0.7.0.tar.gz
    URL_HASH SHA256=4330a6b3685c2f66d108e1df0448eb40c468518c3a66f2c1607a924c262a3eb9
)
FetchContent_Declare(ts-css
    URL      https://github.com/tree-sitter/tree-sitter-css/archive/refs/tags/v0.25.0.tar.gz
    URL_HASH SHA256=03965344d8c0435dc54fb45b281578420bb7db8b99df4d34e7e74105a274cb79
)
FetchContent_Declare(ts-bash
    URL      https://github.com/tree-sitter/tree-sitter-bash/archive/refs/tags/v0.25.1.tar.gz
    URL_HASH SHA256=2e785a761225b6c433410ef9c7b63cfb0a4e83a35a19e0f2aec140b42c06b52d
)
FetchContent_Declare(ts-toml
    URL      https://github.com/tree-sitter-grammars/tree-sitter-toml/archive/refs/tags/v0.7.0.tar.gz
    URL_HASH SHA256=7d52a7d4884f307aabc872867c69084d94456d8afcdc63b0a73031a8b29036dc
)
FetchContent_Declare(ts-yaml
    URL      https://github.com/tree-sitter-grammars/tree-sitter-yaml/archive/refs/tags/v0.7.2.tar.gz
    URL_HASH SHA256=aeaff5731bb8b66c7054c8aed33cd5edea5f4cd2ac71654f3f6c2ba2073d8fac
)
FetchContent_Declare(ts-lua
    URL      https://github.com/MunifTanjim/tree-sitter-lua/archive/refs/tags/v0.0.19.tar.gz
    URL_HASH SHA256=974230f212d0049fce8e881b88b18eebbd05f1fd0edd16fe4ba5bdd2bcd78d08
)
FetchContent_Declare(ts-dockerfile
    URL      https://github.com/camdencheek/tree-sitter-dockerfile/archive/refs/tags/v0.2.0.tar.gz
    URL_HASH SHA256=8cbdf50838cc55e841aa9585a1a09e5b8c41454ae30ce0a93a7bd71adb140818
)
FetchContent_Declare(ts-cmake-lang
    URL      https://github.com/uyha/tree-sitter-cmake/archive/refs/tags/v0.7.2.tar.gz
    URL_HASH SHA256=c9498a31d6462b3eda82ff0988e95109b3853d88cc7c393a5008736e7da527e0
)
FetchContent_Declare(ts-gitattributes
    URL      https://github.com/ObserverOfTime/tree-sitter-gitattributes/archive/refs/tags/v0.1.6.tar.gz
    URL_HASH SHA256=118a66d8b3332593e61f7466ac3f21fd15b580e60eb2436ca3ee70955d6714ae
)
FetchContent_Declare(ts-gitconfig
    # main @ 2026-06-11 — no upstream tags
    URL      https://github.com/the-mikedavis/tree-sitter-git-config/archive/0fbc9f99d5a28865f9de8427fb0672d66f9d83a5.tar.gz
    URL_HASH SHA256=4a008a5392e2696879f60c0490bef6c6fe7f554aecb1d612bb6efec799e45584
)
FetchContent_Declare(ts-git-rebase
    # main @ 2026-06-11 — no upstream tags
    URL      https://github.com/the-mikedavis/tree-sitter-git-rebase/archive/32686d6b72980b36f876ae2d07719c9c3ed154e2.tar.gz
    URL_HASH SHA256=951d33ba305003ad3444ea89cb878d63fdee03c376f2f43efb389a0cccd4efea
)

# --- Special repos ---
FetchContent_Declare(ts-typescript
    URL      https://github.com/tree-sitter/tree-sitter-typescript/archive/refs/tags/v0.23.2.tar.gz
    URL_HASH SHA256=2c4ce711ae8d1218a3b2f899189298159d672870b5b34dff5d937bed2f3e8983
)
FetchContent_Declare(ts-php
    URL      https://github.com/tree-sitter/tree-sitter-php/archive/refs/tags/v0.24.2.tar.gz
    URL_HASH SHA256=0e73ad63dda67ac12c0e012726a4e1a9811c26b020a0a2dea3e889f8246d9cf4
)
FetchContent_Declare(ts-vim
    URL      https://github.com/tree-sitter-grammars/tree-sitter-vim/archive/refs/tags/v0.8.1.tar.gz
    URL_HASH SHA256=93cafb9a0269420362454ace725a118ff1c3e08dcdfdc228aa86334b54d53c2a
)
FetchContent_Declare(ts-powershell
    URL      https://github.com/airbus-cert/tree-sitter-powershell/archive/refs/tags/v0.26.3.tar.gz
    URL_HASH SHA256=38f9cba3174dc63274336120070cd6a1828fa8eb832360b94ed2ddfe6c3ac226
)
FetchContent_Declare(ts-gitignore
    # main @ 2026-06-11 — no upstream tags
    URL      https://github.com/shunsambongi/tree-sitter-gitignore/archive/f4685bf11ac466dd278449bcfe5fd014e94aa504.tar.gz
    URL_HASH SHA256=15727772801cf49bd85b147dc7f77f6c3ddabbdb3b3d55c6580e7dd8f7aa559c
)
FetchContent_Declare(ts-unreal-cpp
    # 92eee7d @ 2026-04-10 — pinned SHA, no upstream tags
    URL      https://github.com/taku25/tree-sitter-unreal-cpp/archive/92eee7d1ac994e408c208bcb1b73170c8746356f.tar.gz
    URL_HASH SHA256=74aafbaaa43a7a88eb86d220dbd1279ce16b65bcd6d615437b1613424a2a8f88
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
fetch_grammar(ts-xml)
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
fetch_grammar(ts-unreal-cpp)

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

# XML: split repo (xml/ + dtd/), we only ship the xml parser.
# Parser src is at xml/src/parser.c; highlights at queries/xml/highlights.scm
# (queries live at the repo root, like the vim layout).
add_grammar(xml         "${ts-xml_SOURCE_DIR}/xml" QUERY_DIR "${ts-xml_SOURCE_DIR}/queries/xml")

# PHP: split repo, use php/ subdir (exports tree_sitter_php)
add_grammar(php         "${ts-php_SOURCE_DIR}/php" QUERY_DIR "${ts-php_SOURCE_DIR}/queries")

# Vim: highlights.scm is at queries/vim/highlights.scm in repo root
add_grammar(vim         "${ts-vim_SOURCE_DIR}" QUERY_DIR "${ts-vim_SOURCE_DIR}/queries/vim")

# PowerShell: no upstream highlights.scm — use our bundled one
add_grammar(powershell  "${ts-powershell_SOURCE_DIR}")

# gitignore: no upstream highlights.scm — use our bundled one
add_grammar(gitignore   "${ts-gitignore_SOURCE_DIR}")

# Unreal C++ — fork that exports tree_sitter_cpp; alias TU bridges the name.
add_grammar(unreal-cpp "${ts-unreal-cpp_SOURCE_DIR}" UPSTREAM_SYMBOL tree_sitter_cpp)
