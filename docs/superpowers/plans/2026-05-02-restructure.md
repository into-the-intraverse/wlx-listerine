# Source Tree Restructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **For this restructure: Inline Execution is recommended** — cross-cutting moves and #include sweeps cannot be safely parallelized across stateless subagents.

**Goal:** Restructure the source tree so binaries map to top-level folders, every type lives in its own file named after it, namespaces match folder paths under `wlx::`, tests mirror `src/`, and per-folder `CMakeLists.txt` files own their own targets — without changing any runtime behavior.

**Architecture:** Six staged phases on a single branch; every commit independently builds and passes both `tests` and `colorizer-tests`. Phase order: build extraction → file moves → header splits → namespace adoption → duplicate-helper lift → test scenario splits → docs. Layout is hybrid binary+subsystem (`core_dll/`, `runtime/`, `plugin_md/`, `plugin_colorizer/`, `tools/`).

**Tech Stack:** C++20, MSVC, CMake 3.20+, Conan 2.x, doctest, Direct2D/DirectWrite, md4c, tree-sitter, tomlplusplus.

**Reference:** `docs/superpowers/specs/2026-05-02-restructure-design.md` — read this first if you need context on *why* a particular folder exists.

---

## Common Procedures

These rituals repeat across nearly every task. Defined once here; each task references them.

### Build + test ritual (`BUILD_TEST_OK`)

After every modification:

```bash
cmake --build --preset conan-release
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

**Expected:** clean build (no warnings-as-errors), `tests` reports `0 failed`, `colorizer-tests` reports `0 failed`. If anything fails, **stop and fix before committing** — never commit a red state.

For Phase 4 only, also run:

```bash
./scripts/visual-test.sh
```

**Expected:** all 27 test cases ≥ 95% pixel similarity (PASS).

### Move-and-update pattern (`MOVE_PATTERN`)

To move a file from `<old>` to `<new>`:

1. `git mv <old> <new>` — preserves history per file.
2. Update `<new>`'s declaring `CMakeLists.txt` to reference the new path.
3. Remove `<old>` from the root `CMakeLists.txt` source lists.
4. Sweep `#include` references across the codebase:

   ```bash
   grep -rln '#include "<old-basename>"' src tests | xargs sed -i 's|#include "<old-basename>"|#include "<new-relative-path>"|g'
   ```

   On Windows shells, prefer the equivalent PowerShell:
   ```powershell
   Get-ChildItem -Recurse src,tests -Include *.h,*.cpp | ForEach-Object {
       (Get-Content $_.FullName) -replace [regex]::Escape('#include "<old>"'), '#include "<new>"' | Set-Content $_.FullName
   }
   ```

5. `BUILD_TEST_OK`.

### Commit message style

`refactor(layout): <one-line summary>` for layout-only commits, `refactor(lift): ...` for Phase 4. Always include the spec reference in the body:

```
Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md
```

### Include path convention

Every binary's `CMakeLists.txt` adds `target_include_directories(<target> PRIVATE ${CMAKE_SOURCE_DIR}/src)`. From any source file, includes use the full module path rooted at `src/`:

- `#include "runtime/layout/layout_engine.h"`
- `#include "core_dll/grammar/grammar_registry.h"`
- `#include "wlx_core/abi.h"` (public ABI under `include/wlx_core/`)

Never use `..` in includes; never include from a sibling binary's private folder.

---

## Phase 0 — Build extraction

No source files move. CMake only.

### Task 0.1: Extract grammar machinery to `cmake/grammars.cmake`

**Files:**
- Create: `cmake/grammars.cmake`
- Modify: `CMakeLists.txt` (root)

**Why:** The root `CMakeLists.txt` is 561 lines, of which ~270 are the `add_grammar`/`fetch_grammar` machinery and 26 grammar declarations. Extracting them shrinks the root and groups related logic.

- [ ] **Step 1: Create `cmake/grammars.cmake` with the grammar machinery**

Move lines 252–541 of the current root `CMakeLists.txt` (the `include(FetchContent)` block through `add_grammar(unreal-cpp ...)`) into `cmake/grammars.cmake`. The new file's first lines should be:

```cmake
# Tree-sitter grammar fetching and build rules.
# Produces ${CMAKE_SOURCE_DIR}/grammars/<lang>/tree-sitter-<lang>.dll for each declared grammar.
# Included from the root CMakeLists.txt after find_package(tree-sitter).

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
```

…followed by the existing `function(add_grammar …)`, `macro(fetch_grammar …)`, all `FetchContent_Declare` calls, all `fetch_grammar` calls, and all `add_grammar` calls.

- [ ] **Step 2: Replace those lines in root `CMakeLists.txt` with `include(cmake/grammars.cmake)`**

The line `find_package(tree-sitter REQUIRED)` (currently around line 63) stays where it is. Immediately before the `# --- Standard grammars (21) ---` block, replace everything through the last `add_grammar(unreal-cpp ...)` line with:

```cmake
include(${CMAKE_SOURCE_DIR}/cmake/grammars.cmake)
```

- [ ] **Step 3: `BUILD_TEST_OK`**

The build must produce identical output binaries and the same 26 grammar DLLs.

- [ ] **Step 4: Commit**

```bash
git add cmake/grammars.cmake CMakeLists.txt
git commit -m "$(cat <<'EOF'
refactor(cmake): extract grammar machinery to cmake/grammars.cmake

Root CMakeLists drops from 561 to ~290 lines. Grammar declarations,
add_grammar/fetch_grammar functions, and FetchContent calls now live
in cmake/grammars.cmake. No source changes; build output unchanged.

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 0.2: Add empty per-folder `CMakeLists.txt` scaffolds

**Files:**
- Create: `src/runtime/CMakeLists.txt`
- Create: `src/core_dll/CMakeLists.txt`
- Create: `src/plugin_md/CMakeLists.txt`
- Create: `src/plugin_colorizer/CMakeLists.txt`
- Create: `src/tools/screenshot/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt` (root)

**Why:** Establishes empty subdirectories that the root will `add_subdirectory()` into during Phase 1. Keeps root unchanged in target definitions for now — sources move into per-folder lists in Phase 1. CMake permits empty `CMakeLists.txt` files.

- [ ] **Step 1: Create the directory tree**

```bash
mkdir -p src/runtime src/core_dll src/plugin_md src/plugin_colorizer src/tools/screenshot
```

- [ ] **Step 2: Create empty `CMakeLists.txt` in each new folder**

Each file's contents:

```cmake
# Filled during Phase 1 of the restructure.
# See docs/superpowers/plans/2026-05-02-restructure.md
```

`tests/CMakeLists.txt` already has nothing today — the test target is declared in the root. Create `tests/CMakeLists.txt` with the same placeholder text.

- [ ] **Step 3: Hoist `find_package(tree-sitter)` and `include(grammars.cmake)`, then add `add_subdirectory()` calls**

After Task 0.1, the root looks like: `find_package(md4c)`, `find_package(tomlplusplus)` near the top (line ~35), then existing target definitions, then near line 252: `find_package(tree-sitter)` and `include(${CMAKE_SOURCE_DIR}/cmake/grammars.cmake)`. The existing targets sit between them.

We want the *final* top-of-file order to be: all three `find_package` calls grouped, `include(grammars.cmake)` immediately after, the six `add_subdirectory()` calls below that, and existing target definitions begin only after the `add_subdirectory` block.

Concretely:
1. **Move `find_package(tree-sitter REQUIRED)`** up so it sits next to `find_package(md4c)` and `find_package(tomlplusplus)` — the three form a contiguous block. (`grammars.cmake` references `tree-sitter::tree-sitter`, so this find_package MUST run before the include — moving it up preserves the precondition.)
2. **Move `include(${CMAKE_SOURCE_DIR}/cmake/grammars.cmake)`** up to immediately after the find_package block.
3. **Add the six `add_subdirectory(...)` calls** immediately after the include:

```cmake
add_subdirectory(src/runtime)
add_subdirectory(src/core_dll)
add_subdirectory(src/plugin_md)
add_subdirectory(src/plugin_colorizer)
add_subdirectory(src/tools/screenshot)
add_subdirectory(tests)
```

4. **Remove the now-empty section comment** (e.g., `# --- Grammar DLLs via FetchContent ---`) where the include used to live.

The existing target declarations (`add_library(wlx-core STATIC ...)`, etc.) stay below; they'll be removed one-by-one in Phase 1 as their sources migrate.

**Why the move:** at Phase 1's end-state the root has no targets, just `find_package` + `include(grammars.cmake)` + `add_subdirectory()` blocks. Hoisting the `include` and `find_package(tree-sitter)` here in Phase 0.2 puts that final structure in place early so Phase 1 commits only need to delete target blocks, not also reorder.

- [ ] **Step 4: `BUILD_TEST_OK`**

Empty `add_subdirectory` is a no-op; build output unchanged.

- [ ] **Step 5: Commit**

```bash
git add src/runtime src/core_dll src/plugin_md src/plugin_colorizer src/tools/screenshot tests/CMakeLists.txt CMakeLists.txt
git commit -m "$(cat <<'EOF'
refactor(cmake): scaffold per-area CMakeLists for upcoming source moves

Empty placeholders that root's add_subdirectory now reaches.
Source files migrate into them in Phase 1.

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase 1 — File moves

Each task moves a logical group of files (all files belonging to one destination folder), updates the per-folder `CMakeLists.txt` to declare the relevant target sources, removes those sources from the root `CMakeLists.txt`, and sweeps `#include` references. Apply `MOVE_PATTERN` to each.

**Important:** during Phase 1, only paths change. **No type splits, no namespace additions, no behavioral changes.** Strict-A enforcement happens in Phase 2.

**Order matters:** move runtime/ files before plugin/ files (plugins #include runtime headers, so their includes need to be resolvable when the plugin sources move). Move `core_dll/` before plugins too.

### Task 1.1: Move `runtime/io/`

**Files:**
- Move: `src/file_service.h` → `src/runtime/io/file_service.h`
- Move: `src/file_service.cpp` → `src/runtime/io/file_service.cpp`
- Move: `tests/test_file_service.cpp` → `tests/runtime/io/test_file_service.cpp`

- [ ] **Step 1: `git mv` the source files**

```bash
mkdir -p src/runtime/io tests/runtime/io
git mv src/file_service.h src/runtime/io/file_service.h
git mv src/file_service.cpp src/runtime/io/file_service.cpp
git mv tests/test_file_service.cpp tests/runtime/io/test_file_service.cpp
```

- [ ] **Step 2: Sweep `#include "file_service.h"` → `#include "runtime/io/file_service.h"`**

```bash
grep -rln '"file_service.h"' src tests | xargs sed -i 's|"file_service.h"|"runtime/io/file_service.h"|g'
```

- [ ] **Step 3: Add to `src/runtime/CMakeLists.txt`**

Replace placeholder content with:

```cmake
# wlx-core static library — markdown plugin runtime.
# Filled incrementally during Phase 1.

add_library(wlx-core STATIC
    io/file_service.cpp
)

target_include_directories(wlx-core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(wlx-core
    PUBLIC
        md4c::md4c
        d2d1
        dwrite
        wlx-listerine-core
    PRIVATE
        tomlplusplus::tomlplusplus
)
```

- [ ] **Step 4: Remove `add_library(wlx-core STATIC …)` block from root `CMakeLists.txt`**

The root's `add_library(wlx-core STATIC src/file_service.cpp src/markdown_parser.cpp …)` and its companion `target_include_directories`/`target_link_libraries` calls move *into* `src/runtime/CMakeLists.txt`. Remove them from root entirely. (Subsequent Phase 1 tasks will append more sources to `src/runtime/CMakeLists.txt`'s `add_library` list.)

- [ ] **Step 5: Update `tests/CMakeLists.txt`**

Replace placeholder with:

```cmake
find_package(doctest REQUIRED)

add_executable(tests
    test_main.cpp
    runtime/io/test_file_service.cpp
)
target_link_libraries(tests PRIVATE wlx-core doctest::doctest comctl32)

add_executable(colorizer-tests
    test_main.cpp
)
target_link_libraries(colorizer-tests PRIVATE wlx-listerine-core doctest::doctest)
```

(Subsequent tasks append to the source lists.) `tests/test_main.cpp` doesn't move yet — it's the doctest entrypoint shared by both exes.

- [ ] **Step 6: Remove `add_executable(tests …)` and `add_executable(colorizer-tests …)` from root `CMakeLists.txt`**

The root no longer declares these targets.

- [ ] **Step 7: `BUILD_TEST_OK`**

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
refactor(layout): move file_service into runtime/io

Move src/file_service.{h,cpp} → src/runtime/io/. Test file mirrors
into tests/runtime/io/. Per-folder CMakeLists begins owning wlx-core
sources; tests/CMakeLists owns the test executables.

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 1.2: Move `runtime/parser/`

**Files:**
- Move: `src/markdown_parser.h` → `src/runtime/parser/markdown_parser.h`
- Move: `src/markdown_parser.cpp` → `src/runtime/parser/markdown_parser.cpp`
- Move: `src/document_model.h` → `src/runtime/parser/document_model.h`
- Move: `tests/test_markdown_parser.cpp` → `tests/runtime/parser/test_markdown_parser.cpp`
- Move: `tests/test_document_model.cpp` → `tests/runtime/parser/test_document_model.cpp`

- [ ] **Step 1: `git mv` source + tests**

```bash
mkdir -p src/runtime/parser tests/runtime/parser
git mv src/markdown_parser.h src/runtime/parser/markdown_parser.h
git mv src/markdown_parser.cpp src/runtime/parser/markdown_parser.cpp
git mv src/document_model.h src/runtime/parser/document_model.h
git mv tests/test_markdown_parser.cpp tests/runtime/parser/test_markdown_parser.cpp
git mv tests/test_document_model.cpp tests/runtime/parser/test_document_model.cpp
```

- [ ] **Step 2: Sweep includes**

```bash
grep -rln '"markdown_parser.h"' src tests | xargs sed -i 's|"markdown_parser.h"|"runtime/parser/markdown_parser.h"|g'
grep -rln '"document_model.h"' src tests | xargs sed -i 's|"document_model.h"|"runtime/parser/document_model.h"|g'
```

- [ ] **Step 3: Add to `src/runtime/CMakeLists.txt`'s `add_library(wlx-core …)` list**

```cmake
add_library(wlx-core STATIC
    io/file_service.cpp
    parser/markdown_parser.cpp
)
```

- [ ] **Step 4: Add to `tests/CMakeLists.txt`'s `add_executable(tests …)` list**

```cmake
add_executable(tests
    test_main.cpp
    runtime/io/test_file_service.cpp
    runtime/parser/test_markdown_parser.cpp
    runtime/parser/test_document_model.cpp
)
```

- [ ] **Step 5: `BUILD_TEST_OK`**

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor(layout): move parser into runtime/parser

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Tasks 1.3 – 1.13: Remaining `runtime/` moves

Apply the same pattern (`mkdir`, `git mv`, sweep includes, append to CMakeLists, build/test, commit) for each subfolder. Each task is one commit.

| # | Subfolder | Source files (from `src/`) | Test files (from `tests/`) |
|---|---|---|---|
| 1.3 | `runtime/layout/` | `layout_engine.{h,cpp}` | `test_layout_engine.cpp` |
| 1.4 | `runtime/render/` | `render_engine.{h,cpp}` | (none) |
| 1.5 | `runtime/cache/` | `cache_service.{h,cpp}` | `test_cache_service.cpp` |
| 1.6 | `runtime/theme/` | `theme_service.{h,cpp}` | `test_theme_service.cpp` |
| 1.7 | `runtime/interaction/` | `interaction_engine.{h,cpp}`, `text_selection.cpp` | `test_text_selection.cpp` |
| 1.8 | `runtime/search/` | `search_engine.{h,cpp}`, `search_ops.h`, `search_counter_format.h`, `search_hud.{h,cpp}`, `search_hud_painter.{h,cpp}` | `test_search_engine.cpp`, `test_search_ops.cpp`, `test_search_counter_format.cpp`, `test_search_hud_painter.cpp` |
| 1.9 | `runtime/host/` | `wlx_host_common.h` | `test_wlx_host_common.cpp` |
| 1.10 | `runtime/diagnostics/` | `wlx_trace.h` | (none) |
| 1.11 | `runtime/util/` | `string_util.h` | (none) |

For each task above:

- [ ] **Step 1:** `mkdir -p src/runtime/<sub> tests/runtime/<sub>` (skip tests/ subdir if no tests).
- [ ] **Step 2:** `git mv` each source and test file (use the table above).
- [ ] **Step 3:** Sweep includes for each moved header. For example, for Task 1.3:

  ```bash
  grep -rln '"layout_engine.h"' src tests | xargs sed -i 's|"layout_engine.h"|"runtime/layout/layout_engine.h"|g'
  ```

- [ ] **Step 4:** Append the moved `.cpp` files to `src/runtime/CMakeLists.txt`'s `add_library(wlx-core …)` list using the new relative paths (e.g., `layout/layout_engine.cpp`).
- [ ] **Step 5:** Append the moved tests to `tests/CMakeLists.txt`'s `add_executable(tests …)` list.
- [ ] **Step 6:** `BUILD_TEST_OK`.
- [ ] **Step 7:** Commit with message `refactor(layout): move <subsystem> into runtime/<sub>`.

**Special note for Task 1.7 (`runtime/interaction/`):** `text_selection.cpp` has no `.h` today (free functions declared inside the `.cpp`). Move just the `.cpp`. The header creation happens in Phase 2 (when we make `text_selection.h` declaring the free functions).

**Special note for Task 1.8 (`runtime/search/`):** `search_engine.cpp` declares both `SearchIndex` and free helpers. The class extraction happens in Phase 2.

**Special note for Task 1.9 (`runtime/host/`):** `wlx_host_common.h` is a single multi-component header (concept + class template + internal probe). Move it as-is in Phase 1; split into `host_view.h` + `host_integration.h` in Phase 2.

---

### Task 1.14: Move `core_dll/` files

**Files (all from `src/colorizer/`):**

| Destination | Source files |
|---|---|
| `src/core_dll/abi/` | `wlx_core_abi.cpp`, `dllmain.cpp` |
| `src/core_dll/colorizer/` | `colorizer.{h,cpp}` |
| `src/core_dll/grammar/` | `grammar_registry.{h,cpp}`, `grammar_cache.{h,cpp}` |
| `src/core_dll/highlighting/` | `query_highlighter.{h,cpp}` |
| `src/core_dll/theme/` | `helix_theme.{h,cpp}` |
| `src/core_dll/registry/` | `core_registry.{h,cpp}`, `core_config.{h,cpp}` |

Tests (all from `tests/`):

| Destination | Test files |
|---|---|
| `tests/core_dll/abi/` | `test_wlx_core_abi.cpp` |
| `tests/core_dll/colorizer/` | `test_colorizer.cpp` |
| `tests/core_dll/grammar/` | `test_colorizer_grammar.cpp`, `test_colorizer_grammars.cpp`, `test_grammar_cache.cpp` |
| `tests/core_dll/highlighting/` | `test_colorizer_query_highlighter.cpp` |
| `tests/core_dll/theme/` | `test_colorizer_helix_theme.cpp` |
| `tests/core_dll/registry/` | `test_core_config.cpp` |

This is one logically-related move (all the colorizer DLL contents). Make it one commit, broken into clear steps.

- [ ] **Step 1: Create folders**

```bash
mkdir -p src/core_dll/{abi,colorizer,grammar,highlighting,theme,registry}
mkdir -p tests/core_dll/{abi,colorizer,grammar,highlighting,theme,registry}
```

- [ ] **Step 2: `git mv` everything per the tables above**

(20 file moves — list them out one per line in your shell history.)

- [ ] **Step 3: Sweep includes**

For each moved header:

```bash
for hdr in colorizer.h grammar_registry.h grammar_cache.h query_highlighter.h helix_theme.h core_registry.h core_config.h colorizer_routing.h; do
    case "$hdr" in
        colorizer.h)        new="core_dll/colorizer/colorizer.h" ;;
        grammar_registry.h) new="core_dll/grammar/grammar_registry.h" ;;
        grammar_cache.h)    new="core_dll/grammar/grammar_cache.h" ;;
        query_highlighter.h) new="core_dll/highlighting/query_highlighter.h" ;;
        helix_theme.h)      new="core_dll/theme/helix_theme.h" ;;
        core_registry.h)    new="core_dll/registry/core_registry.h" ;;
        core_config.h)      new="core_dll/registry/core_config.h" ;;
    esac
    grep -rln "\"$hdr\"" src tests | xargs -r sed -i "s|\"$hdr\"|\"$new\"|g"
done
```

`colorizer_routing.h` does *not* move yet — it goes to `plugin_colorizer/language/` in Task 1.15.

- [ ] **Step 4: Populate `src/core_dll/CMakeLists.txt`**

```cmake
# wlx-listerine-core shared DLL.
add_library(wlx-listerine-core SHARED
    abi/wlx_core_abi.cpp
    abi/dllmain.cpp
    colorizer/colorizer.cpp
    grammar/grammar_registry.cpp
    grammar/grammar_cache.cpp
    highlighting/query_highlighter.cpp
    theme/helix_theme.cpp
    registry/core_registry.cpp
    registry/core_config.cpp
)

target_include_directories(wlx-listerine-core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
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

# Mirror the DLL alongside test exes / screenshot_tool.
add_custom_command(TARGET wlx-listerine-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:wlx-listerine-core>"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/wlx-listerine-core.dll"
)

# Mirror grammars and themes alongside the DLL.
add_custom_command(TARGET wlx-listerine-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/grammars"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/grammars"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/config/themes"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/themes"
)

# Ship wlx-listerine-core.toml next to the core DLL.
add_custom_command(TARGET wlx-listerine-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-core.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-core.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-core.toml"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/wlx-listerine-core.toml"
)
```

- [ ] **Step 5: Remove the corresponding `add_library(wlx-listerine-core SHARED ...)` block plus all its `target_*` and `add_custom_command` from root `CMakeLists.txt`**

Root no longer declares `wlx-listerine-core`.

- [ ] **Step 6: Append moved test files to `tests/CMakeLists.txt`'s `add_executable(colorizer-tests …)` list**

```cmake
add_executable(colorizer-tests
    test_main.cpp
    core_dll/abi/test_wlx_core_abi.cpp
    core_dll/colorizer/test_colorizer.cpp
    core_dll/grammar/test_colorizer_grammar.cpp
    core_dll/grammar/test_colorizer_grammars.cpp
    core_dll/grammar/test_grammar_cache.cpp
    core_dll/highlighting/test_colorizer_query_highlighter.cpp
    core_dll/theme/test_colorizer_helix_theme.cpp
    core_dll/registry/test_core_config.cpp
)
```

(`test_colorizer_routing.cpp` moves to `tests/plugin_colorizer/` in the next task.)

- [ ] **Step 7: Remove the `add_executable(colorizer-tests …)` block from root `CMakeLists.txt`**

- [ ] **Step 8: `BUILD_TEST_OK`**

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "refactor(layout): move colorizer engine into core_dll/

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 1.15: Move `plugin_colorizer/` files

**Files (all from `src/colorizer/`):**

| Destination | Source files |
|---|---|
| `src/plugin_colorizer/window/` | `colorizer_host_adapter.cpp` |
| `src/plugin_colorizer/layout/` | `colorizer_layout.{h,cpp}` |
| `src/plugin_colorizer/language/` | `colorizer_routing.h` |
| `src/plugin_colorizer/exports/` | `colorizer_plugin.def` (rename to `plugin.def` after move) |
| `src/plugin_colorizer/resource/` | `colorizer_resource.h`, `colorizer_resource.rc.in` |
| `tests/plugin_colorizer/language/` | `test_colorizer_routing.cpp` |

- [ ] **Step 1: Create folders, `git mv`**

```bash
mkdir -p src/plugin_colorizer/{window,layout,language,exports,resource}
mkdir -p tests/plugin_colorizer/language
git mv src/colorizer/colorizer_host_adapter.cpp src/plugin_colorizer/window/
git mv src/colorizer/colorizer_layout.h src/plugin_colorizer/layout/
git mv src/colorizer/colorizer_layout.cpp src/plugin_colorizer/layout/
git mv src/colorizer/colorizer_routing.h src/plugin_colorizer/language/routing.h
git mv src/colorizer/colorizer_plugin.def src/plugin_colorizer/exports/plugin.def
git mv src/colorizer/colorizer_resource.h src/plugin_colorizer/resource/
git mv src/colorizer/colorizer_resource.rc.in src/plugin_colorizer/resource/
git mv tests/test_colorizer_routing.cpp tests/plugin_colorizer/language/test_colorizer_routing.cpp
```

- [ ] **Step 2: Sweep includes**

```bash
grep -rln '"colorizer_layout.h"' src tests | xargs -r sed -i 's|"colorizer_layout.h"|"plugin_colorizer/layout/colorizer_layout.h"|g'
grep -rln '"colorizer_routing.h"' src tests | xargs -r sed -i 's|"colorizer_routing.h"|"plugin_colorizer/language/routing.h"|g'
grep -rln '"colorizer_resource.h"' src tests | xargs -r sed -i 's|"colorizer_resource.h"|"plugin_colorizer/resource/colorizer_resource.h"|g'
```

- [ ] **Step 3: Update root `CMakeLists.txt` `configure_file` for the colorizer resource**

```cmake
configure_file(
    "${CMAKE_SOURCE_DIR}/src/plugin_colorizer/resource/colorizer_resource.rc.in"
    "${CMAKE_BINARY_DIR}/generated/colorizer_resource.rc"
    @ONLY
)
```

- [ ] **Step 4: Populate `src/plugin_colorizer/CMakeLists.txt`**

```cmake
# wlx-listerine-colorizer plugin DLL.
add_library(wlx-listerine-colorizer SHARED
    window/colorizer_host_adapter.cpp
    layout/colorizer_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/search/search_hud.cpp     # shared with md plugin until Phase 4 lifts
    exports/plugin.def
    "${CMAKE_BINARY_DIR}/generated/colorizer_resource.rc"
)

target_include_directories(wlx-listerine-colorizer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

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

# Output staging — runs as the last per-plugin POST_BUILD; mirrors today's behavior.
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
```

- [ ] **Step 5: Remove `add_library(wlx-listerine-colorizer SHARED …)` block from root `CMakeLists.txt`**

- [ ] **Step 6: Append `tests/plugin_colorizer/language/test_colorizer_routing.cpp` to `colorizer-tests` source list**

- [ ] **Step 7: `BUILD_TEST_OK`**

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "refactor(layout): move colorizer plugin shell into plugin_colorizer/

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 1.16: Move `plugin_md/` files

**Files:**

| Destination | Source files |
|---|---|
| `src/plugin_md/window/` | `host_adapter.{h,cpp}` |
| `src/plugin_md/exports/` | `plugin.def` |
| `src/plugin_md/resource/` | `resource.h`, `resource.rc.in` |

Note: `src/search_hud.cpp` is currently in the md plugin's source list AND the colorizer's. After Phase 1, it lives in `src/runtime/search/search_hud.cpp` (moved in Task 1.8). Both plugin CMakeLists reference it from there. After Phase 4 the lift may consolidate further; for now both plugins depend on it through `wlx-core`.

- [ ] **Step 1: Create folders, `git mv`**

```bash
mkdir -p src/plugin_md/{window,exports,resource}
git mv src/host_adapter.h src/plugin_md/window/host_adapter.h
git mv src/host_adapter.cpp src/plugin_md/window/host_adapter.cpp
git mv src/plugin.def src/plugin_md/exports/plugin.def
git mv src/resource.h src/plugin_md/resource/resource.h
git mv src/resource.rc.in src/plugin_md/resource/resource.rc.in
```

- [ ] **Step 2: Sweep includes**

```bash
grep -rln '"host_adapter.h"' src tests | xargs -r sed -i 's|"host_adapter.h"|"plugin_md/window/host_adapter.h"|g'
grep -rln '"resource.h"' src tests | xargs -r sed -i 's|"resource.h"|"plugin_md/resource/resource.h"|g'
```

- [ ] **Step 3: Update root `configure_file` for the md plugin resource**

```cmake
configure_file(
    "${CMAKE_SOURCE_DIR}/src/plugin_md/resource/resource.rc.in"
    "${CMAKE_BINARY_DIR}/generated/resource.rc"
    @ONLY
)
```

- [ ] **Step 4: Populate `src/plugin_md/CMakeLists.txt`**

```cmake
# wlx-listerine-md plugin DLL.
add_library(wlx-listerine-md SHARED
    window/host_adapter.cpp
    exports/plugin.def
    "${CMAKE_BINARY_DIR}/generated/resource.rc"
)

target_include_directories(wlx-listerine-md PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

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
```

- [ ] **Step 5: Remove `add_library(wlx-listerine-md SHARED …)` block from root**

- [ ] **Step 6: `BUILD_TEST_OK`**

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor(layout): move md plugin shell into plugin_md/

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 1.17: Move `tools/screenshot/` files

- [ ] **Step 1: `git mv`**

```bash
git mv src/screenshot_main.cpp src/tools/screenshot/main.cpp
```

- [ ] **Step 2: Populate `src/tools/screenshot/CMakeLists.txt`**

```cmake
add_executable(screenshot_tool
    main.cpp
)

target_include_directories(screenshot_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(screenshot_tool PRIVATE
    wlx-core
    windowscodecs
    ole32
)
```

- [ ] **Step 3: Remove `add_executable(screenshot_tool …)` block from root**

- [ ] **Step 4: `BUILD_TEST_OK`**

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor(layout): move screenshot tool into tools/screenshot/

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 1.18: Move + rename `text_modifiers.h` → `include/wlx_core/text_modifier.h`

The existing `text_modifiers.h` declares `enum TextModifier`. Per strict-A, the filename matches the type name (singular). Per the spec, this is engine-vocabulary so it lives in the public ABI.

- [ ] **Step 1: `git mv` + rename**

```bash
git mv src/text_modifiers.h include/wlx_core/text_modifier.h
```

- [ ] **Step 2: Sweep includes**

```bash
grep -rln '"text_modifiers.h"' src tests | xargs -r sed -i 's|"text_modifiers.h"|"wlx_core/text_modifier.h"|g'
```

- [ ] **Step 3: `BUILD_TEST_OK`**

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "refactor(layout): rename text_modifiers.h to text_modifier.h, move to public ABI

Singular filename matches enum name; lives under include/wlx_core/
since it's the engine's output vocabulary consumed by runtime/+plugins.

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 1.19: Move test_main.cpp into shared root

`tests/test_main.cpp` is the doctest entrypoint shared by both test exes. It stays at `tests/test_main.cpp` but verify the include path still works after all the test moves.

- [ ] **Step 1: Verify**

```bash
cat tests/test_main.cpp
```

If it has any `#include` of moved headers, sweep them. (Likely it only has `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` plus `<doctest/doctest.h>` — no project headers to update.)

- [ ] **Step 2: `BUILD_TEST_OK` — full test runs.**

- [ ] **Step 3: No commit needed if no changes.**

---

### Phase 1 acceptance

After Phase 1:

- `src/` contains only the five top-level folders (`runtime/`, `core_dll/`, `plugin_md/`, `plugin_colorizer/`, `tools/`) plus `src/colorizer/` empty (verify and `rmdir`).
- `tests/` contains only `test_main.cpp` plus the four mirrored top-level test folders.
- Root `CMakeLists.txt` is ~80 lines: `cmake_minimum_required`, `project()`, version parsing, `find_package` calls, `option`, `configure_file`, `set(CMAKE_CXX_STANDARD …)`, `include(cmake/grammars.cmake)`, six `add_subdirectory` calls. **No `add_library`, `add_executable`, or `target_*` calls in root.**
- `BUILD_TEST_OK` + visual regression all pass.

Run `bash scripts/visual-test.sh` once at the end of Phase 1 to confirm the markdown plugin still renders identically to its goldens.

---

## Phase 2 — Header splits (strict one-type-per-file)

For each multi-class header, split each top-level type into its own file. The pattern: create per-type headers, move the type declaration verbatim into the new file, replace the original header's body with `#include` lines pointing at the new files (so consumers using the old header still compile, then sweep consumers to point at the specific files in a follow-up step within the same task), then build/test/commit.

### Task 2.1: Split `runtime/layout/layout_engine.h`

Today's `layout_engine.h` has 8 POD types + the `LayoutEngine` class. Strict-A: each gets its own file.

**Files (all in `src/runtime/layout/`):**
- Create: `color_range.h`, `code_bg_rect.h`, `text_run.h`, `interactive_span.h`, `anchor_entry.h`, `layout_block.h`, `layout_document.h`, `text_position.h`
- Modify: `layout_engine.h` (becomes class-only)

- [ ] **Step 1: Read the current `layout_engine.h`**

```bash
cat src/runtime/layout/layout_engine.h
```

Identify each top-level struct/class declaration. Confirm it matches the spec (8 POD types + `LayoutEngine`).

- [ ] **Step 2: Create `color_range.h`**

```cpp
#pragma once

#include <cstdint>

#include "wlx_core/text_modifier.h"

struct ColorRange {
    uint32_t start;
    uint32_t length;
    uint32_t color;
    uint8_t modifiers;  // TextModifier bit flags
};
```

(Adjust fields to match exactly what's in the source today — copy the struct verbatim.)

- [ ] **Step 3: Repeat for each of `code_bg_rect.h`, `text_run.h`, `interactive_span.h`, `anchor_entry.h`, `layout_block.h`, `layout_document.h`, `text_position.h`**

For each file:
- `#pragma once` at top
- minimal `#include`s (cstdint, string, vector, plus any sibling type headers needed)
- the struct declaration verbatim
- forward-declare or `#include` only what the *struct* needs, not the whole engine

Order matters because `LayoutBlock` references `TextRun`, `InteractiveSpan`, `AnchorEntry`; `LayoutDocument` references `LayoutBlock`. Each file `#include`s its dependencies.

- [ ] **Step 4: Trim `layout_engine.h` to class-only**

The file should now contain only:

```cpp
#pragma once

#include "runtime/layout/layout_document.h"
// any other transitive headers needed by the LayoutEngine signature

class LayoutEngine {
    // body unchanged
};
```

Remove the 8 struct declarations (now in their own files).

- [ ] **Step 5: Sweep consumer includes**

A consumer that previously did `#include "runtime/layout/layout_engine.h"` to get `LayoutBlock` should now `#include "runtime/layout/layout_block.h"` directly. Find consumers:

```bash
grep -rln 'LayoutBlock\|TextRun\|InteractiveSpan\|AnchorEntry\|ColorRange\|CodeBgRect\|LayoutDocument\|TextPosition' src tests
```

For each consumer file, verify it has the appropriate `#include` line for the type(s) it uses. Add the new specific include; remove the broad `runtime/layout/layout_engine.h` include if the consumer doesn't actually use `LayoutEngine`.

- [ ] **Step 6: `BUILD_TEST_OK`**

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor(split): one type per file in runtime/layout/

Split layout_engine.h's 8 POD types into per-type headers.
LayoutEngine class header now only declares the class.

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 2.2: Split `runtime/parser/document_model.h`

Same pattern as Task 2.1. Types to extract (each gets its own header in `runtime/parser/`):
- `source_range.h` — `struct SourceRange`
- `link_target.h` — `struct LinkTarget` (with its nested `Kind` enum stays inside)
- `inline_node.h` — `struct InlineNode`
- `block_node.h` — `struct BlockNode`
- `document.h` — `struct Document`

After the split, `document_model.h` becomes a convenience aggregate header:

```cpp
#pragma once

#include "runtime/parser/source_range.h"
#include "runtime/parser/link_target.h"
#include "runtime/parser/inline_node.h"
#include "runtime/parser/block_node.h"
#include "runtime/parser/document.h"
```

…or it gets removed entirely if no consumer relies on it. Decide based on consumer count. **Recommendation:** remove `document_model.h` after Phase 2 — every consumer should include the specific type it uses. The convenience aggregate is technical debt by another name.

- [ ] **Step 1: Read `document_model.h`, identify all types**
- [ ] **Step 2: Create the 5 per-type headers**
- [ ] **Step 3: Sweep consumers — replace `#include "runtime/parser/document_model.h"` with the specific types each consumer needs**
- [ ] **Step 4: `git rm src/runtime/parser/document_model.h`** (after step 3 verifies no consumers)
- [ ] **Step 5: `BUILD_TEST_OK`**
- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor(split): one type per file in runtime/parser/

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 2.3: Split `runtime/cache/cache_service.h`

Today's `cache_service.h` has `ParseCacheKey`, `LayoutCacheKey`, their hash specializations, and the `CacheService` class.

Per spec Exception 2: hash specs live in the same file as their key.

**Files:**
- Create: `parse_cache_key.h` — `ParseCacheKey` struct + `std::hash<ParseCacheKey>` specialization
- Create: `layout_cache_key.h` — `LayoutCacheKey` struct + `std::hash<LayoutCacheKey>` specialization
- Modify: `cache_service.h` — class only, includes the two key headers

Apply the same 6-step pattern as Task 2.1. Commit message: `refactor(split): one type per file in runtime/cache/`.

---

### Task 2.4: Split `runtime/theme/theme_service.h`

Types to extract:
- `color_palette.h` — `ColorPalette`
- `spacing_config.h` — `SpacingConfig`
- `font_config.h` — `FontConfig`
- `theme_config.h` — `ThemeConfig`

Modify `theme_service.h` to contain only the `ThemeService` class plus the necessary `#include`s.

Same 6-step pattern. Commit: `refactor(split): one type per file in runtime/theme/`.

---

### Task 2.5: Split `runtime/io/file_service.h`

Types to extract:
- `file_identity.h` — `FileIdentity`
- `file_content.h` — `FileContent`

`file_service.h` becomes class-only.

---

### Task 2.6: Split `runtime/search/search_engine.h` and `search_hud_painter.h`

`search_engine.h` types:
- `search_match.h` — `SearchMatch`
- `search_query.h` — `SearchQuery`
- `search_index.h` (+ `.cpp`) — `SearchIndex` class (move its impl out of `search_engine.cpp` if both live there today)

`search_engine.h` becomes a header for any remaining free helper declarations, OR is removed if SearchIndex was the only behavior. Audit when you read the file.

`search_hud_painter.h` types:
- `search_hud_state.h` — `SearchHudState`
- `search_hud_hit_rects.h` — `SearchHudHitRects`

`search_hud_painter.h` becomes class-only.

---

### Task 2.7: Split `core_dll/colorizer/colorizer.h`

Types to extract:
- `color_span.h` — `ColorSpan`
- `colorize_result.h` — `ColorizeResult`

`colorizer.h` becomes the `Colorizer` class only.

---

### Task 2.8: Split `core_dll/theme/helix_theme.h`

Types to extract:
- `resolved_style.h` — `ResolvedStyle`

`helix_theme.h` becomes the `HelixTheme` class only.

---

### Task 2.9: Split `runtime/host/wlx_host_common.h`

Today's file has the `HostView` concept and the `HostIntegration<V>` class template.

**Files:**
- Create: `host_view.h` — `template<typename V> concept HostView = …;` and the internal `ConceptProbe`
- Create: `host_integration.h` — `template <HostView V> class HostIntegration { … };` plus all the inline template method definitions (today's file's bottom 200 lines)
- Delete: `wlx_host_common.h`

Sweep `#include "runtime/host/wlx_host_common.h"` consumers — replace with the specific include they need (usually `host_integration.h` since they use the class).

---

### Task 2.10: Create `runtime/interaction/text_selection.h`

`text_selection.cpp` declares its free functions inline today. Create a public header.

- [ ] **Step 1: Read `text_selection.cpp` — list every non-static free function**
- [ ] **Step 2: Create `runtime/interaction/text_selection.h`** with declarations:

```cpp
#pragma once

#include <string>

#include "runtime/layout/layout_document.h"
#include "runtime/layout/text_position.h"

std::wstring extract_selected_text(const LayoutDocument& layout,
                                   TextPosition start, TextPosition end);
// (declare every other public free function from text_selection.cpp here)
```

- [ ] **Step 3: Update `text_selection.cpp` to `#include "runtime/interaction/text_selection.h"`**
- [ ] **Step 4: Sweep consumers** — anything that called `extract_selected_text` should now `#include "runtime/interaction/text_selection.h"`
- [ ] **Step 5: `BUILD_TEST_OK`**
- [ ] **Step 6: Commit**

---

### Phase 2 acceptance

After Phase 2:

- Every public type lives in its own file with that type's name.
- No header declares more than one type (with the documented exceptions: nested types, hash specs, free-function modules).
- `BUILD_TEST_OK` + visual regression.

---

## Phase 3 — Add namespaces

One commit per top-level area. Each commit wraps everything in that area's headers and `.cpp` files in the appropriate namespace, then sweeps call sites in consuming binaries.

### Task 3.1: Add `wlx::runtime::*` namespaces

**Strategy:** for each `runtime/<sub>/` folder, wrap every `.h` and `.cpp` in:

```cpp
namespace wlx::runtime::<sub> {
    // existing content
}
```

Then in **every consumer file** (across `core_dll`, `plugin_md`, `plugin_colorizer`, `tools`, `tests`), either:

- Use fully-qualified names: `wlx::runtime::layout::LayoutEngine`, OR
- Add `using namespace wlx::runtime::layout;` near the top of the consuming `.cpp` (never in headers).

For minimal disruption: prefer `using` directives in `.cpp` consumers; require fully-qualified names in headers (which can't pollute consumers' namespaces).

- [ ] **Step 1: Apply namespace wrappers across `src/runtime/`**

For each subfolder, edit each `.h` and `.cpp` to add the namespace block. Sample for `runtime/layout/layout_block.h`:

```cpp
#pragma once
#include <cstdint>

namespace wlx::runtime::layout {

struct LayoutBlock {
    // existing fields
};

}  // namespace wlx::runtime::layout
```

For `.cpp` files, the namespace also wraps file-local helpers and the function definitions. Example for `layout_engine.cpp`:

```cpp
#include "runtime/layout/layout_engine.h"
// ... other includes ...

namespace wlx::runtime::layout {

namespace {  // anonymous — file-local
    // (move today's `static void foo(...)` here as `void foo(...)`)
}

LayoutEngine::LayoutEngine() { ... }
// ... other definitions ...

}  // namespace wlx::runtime::layout
```

Replace today's `static` qualifiers on file-local functions with anonymous namespaces inside the named namespace.

- [ ] **Step 2: Sweep consuming `.cpp` files**

In each `.cpp` file outside `src/runtime/`, after the includes, add:

```cpp
using namespace wlx::runtime::parser;
using namespace wlx::runtime::layout;
using namespace wlx::runtime::render;
using namespace wlx::runtime::io;
using namespace wlx::runtime::cache;
using namespace wlx::runtime::theme;
using namespace wlx::runtime::interaction;
using namespace wlx::runtime::search;
using namespace wlx::runtime::host;
using namespace wlx::runtime::diagnostics;
using namespace wlx::runtime::util;
```

Use only the ones the file actually needs — `using namespace` for the ones that have used types in this `.cpp`. Aggressive importing is fine in plugin and test `.cpp` files; minimal in headers.

- [ ] **Step 3: Header consumers must use qualified names**

Headers (e.g., `core_dll/colorizer/colorizer.h`) that reference runtime types must qualify them: `wlx::runtime::layout::LayoutDocument`. No `using namespace` in headers.

- [ ] **Step 4: `BUILD_TEST_OK`**

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor(ns): wrap runtime/ in wlx::runtime::* namespaces

Each runtime/<sub>/ folder is its own ns. Consumers add 'using namespace'
in .cpp files; headers use fully-qualified names. File-local statics
become anonymous-namespace members.

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Tasks 3.2 – 3.5: Repeat for other top-level areas

Apply the same pattern, one area per commit:

- **Task 3.2:** `wlx::core::*` for `src/core_dll/`. Subfolders → namespaces: `abi`, `colorizer`, `grammar`, `highlighting`, `theme`, `registry`. **Exception:** the `extern "C"` ABI declarations in `core_dll/abi/wlx_core_abi.cpp` and the `DllMain` in `core_dll/abi/dllmain.cpp` stay outside any namespace.
- **Task 3.3:** `wlx::plugin_md::*` for `src/plugin_md/`. Subfolders: `window`, `exports`, `resource`. **Exception:** `extern "C"` WLX exports in `plugin_md/exports/wlx_exports.cpp` and `DllMain` in `plugin_md/window/dllmain.cpp` stay outside namespaces.
- **Task 3.4:** `wlx::plugin_colorizer::*` for `src/plugin_colorizer/`. Same exceptions.
- **Task 3.5:** `wlx::tools::screenshot` for `src/tools/screenshot/main.cpp` (the `int main()` function stays outside the namespace).

For each: edit, sweep consumers if any cross-area calls, build, test, commit.

### Phase 3 acceptance

- Every type and function (except `extern "C"` exports and `main`) lives inside a namespace whose path matches its folder.
- `using namespace` appears only in `.cpp` files.
- File-local statics replaced with anonymous namespaces.
- `BUILD_TEST_OK` + visual regression.

---

## Phase 4 — Lift duplicate host helpers into `runtime/host/`

This is the highest-risk phase. Each lifted helper is its own commit. After every commit: full build + both test suites + visual regression for the markdown plugin + manual smoke test of the colorizer plugin in TC (since there's no automated visual-regression for it).

The lifts share a pattern:

**Pattern (`LIFT_PATTERN`):**

1. Read both source copies (`plugin_md/window/host_adapter.cpp` and `plugin_colorizer/window/colorizer_host_adapter.cpp`).
2. Confirm behavioral equivalence — same input/output, no unexpected branches.
3. Create the new file in `src/runtime/host/`. Templatize on a concept if the helper depends on per-plugin `ViewState` fields. Otherwise it's a free function.
4. Delete both source-side copies. Replace call sites with calls to the new function.
5. `BUILD_TEST_OK` + `./scripts/visual-test.sh` + manual TC smoke test (open both an .md and a .cpp file).
6. Commit.

### Task 4.1: Lift `apply_dark_mode`

The two source-side implementations are likely byte-for-byte identical (no `ViewState` reference).

**Files:**
- Create: `src/runtime/host/dark_mode.h` and `dark_mode.cpp`
- Modify: `src/plugin_md/window/host_adapter.cpp` — remove the `static void apply_dark_mode` definition, add `#include "runtime/host/dark_mode.h"`
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp` — same

- [ ] **Step 1: Read both copies**

```bash
grep -A 8 'apply_dark_mode' src/plugin_md/window/host_adapter.cpp
grep -A 8 'apply_dark_mode' src/plugin_colorizer/window/colorizer_host_adapter.cpp
```

Confirm they're equivalent. If they aren't, **stop and audit** — the spec assumes equivalence; a divergence is a behavior bug to investigate before lifting.

- [ ] **Step 2: Create `src/runtime/host/dark_mode.h`**

```cpp
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace wlx::runtime::host {

void apply_dark_mode(HWND hwnd, bool dark);

}  // namespace wlx::runtime::host
```

- [ ] **Step 3: Create `src/runtime/host/dark_mode.cpp`**

Copy the function body from one of the host adapters. Wrap it in `namespace wlx::runtime::host { ... }`. Add the file to `src/runtime/CMakeLists.txt`'s `add_library(wlx-core …)` source list (`host/dark_mode.cpp`).

- [ ] **Step 4: Remove `static void apply_dark_mode` from both host adapters**

In each `.cpp`, delete the function. Add `#include "runtime/host/dark_mode.h"` near the top. Replace each call site (`apply_dark_mode(hwnd, dark)`) with `wlx::runtime::host::apply_dark_mode(hwnd, dark)` — OR add `using wlx::runtime::host::apply_dark_mode;` near the top of the `.cpp` so call sites stay short.

- [ ] **Step 5: `BUILD_TEST_OK` + `./scripts/visual-test.sh`**

- [ ] **Step 6: Manual TC smoke test**

Open Total Commander, navigate to a markdown file, F3 to view (markdown plugin loads), confirm dark mode follows the system theme. Repeat for a `.cpp` file (colorizer plugin). Both should respect dark mode identically to before.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
refactor(lift): apply_dark_mode into runtime/host/dark_mode

Removes one of ~12 duplicated host helpers across the two .wlx64s.
Both plugins now call wlx::runtime::host::apply_dark_mode.

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4.2: Lift `get_module_dir`

Pure helper, no `ViewState` reference. Same pattern as 4.1.

**Files:**
- Create: `src/runtime/host/module_path.{h,cpp}`

```cpp
// runtime/host/module_path.h
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace wlx::runtime::host {

std::wstring get_module_dir(HMODULE module);

}  // namespace wlx::runtime::host
```

`.cpp` carries the implementation copied verbatim from the host adapter. Both adapters: remove the static def, include the new header, qualify or `using` the call sites.

- [ ] **Step 1: Read** both copies, confirm equivalence.
- [ ] **Step 2: Create `module_path.{h,cpp}`** as above.
- [ ] **Step 3: Add to `src/runtime/CMakeLists.txt`**.
- [ ] **Step 4: Remove from both host adapters; replace call sites.**
- [ ] **Step 5: `BUILD_TEST_OK`** + visual regression.
- [ ] **Step 6: Commit** with message `refactor(lift): get_module_dir into runtime/host/module_path`.

---

### Task 4.3: Lift `copy_to_clipboard`

Pure HWND + wstring. Same pattern.

**Files:**
- Create: `src/runtime/host/clipboard.{h,cpp}`

```cpp
// runtime/host/clipboard.h
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace wlx::runtime::host {

bool copy_to_clipboard(HWND owner, const std::wstring& text);

}  // namespace wlx::runtime::host
```

Same flow. Commit: `refactor(lift): copy_to_clipboard into runtime/host/clipboard`.

---

### Task 4.4: Lift `hit_test_position` + `block_text_length`

Both operate on layout types, no `ViewState`. Group together.

**Files:**
- Create: `src/runtime/host/hit_test.{h,cpp}`

```cpp
// runtime/host/hit_test.h
#pragma once

#include "runtime/layout/layout_document.h"
#include "runtime/layout/layout_block.h"
#include "runtime/layout/text_position.h"

namespace wlx::runtime::host {

wlx::runtime::layout::TextPosition hit_test_position(
    const wlx::runtime::layout::LayoutDocument& layout, float x, float y);

int block_text_length(const wlx::runtime::layout::LayoutBlock& block);

}  // namespace wlx::runtime::host
```

`.cpp` copies both implementations verbatim, wrapped in the namespace. Both adapters: remove their static copies, include the new header, replace call sites.

Same flow. Commit: `refactor(lift): hit_test_position + block_text_length into runtime/host/hit_test`.

---

### Task 4.5: Lift `ensure_factories`

This is more involved — `ensure_factories` initializes file-scope global `ComPtr<ID2D1Factory>` and `ComPtr<IDWriteFactory>` singletons. Today each plugin has its own pair of globals. The lift consolidates into one pair owned by `runtime/host/factories`.

**Files:**
- Create: `src/runtime/host/factories.{h,cpp}`

```cpp
// runtime/host/factories.h
#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

namespace wlx::runtime::host {

void ensure_factories();
ID2D1Factory*  d2d_factory();
IDWriteFactory* dwrite_factory();

}  // namespace wlx::runtime::host
```

```cpp
// runtime/host/factories.cpp
#include "runtime/host/factories.h"

namespace wlx::runtime::host {

namespace {
    Microsoft::WRL::ComPtr<ID2D1Factory> g_d2d;
    Microsoft::WRL::ComPtr<IDWriteFactory> g_dwrite;
}

void ensure_factories() {
    if (!g_d2d)    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(g_d2d.GetAddressOf()));
    if (!g_dwrite) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(g_dwrite.GetAddressOf()));
}

ID2D1Factory*  d2d_factory()    { return g_d2d.Get(); }
IDWriteFactory* dwrite_factory() { return g_dwrite.Get(); }

}  // namespace wlx::runtime::host
```

In each host adapter:
- Remove the `static ComPtr<ID2D1Factory> g_d2d_factory` and `static ComPtr<IDWriteFactory> g_dwrite_factory` globals.
- Remove the `static void ensure_factories()` definition.
- Replace `g_d2d_factory.Get()` call sites with `wlx::runtime::host::d2d_factory()`.
- Replace `g_dwrite_factory.Get()` call sites with `wlx::runtime::host::dwrite_factory()`.

**Critical:** check `DLL_PROCESS_DETACH` handlers in both `dllmain.cpp` files. The CLAUDE.md memory note says **never `Release()` COM objects in `DLL_PROCESS_DETACH`** — the lifted globals follow the same rule. The spec section "Risks" already flags this. Verify the current detach handlers don't `Reset()` these globals; if they do, port that leak-on-purpose pattern into `factories.cpp` (e.g., a `leak_factories_on_detach()` function called from each plugin's DllMain).

Commit: `refactor(lift): D2D/DWrite factories consolidate into runtime/host/factories`.

---

### Task 4.6: Lift `ensure_window_class`

Each plugin registers a window class. The class name and WndProc differ; everything else is boilerplate.

**Files:**
- Create: `src/runtime/host/window_class.{h,cpp}`

```cpp
// runtime/host/window_class.h
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace wlx::runtime::host {

ATOM ensure_window_class(HMODULE module, const wchar_t* class_name, WNDPROC wnd_proc);

}  // namespace wlx::runtime::host
```

`.cpp` body: the existing `ensure_window_class` body, parameterized on `class_name` and `wnd_proc` — taking them from the function arguments instead of file-scope constants.

In each adapter: replace `ensure_window_class()` (no args) call with `wlx::runtime::host::ensure_window_class(g_hModule, L"WlxListerineMdView", ViewWndProc)` (md) or `…(g_hModule, L"WlxListerineColorizerView", ColorViewWndProc)` (colorizer).

Commit: `refactor(lift): ensure_window_class into runtime/host/window_class`.

---

### Task 4.7: Lift `update_scrollbar` + `handle_scroll`

These reference per-plugin `ViewState`. Templatize on a `Scrollable` concept.

**Files:**
- Create: `src/runtime/host/scroll_handler.h`

```cpp
// runtime/host/scroll_handler.h
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <concepts>

namespace wlx::runtime::host {

template <typename V>
concept Scrollable = requires(V& v, float delta) {
    { v.hwnd }            -> std::convertible_to<HWND>;
    { v.scroll_y }        -> std::same_as<float&>;
    { v.content_height }  -> std::same_as<float&>;
    { v.viewport_height } -> std::same_as<float&>;
    // (audit ViewState/ColorViewState for the exact fields they share)
};

template <Scrollable V>
void update_scrollbar(V& v) {
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_ALL;
    si.nMin   = 0;
    si.nMax   = static_cast<int>(v.content_height);
    si.nPage  = static_cast<int>(v.viewport_height);
    si.nPos   = static_cast<int>(v.scroll_y);
    SetScrollInfo(v.hwnd, SB_VERT, &si, TRUE);
}

template <Scrollable V>
void handle_scroll(V& v, float delta) {
    v.scroll_y += delta;
    if (v.scroll_y < 0) v.scroll_y = 0;
    const float max_scroll = v.content_height - v.viewport_height;
    if (v.scroll_y > max_scroll) v.scroll_y = max_scroll;
    update_scrollbar(v);
    InvalidateRect(v.hwnd, nullptr, FALSE);
}

}  // namespace wlx::runtime::host
```

The exact concept fields must match what BOTH `ViewState` and `ColorViewState` expose. **Audit step:** before writing, read both structs in `plugin_md/window/view_state.h` and `plugin_colorizer/window/view_state.h` (created in Phase 2 split of host adapters; if not split yet, read directly from the host adapters) and confirm field names. If field names differ between the two ViewStates, rename one to match before lifting.

Each adapter: remove the static `update_scrollbar`/`handle_scroll` defs; replace call sites with `wlx::runtime::host::update_scrollbar(*vs)` / `wlx::runtime::host::handle_scroll(*vs, delta)`.

Commit: `refactor(lift): scrollbar handling into runtime/host/scroll_handler`.

---

### Task 4.8: Lift `clear_selection` + `scroll_to_match`

These also reference per-plugin ViewState. Templatize on appropriate concepts.

**Files:**
- Create: `src/runtime/host/selection_helpers.h`

```cpp
// runtime/host/selection_helpers.h
#pragma once

#include <concepts>

#include "runtime/layout/text_position.h"
#include "runtime/search/search_match.h"
#include "runtime/host/scroll_handler.h"  // for Scrollable

namespace wlx::runtime::host {

template <typename V>
concept Selectable = Scrollable<V> && requires(V& v) {
    { v.selection_start } -> std::same_as<wlx::runtime::layout::TextPosition&>;
    { v.selection_end }   -> std::same_as<wlx::runtime::layout::TextPosition&>;
    // (audit fields)
};

template <Selectable V>
void clear_selection(V& v) {
    v.selection_start = {};
    v.selection_end = {};
    InvalidateRect(v.hwnd, nullptr, FALSE);
}

template <typename V>
concept Searchable = Scrollable<V> && requires(V& v) {
    { v.layout } -> std::convertible_to<const wlx::runtime::layout::LayoutDocument&>;
    // (audit)
};

template <Searchable V>
void scroll_to_match(V& v, const wlx::runtime::search::SearchMatch& m) {
    // body copied from one of the host adapters, with v.<field> for ViewState members
}

}  // namespace wlx::runtime::host
```

Same audit-fields-first principle. Adapt concepts based on actual ViewState shapes. Replace call sites in both adapters.

Commit: `refactor(lift): clear_selection + scroll_to_match into runtime/host/selection_helpers`.

---

### Task 4.9: Audit `ensure_theme`; lift if equivalent

**Spec note:** `ensure_theme` may or may not be safe to lift. Both plugins load a `ThemeService` from a TOML; the call shapes might diverge in subtle ways (default ini path computation, fallback rules).

- [ ] **Step 1: Read both `ensure_theme` definitions** in `host_adapter.cpp` and `colorizer_host_adapter.cpp`.

Compare line by line. If the only difference is the TOML filename (`wlx-listerine-md.toml` vs `wlx-listerine-colorizer.toml`), they're liftable with the filename as a parameter. If they differ in fallback logic or default-path computation, **leave them in place** and document the divergence in a comment in the spec's "Open questions" section as resolved-not-lifted.

- [ ] **Step 2 (if liftable): create `src/runtime/host/theme_loader.{h,cpp}`** with the pattern:

```cpp
namespace wlx::runtime::host {
void ensure_theme(wlx::runtime::theme::ThemeService& service,
                  bool& loaded_flag,
                  const wchar_t* config_filename);
}
```

Both plugins call with their own filename.

- [ ] **Step 3 (if not liftable): commit a comment in both files** explaining why each `ensure_theme` is per-plugin, then move on.

Commit (whichever path): `refactor(lift): ensure_theme audit (lifted | left-per-plugin)`.

---

### Phase 4 acceptance

After Phase 4:

- The duplicate-helper table in the spec is fully resolved — every entry is either lifted to `runtime/host/` or documented-not-lifted with a code comment.
- `host_adapter.cpp` and `colorizer_host_adapter.cpp` are each ~600–800 lines lighter than before Phase 4.
- `BUILD_TEST_OK` + visual regression for md plugin + manual TC smoke test for colorizer plugin all pass.

---

## Phase 5 — Test scenario splits

Only one split is mandated by this spec: `test_markdown_parser.cpp` (35 cases) → per-feature files under `tests/runtime/parser/markdown/`. Other test files stay as-is unless reading them reveals similar fragmentation (decide during the task).

### Task 5.1: Split `test_markdown_parser.cpp` by feature

**Files:**
- Read: `tests/runtime/parser/test_markdown_parser.cpp`
- Create: `tests/runtime/parser/markdown/test_<feature>.cpp` per feature group
- Delete: `tests/runtime/parser/test_markdown_parser.cpp` (replaced)
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Read all 35 test cases**

```bash
grep -E '^TEST_CASE\(' tests/runtime/parser/test_markdown_parser.cpp
```

List the case names. Cluster them by feature: headings, lists, links, code blocks, inline emphasis, blockquotes, horizontal rules, tables, html blocks, etc. Pick the cluster boundaries based on the actual cases.

- [ ] **Step 2: Create one file per cluster**

For each feature cluster (example: headings):

```cpp
// tests/runtime/parser/markdown/test_headings.cpp
#include <doctest/doctest.h>

#include "runtime/parser/markdown_parser.h"
#include "runtime/parser/document.h"
// (other includes the cases need)

TEST_SUITE("runtime::parser::markdown::headings") {
    // Move the heading-related TEST_CASEs verbatim from
    // test_markdown_parser.cpp into this file.
}
```

Repeat for each cluster: `test_lists.cpp`, `test_links.cpp`, `test_code_blocks.cpp`, etc.

- [ ] **Step 3: Delete `tests/runtime/parser/test_markdown_parser.cpp`** once all 35 cases have new homes

```bash
git rm tests/runtime/parser/test_markdown_parser.cpp
```

- [ ] **Step 4: Update `tests/CMakeLists.txt`'s `add_executable(tests …)` list**

Replace `runtime/parser/test_markdown_parser.cpp` with the per-feature files:

```cmake
add_executable(tests
    test_main.cpp
    runtime/parser/test_document_model.cpp
    runtime/parser/markdown/test_headings.cpp
    runtime/parser/markdown/test_lists.cpp
    runtime/parser/markdown/test_links.cpp
    runtime/parser/markdown/test_code_blocks.cpp
    runtime/parser/markdown/test_inline_emphasis.cpp
    runtime/parser/markdown/test_blockquotes.cpp
    runtime/parser/markdown/test_horizontal_rules.cpp
    runtime/parser/markdown/test_tables.cpp
    runtime/parser/markdown/test_html_blocks.cpp
    # … rest of test sources unchanged …
)
```

(Adjust list to match the actual clusters you found.)

- [ ] **Step 5: `BUILD_TEST_OK`**

Test count must remain 35 (or unchanged); `tests` reports `0 failed` and the same total case count as before.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
refactor(tests): split test_markdown_parser by feature

35 cases → per-feature files under tests/runtime/parser/markdown/.
Test count unchanged; finding 'where's the test for X' is now
answered by filename.

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5.2: Audit other tests for excessive scenarios

- [ ] **Step 1: Count test cases per file**

```bash
for f in $(find tests -name 'test_*.cpp'); do
    n=$(grep -c '^TEST_CASE\|^SUBCASE' "$f")
    echo "$n $f"
done | sort -rn | head -20
```

- [ ] **Step 2: For any file with >15 cases that span unrelated features, split following the same pattern as Task 5.1.**

Likely candidates: `test_colorizer_grammars.cpp` (covers many languages — could split into `core_dll/grammar/grammars/test_<lang>.cpp`).

- [ ] **Step 3: If splits done, commit per file split with `refactor(tests): split <file> by …`. If no splits needed, no commit.**

---

## Phase 6 — Docs

### Task 6.1: Update `CLAUDE.md`

**Files:**
- Modify: `CLAUDE.md`

The "Architecture" diagram and module references all change. The `wlx-listerine-core` section's file references update too.

- [ ] **Step 1: Read current `CLAUDE.md` "Architecture" section**

- [ ] **Step 2: Replace the architecture diagram**

```markdown
## Architecture

```
src/runtime/                              wlx-core static lib (ns: wlx::runtime)
  parser/    layout/    render/    io/    cache/    theme/
  interaction/    search/    host/    diagnostics/    util/

src/core_dll/                             wlx-listerine-core.dll (ns: wlx::core)
  abi/    colorizer/    grammar/    highlighting/    theme/    registry/

src/plugin_md/                            wlx-listerine-md.wlx64 (ns: wlx::plugin_md)
  exports/    window/    document/    resource/

src/plugin_colorizer/                     wlx-listerine-colorizer.wlx64 (ns: wlx::plugin_colorizer)
  exports/    window/    language/    layout/    document/    resource/

src/tools/screenshot/                     screenshot_tool.exe (ns: wlx::tools::screenshot)

include/                                  public + 3rd-party headers only
  listerplugin.h                          TC SDK
  wlx_core/abi.h                          colorizer DLL public C ABI
  wlx_core/text_modifier.h                shared TextModifier enum
```

Each top-level src/ folder owns its own CMakeLists.txt; root CMakeLists.txt drives via add_subdirectory.
```

- [ ] **Step 3: Update the "Data flow" paragraph** to use the new file paths.

- [ ] **Step 4: Update the "wlx-listerine-core" section** — its file references (`grammar_registry.cpp` etc.) become `core_dll/grammar/grammar_registry.cpp`.

- [ ] **Step 5: Update "Per-window state"** path to `plugin_md/window/view_state.h` (and its colorizer counterpart).

- [ ] **Step 6: Commit**

```bash
git add CLAUDE.md
git commit -m "docs(CLAUDE.md): reflect new source tree layout

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 6.2: Create `.claude/rules/cpp-plugin-structure.md`

**Files:**
- Create: `.claude/rules/cpp-plugin-structure.md`

Today `CLAUDE.md` references this file but it doesn't exist. Create it as the canonical rule reference for future C++ work in this repo.

- [ ] **Step 1: Create the directory**

```bash
mkdir -p .claude/rules
```

- [ ] **Step 2: Write `.claude/rules/cpp-plugin-structure.md`**

```markdown
# C++ Plugin Structure Rules

Auto-loaded for C++/CMake work in wlx-listerine. Canonicalizes the layout decided in
docs/superpowers/specs/2026-05-02-restructure-design.md.

## Folder = namespace = binary boundary

- `src/runtime/` → static lib `wlx-core`, namespace `wlx::runtime::*`
- `src/core_dll/` → shared DLL `wlx-listerine-core`, namespace `wlx::core::*`
- `src/plugin_md/` → `wlx-listerine-md.wlx64`, namespace `wlx::plugin_md::*`
- `src/plugin_colorizer/` → `wlx-listerine-colorizer.wlx64`, namespace `wlx::plugin_colorizer::*`
- `src/tools/<tool>/` → exe targets, namespace `wlx::tools::<tool>`

Each top-level `src/<area>/` owns its own `CMakeLists.txt`. Subfolders form deeper namespaces:
`src/runtime/layout/` → `wlx::runtime::layout::*`.

## One type per file

Each public class, struct, or top-level enum gets its own header named after the type
(PascalCase → snake_case). Filename = type name.

Exceptions:
1. Types nested inside another type (`enum Kind` inside `LinkTarget`) stay with the enclosing type.
2. `std::hash` specializations live in the same file as the key type they hash.
3. Types defined inside a `.cpp` (anonymous namespace) are implementation details — strict-A
   doesn't apply. The public surface is what gets included by name.
4. Free-function modules (`text_selection`, `search_ops`, `string_util`) are not types and
   stay grouped by module — strict-A applies to types.

## Headers vs. sources

- Public headers stay co-located with their `.cpp` in `src/<module>/`.
- `include/` is reserved for cross-binary or third-party headers (`include/wlx_core/abi.h`,
  `include/wlx_core/text_modifier.h`, `include/listerplugin.h`).
- Include paths are full module paths from `src/`: `#include "runtime/layout/layout_engine.h"`.
- Each binary's `CMakeLists.txt` adds `target_include_directories(<target> PRIVATE
  ${CMAKE_SOURCE_DIR}/src)`.
- No `using namespace` in headers. `using namespace wlx::runtime::<area>` allowed in `.cpp`.

## File-local statics

Replace `static void foo(…)` with anonymous-namespace members inside the file's named namespace:

```cpp
namespace wlx::runtime::layout {
namespace { void helper() {} }
// …
}
```

## Tests

`tests/` mirrors `src/` exactly. Default: one test file per class (`test_<class>.cpp`).
When a class has many distinct scenarios, push the class into a subfolder and use scenario
filenames without redundant prefixes:

```
tests/runtime/parser/markdown/test_headings.cpp
tests/runtime/parser/markdown/test_lists.cpp
```

(Subfolder name avoids redundancy with parent: `parser/markdown/` not `parser/markdown_parser/`.)

POD-only files get no test file.

## CMakeLists organization

- Root: `cmake_minimum_required`, `project()`, version, `find_package`, `option`, `configure_file`,
  `include(cmake/grammars.cmake)`, then six `add_subdirectory` calls. **No targets in root.**
- `cmake/grammars.cmake`: tree-sitter grammar fetching/build machinery + 26 grammar declarations.
- Each `src/<area>/CMakeLists.txt` declares its own targets.
- `tests/CMakeLists.txt` declares both test executables.

## DLL detach must leak COM

(Cross-references CLAUDE.md memory `feedback_dll_detach.md`.)
Never `Release()` COM objects in `DLL_PROCESS_DETACH`. The lifted singletons in
`runtime/host/factories.cpp` follow the same rule — leak via std::move to heap if needed.
```

- [ ] **Step 3: Commit**

```bash
git add .claude/rules/cpp-plugin-structure.md
git commit -m "docs(rules): create cpp-plugin-structure ruleset

Canonicalizes the post-restructure rules. Resolves the dangling
reference in CLAUDE.md to a previously-missing file.

Refs spec: docs/superpowers/specs/2026-05-02-restructure-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Final acceptance

After all phases complete:

- [ ] `find src -maxdepth 2 -type d | sort` shows only the planned tree.
- [ ] `grep -rln '^namespace wlx::' src | wc -l` is non-zero (proves namespaces are in place).
- [ ] `grep -rln 'static [a-zA-Z_]\+ [a-zA-Z_]\+(' src/runtime src/core_dll src/plugin_md src/plugin_colorizer | wc -l` is near zero (file-local statics replaced with anonymous namespaces; some may remain in `extern "C"` blocks where namespace doesn't apply).
- [ ] Root `CMakeLists.txt` has no `add_library`, `add_executable`, or `target_*` calls.
- [ ] `cmake --build --preset conan-release` succeeds.
- [ ] `./build/Release/tests.exe` reports 0 failed.
- [ ] `./build/Release/colorizer-tests.exe` reports 0 failed.
- [ ] `./scripts/visual-test.sh` reports all 27 cases ≥ 95% similarity.
- [ ] Manual TC smoke test: open `.md` and `.cpp` files; both render correctly with dark mode, scroll, hit-testing, copy, search.
- [ ] `CLAUDE.md` Architecture section reflects new layout.
- [ ] `.claude/rules/cpp-plugin-structure.md` exists.

If all check, the restructure is complete.

---

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| #include sweep misses a reference, breaking the build | After every move, `BUILD_TEST_OK`. Failures are caught at the commit boundary, not at end. |
| Phase 4 lift introduces a behavioral regression | Per-helper commits + visual regression after each + manual TC smoke. The spec accepts this is the highest-risk phase. |
| Subagent tries to coordinate cross-cutting renames | Plan execution stays in main thread. Subagents OK only for parallel-mechanical work *inside* a single task (e.g., applying namespace wrappers to 8 sibling files in parallel) where the destination is fixed. |
| Visual-regression failures mask real bugs as test flake | If any case drops below 95%, investigate; do NOT update goldens unless the change was intentional and matches Chrome rendering. |
| Phase 1 leaves `src/colorizer/` as an empty zombie folder | Acceptance check explicitly requires `rmdir src/colorizer` after Task 1.15 + 1.14. |
| Phase 4 templating changes header sizes/inlines, slowing build | Acceptable tradeoff for code dedup; revisit only if compilation time becomes painful. |

---

## Notes on subagent use

This plan executes inline by default (recommended for cross-cutting refactors). Tactical subagent use:

- **Phase 1:** A subagent CAN parallelize the per-folder moves *within* a phase if you can hand each subagent a self-contained task description (move folder X, return). Don't have multiple subagents touch overlapping files (e.g., the root CMakeLists.txt) simultaneously.
- **Phase 2:** Header splits CAN be parallelized — one subagent per multi-class header, since each header's split is independent. Dispatch all 10 split tasks in parallel after Phase 1 completes.
- **Phase 3:** Namespace wrappers within one area (e.g., all of `runtime/`) MUST be one subagent's task — cross-area #include consumers need coordinated updates.
- **Phase 4:** **Inline only.** Each lift requires reading both source copies, comparing, deciding equivalence, and validating with visual regression. No subagent has the context.
- **Phase 5:** The markdown test split CAN be a subagent task with the cluster list as input.
- **Phase 6:** Inline.

Subagents launched within a phase must report:
- Files they created/modified
- Build/test status when they finished
- Any unexpected divergence they encountered

Verify their reported diffs against `git status` before committing.
