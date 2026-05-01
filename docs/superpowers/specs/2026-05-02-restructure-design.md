# Source Tree Restructure — Design

**Date:** 2026-05-02
**Status:** Draft, pending user review

## Problem

The repository's source layout has grown organically and now hides structure that should be obvious from the directory tree:

1. **`src/` is flat.** 30+ source files dumped into `src/` root. Only `colorizer/` is a subfolder, and it mixes the colorizer-engine DLL files with the colorizer-plugin shell — two different binaries, one folder.
2. **No namespaces.** The codebase has effectively one global namespace. The only declared namespaces are `wlx_host_common_internal_::` (one private detail) and `namespace fs = std::filesystem` aliases.
3. **Multi-class headers.** Many headers cluster several types: `layout_engine.h` (1 class + 8 structs), `cache_service.h` (1 class + 4 structs), `document_model.h` (5 structs), `theme_service.h` (1 class + 4 structs), `search_engine.h` (1 class + 2 structs), `colorizer.h` (1 class + 2 structs), `helix_theme.h` (1 class + 1 struct), `wlx_host_common.h` (1 concept + 1 class template + 1 internal probe). Filenames don't tell you where a type lives.
4. **Catch-alls.** `wlx_host_common.h`, `string_util.h`, `wlx_trace.h` — generic names, mixed responsibilities.
5. **Mega `.cpp` files.** `colorizer_host_adapter.cpp` (1212 lines), `host_adapter.cpp` (1085), `layout_engine.cpp` (721), `render_engine.cpp` (653). All contain multiple sections separated by `// ----------` dividers — the seams are visible in the source itself.
6. **Duplicate plugin code.** `colorizer_host_adapter.cpp` and `host_adapter.cpp` carry near-identical helpers: `apply_dark_mode`, `ensure_factories`, `get_module_dir`, `ensure_theme`, `update_scrollbar`, `handle_scroll`, `hit_test_position`, `block_text_length`, `copy_to_clipboard`, `clear_selection`, `scroll_to_match`, `ensure_window_class`. Two copies, one logic.
7. **Flat tests.** 22 `tests/test_*.cpp` files in one directory. `test_markdown_parser.cpp` alone holds 30+ scenarios.
8. **Monolithic CMakeLists.** 561 lines in the root `CMakeLists.txt` covering project setup, five targets, output staging, and 26 tree-sitter grammar declarations.
9. **Doc drift.** `CLAUDE.md` references `.claude/rules/cpp-plugin-structure.md` — the file does not exist.

## Goals

- Folder layout reveals architecture: top-level folders correspond to deliverables (binaries) and shared subsystems.
- A type's location is predictable from its name: `LayoutBlock` → `runtime/layout/layout_block.h`.
- Filenames are unambiguous: one type per file, file name = type name in snake_case.
- Namespaces match folder paths so a fully-qualified name (`wlx::runtime::layout::LayoutEngine`) tells you exactly where the type's source lives.
- Tests mirror the source tree; finding a test is the same lookup as finding the code under test.
- Build rules co-located with the code they describe — each top-level `src/<area>/` owns a `CMakeLists.txt`.
- Eliminate the two-copies-of-host-helpers duplication while reorganizing.

## Non-goals

- Behavioral changes. Output binaries, install layout, configuration semantics, theme behavior, plugin features — all unchanged.
- Cross-platform support beyond Windows (still 64-bit, MSVC, Win11).
- Performance work (no profiling-driven changes inside this restructure).
- Replacing third-party headers (`listerplugin.h`, doctest, md4c, tree-sitter, tomlplusplus stay where they are).
- Adding tests beyond the test-tree reorganization. Coverage stays the same.

---

## Architecture

### Top-level layout

```
wlx-mini-markdown/
├── include/                              public + 3rd-party headers only
│   ├── listerplugin.h                    (TC SDK, untouched)
│   └── wlx_core/
│       ├── abi.h                         colorizer DLL public C ABI
│       └── text_modifier.h               TextModifier enum — engine output, consumed by runtime+plugins
│
├── src/
│   ├── core_dll/                         → wlx-listerine-core.dll        ns: wlx::core
│   ├── plugin_md/                        → wlx-listerine-md.wlx64        ns: wlx::plugin_md
│   ├── plugin_colorizer/                 → wlx-listerine-colorizer.wlx64 ns: wlx::plugin_colorizer
│   ├── runtime/                          → wlx-core static lib           ns: wlx::runtime
│   └── tools/
│       └── screenshot/                   → screenshot_tool.exe           ns: wlx::tools::screenshot
│
├── tests/                                mirrors src/ exactly (same folders, same namespaces)
│   ├── core_dll/   plugin_md/   plugin_colorizer/   runtime/   tools/
│
├── cmake/
│   └── grammars.cmake                    add_grammar/fetch_grammar machinery + 26 grammar declarations
│
├── CMakeLists.txt                        ~80 lines: project, find_package, version, add_subdirectory
└── (each src/<area>/ owns its own CMakeLists.txt; tests/ has one too)
```

**Binary → folder mapping:**

| Folder | CMake target | Type |
|---|---|---|
| `src/runtime/` | `wlx-core` | static lib (consumed by both plugins) |
| `src/core_dll/` | `wlx-listerine-core` | shared DLL with public C ABI |
| `src/plugin_md/` | `wlx-listerine-md` | shared `.wlx64` |
| `src/plugin_colorizer/` | `wlx-listerine-colorizer` | shared `.wlx64` |
| `src/tools/screenshot/` | `screenshot_tool` | exe |
| `tests/` | `tests` + `colorizer-tests` | doctest exes |

### Module breakdown

**`src/runtime/`** — shared static lib `wlx-core`, namespace `wlx::runtime`:

```
runtime/parser/                          ns: wlx::runtime::parser
    markdown_parser.{h,cpp}              MarkdownParser class
    document.h                           Document
    block_node.h                         BlockNode
    inline_node.h                        InlineNode
    link_target.h                        LinkTarget (+ nested Kind enum)
    source_range.h                       SourceRange

runtime/layout/                          ns: wlx::runtime::layout
    layout_engine.{h,cpp}                LayoutEngine class
    layout_document.h                    LayoutDocument
    layout_block.h                       LayoutBlock
    text_run.h                           TextRun
    interactive_span.h                   InteractiveSpan
    anchor_entry.h                       AnchorEntry
    color_range.h                        ColorRange
    code_bg_rect.h                       CodeBgRect
    text_position.h                      TextPosition

runtime/render/                          ns: wlx::runtime::render
    render_engine.{h,cpp}                RenderEngine class

runtime/io/                              ns: wlx::runtime::io
    file_service.{h,cpp}                 FileService class
    file_identity.h                      FileIdentity
    file_content.h                       FileContent

runtime/cache/                           ns: wlx::runtime::cache
    cache_service.{h,cpp}                CacheService class
    parse_cache_key.h                    ParseCacheKey + std::hash specialization
    layout_cache_key.h                   LayoutCacheKey + std::hash specialization

runtime/theme/                           ns: wlx::runtime::theme
    theme_service.{h,cpp}                ThemeService class
    color_palette.h                      ColorPalette
    spacing_config.h                     SpacingConfig
    font_config.h                        FontConfig
    theme_config.h                       ThemeConfig

runtime/interaction/                     ns: wlx::runtime::interaction
    interaction_engine.{h,cpp}           InteractionEngine class
    text_selection.{h,cpp}               extract_selected_text + helpers (free functions; one .h declares them all — strict-A applies to types, not free-function modules)

runtime/search/                          ns: wlx::runtime::search
    search_engine.{h,cpp}                (split: search_engine has SearchIndex + free helpers; SearchIndex gets its own file)
    search_index.{h,cpp}                 SearchIndex class
    search_match.h                       SearchMatch
    search_query.h                       SearchQuery
    search_ops.h                         SearchStepResult + step functions (free functions module)
    search_counter_format.h              counter formatter (free function)
    search_hud.{h,cpp}                   SearchHud class
    search_hud_painter.{h,cpp}           SearchHudPainter class
    search_hud_state.h                   SearchHudState
    search_hud_hit_rects.h               SearchHudHitRects

runtime/host/                            ns: wlx::runtime::host
    host_view.h                          HostView concept (today wlx_host_common.h top half)
    host_integration.h                   HostIntegration<V> class template (today wlx_host_common.h bottom half)
    factories.{h,cpp}                    ensure_factories (D2D/DWrite singletons) — LIFTED from both host adapters
    dark_mode.{h,cpp}                    apply_dark_mode — LIFTED
    module_path.{h,cpp}                  get_module_dir — LIFTED
    scroll_handler.{h,cpp}               handle_scroll, update_scrollbar — LIFTED (parameterized by ViewState concept)
    hit_test.{h,cpp}                     hit_test_position, block_text_length — LIFTED (operates on LayoutDocument)
    clipboard.{h,cpp}                    copy_to_clipboard — LIFTED
    selection_helpers.{h,cpp}            clear_selection, scroll_to_match — LIFTED (parameterized by ViewState concept)
    window_class.{h,cpp}                 ensure_window_class — LIFTED (parameterized)

runtime/diagnostics/                     ns: wlx::runtime::diagnostics
    trace.h                              wlx_trace.h moves here

runtime/util/                            ns: wlx::runtime::util
    string_util.h                        wstring_to_utf8, utf8_to_wstring (free-function module; tightly-coupled string conversion helpers).
                                         parse_hex_color (today colocated here) moves to runtime/theme/ since it has only theme consumers.
```

**`src/core_dll/`** — shared DLL `wlx-listerine-core`, namespace `wlx::core`:

```
core_dll/abi/                            ns: wlx::core::abi
    wlx_core_abi.cpp                     extern "C" ABI implementations
    dllmain.cpp                          DllMain for the core DLL

core_dll/colorizer/                      ns: wlx::core::colorizer
    colorizer.{h,cpp}                    Colorizer class
    color_span.h                         ColorSpan
    colorize_result.h                    ColorizeResult

core_dll/grammar/                        ns: wlx::core::grammar
    grammar_registry.{h,cpp}             GrammarRegistry class
    grammar_cache.{h,cpp}                GrammarCache class

core_dll/highlighting/                   ns: wlx::core::highlighting
    query_highlighter.{h,cpp}            QueryHighlighter class.
                                         RawSpan (today a file-local struct inside query_highlighter.cpp) stays in the .cpp
                                         under strict-A Exception 3 — it's an implementation detail, not public surface.

core_dll/theme/                          ns: wlx::core::theme
    helix_theme.{h,cpp}                  HelixTheme class
    resolved_style.h                     ResolvedStyle

core_dll/registry/                       ns: wlx::core::registry
    core_registry.{h,cpp}                CoreRegistry class
    core_config.{h,cpp}                  CoreConfig class
```

**`src/plugin_md/`** — `wlx-listerine-md.wlx64`, namespace `wlx::plugin_md`:

```
plugin_md/exports/                       (no namespace — extern "C" only)
    wlx_exports.cpp                      ListLoadW, ListLoadNextW, ListCloseWindow, ListGetDetectString, ListSendCommand, ListSearchTextW, ListSetDefaultParams
    plugin.def

plugin_md/window/                        ns: wlx::plugin_md::window
    view_state.h                         ViewState
    view_window.{h,cpp}                  view-window class registration + ViewWndProc
    dllmain.cpp                          DllMain

plugin_md/document/                      ns: wlx::plugin_md::document
    document_loader.{h,cpp}              load_document, do_layout, relayout, reload_view orchestration

plugin_md/resource/
    resource.h                           VERSIONINFO ID definitions
    resource.rc.in                       VERSIONINFO template
```

**`src/plugin_colorizer/`** — `wlx-listerine-colorizer.wlx64`, namespace `wlx::plugin_colorizer`:

```
plugin_colorizer/exports/                (no namespace — extern "C" only)
    wlx_exports.cpp                      WLX entry points
    colorizer_plugin.def

plugin_colorizer/window/                 ns: wlx::plugin_colorizer::window
    view_state.h                         ColorViewState
    view_window.{h,cpp}                  ColorViewWndProc + window class registration
    dllmain.cpp

plugin_colorizer/language/               ns: wlx::plugin_colorizer::language
    extension_map.{h,cpp}                kExtLangMap, ext_to_language, filename_to_language
    routing.h                            apply_cpp_variant (today colorizer_routing.h)

plugin_colorizer/layout/                 ns: wlx::plugin_colorizer::layout
    display_config.h                     ColorizerDisplayConfig
    colorizer_layout.{h,cpp}             span→range conversion + the colorizer's specialized layout pass

plugin_colorizer/document/               ns: wlx::plugin_colorizer::document
    document_loader.{h,cpp}              load_document, do_layout, relayout, reload_view (colorizer-side orchestration)

plugin_colorizer/resource/
    colorizer_resource.h
    colorizer_resource.rc.in
```

**`src/tools/screenshot/`** — `screenshot_tool.exe`, namespace `wlx::tools::screenshot`:

```
tools/screenshot/main.cpp                entry point
tools/screenshot/options.h               Options struct
```

### Naming and namespace conventions

- **Filenames:** snake_case. One type per file. Filename = type name converted PascalCase → snake_case (`LayoutBlock` → `layout_block.h`).
- **Class/struct names:** PascalCase (unchanged from today).
- **Namespace names:** lowercase snake_case, identical to the folder name. Root is always `wlx::`.
- **Strict one-type-per-file** (Q3=A in brainstorm):
  - Each named class, struct, or top-level enum **declared in a header** gets its own file.
  - **Exception 1:** types nested inside another type (e.g., a `Kind` enum inside `LinkTarget`) stay in the enclosing type's file.
  - **Exception 2:** `std::hash` specializations and similar trait specializations live in the same file as the key type they hash. No separate `*_hash.h` files.
  - **Exception 3:** types defined inside a `.cpp` (anonymous namespace or today's `static` qualifier) are implementation details and stay where they are. Strict-A is about the *public surface*: what a consumer can `#include` and discover by filename.
  - **Free-function modules** (e.g., `text_selection`, `search_ops`, `search_counter_format`, `string_util`) are not types and stay grouped by module — strict-A applies to types, not function modules. Each module gets one `.h` (declarations) and optionally one `.cpp`.
  - Forward declarations stay in the consuming file's preamble.
- **Namespace declarations:**
  - Headers: explicit `namespace wlx::runtime::layout { ... }` form.
  - `.cpp` files: same form for definitions; `using namespace wlx::runtime::layout;` is allowed inside a `.cpp` body to keep call sites readable. No `using` declarations in headers.
  - File-local statics (today's `static` qualifiers in `.cpp` files) become anonymous-namespace members. The two are equivalent for linkage; anonymous namespace is the modern idiom and composes better with the namespace structure.
- **Header guards:** `#pragma once` (already universal in the codebase).

### Test layout

- **Mirrors `src/` exactly.** `src/runtime/layout/layout_engine.cpp` → `tests/runtime/layout/test_layout_engine.cpp`.
- **Default: one test file per class with behavior.** Class name, no folder-level redundancy: `tests/runtime/parser/test_markdown_parser.cpp`.
- **POD-only files get no test.** `layout_block.h` is data; nothing to test.
- **Scenario splits when a class accumulates many scenarios** (Q6=B with no-duplication refinement): the class becomes a subfolder, scenarios are file names without class prefix:
  ```
  tests/runtime/parser/markdown/test_headings.cpp
  tests/runtime/parser/markdown/test_lists.cpp
  tests/runtime/parser/markdown/test_links.cpp
  tests/runtime/parser/markdown/test_code_blocks.cpp
  tests/runtime/parser/markdown/test_inline_emphasis.cpp
  ...
  ```
  The subfolder name drops redundant prefixes/suffixes shared with its parent (`markdown/` not `markdown_parser/` because the parent is `parser/`).
- **Test files declare a doctest `TEST_SUITE`** matching their location: `TEST_SUITE("runtime::parser::markdown::headings")`.
- **Two test executables stay** (`tests` for markdown coverage, `colorizer-tests` for colorizer coverage). Per-binary test exes match the deliverable boundaries that `core_dll/`, `runtime/`, `plugin_md/`, `plugin_colorizer/` expose.

Initial scenario splits to do during this restructure (don't postpone — they're already-needed splits the current single files conceal):

- `tests/runtime/parser/markdown/` — split today's `test_markdown_parser.cpp` (30+ cases) by feature: `test_headings`, `test_lists`, `test_links`, `test_code_blocks`, `test_inline_emphasis`, `test_blockquotes`, `test_horizontal_rules`, `test_tables`, `test_html_blocks`. Final list determined when reading the file.
- `tests/core_dll/grammar/` — split today's `test_colorizer_grammars.cpp` if it covers many grammars; else leave as one file.
- All other tests: keep as a single file per class for now (move only, no split).

### CMakeLists organization

- **Root `CMakeLists.txt`** (~80 lines): `cmake_minimum_required`, `project()`, version parsing, `find_package` calls (md4c, tomlplusplus, tree-sitter, doctest), `option(WLX_TRACE_ENABLE …)`, `set(CMAKE_CXX_STANDARD 20)`, `configure_file` for resource templates, `include(cmake/grammars.cmake)`, then `add_subdirectory(src/runtime)`, `add_subdirectory(src/core_dll)`, `add_subdirectory(src/plugin_md)`, `add_subdirectory(src/plugin_colorizer)`, `add_subdirectory(src/tools/screenshot)`, `add_subdirectory(tests)`.
- **`cmake/grammars.cmake`**: the `add_grammar`/`fetch_grammar` functions and all 26 `FetchContent_Declare`/`fetch_grammar`/`add_grammar` calls (today ~270 of the 561 lines).
- **`src/runtime/CMakeLists.txt`**: declares `wlx-core` STATIC, lists sources, `target_include_directories(wlx-core PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)` (so consumers can `#include "wlx/runtime/..."` paths if we go that route, or relative `runtime/...` if not — implementation plan decides), links md4c/d2d1/dwrite/wlx-listerine-core publicly, tomlplusplus privately.
- **`src/core_dll/CMakeLists.txt`**: declares `wlx-listerine-core` SHARED with `WLX_CORE_BUILDING` define, `PREFIX ""`, `RUNTIME_OUTPUT_DIRECTORY_RELEASE` to `output/`. All POST_BUILD copy commands (DLL mirror, themes, grammars, core toml) live here.
- **`src/plugin_md/CMakeLists.txt`**: declares `wlx-listerine-md` SHARED, `SUFFIX ".wlx64"`, links `wlx-core` + `wlx-listerine-core` + Win32 libs, version define.
- **`src/plugin_colorizer/CMakeLists.txt`**: same pattern for the colorizer plugin. The output staging POST_BUILD (themes/grammars/sample TOMLs into `output/`) lives here since the colorizer plugin is built last today.
- **`src/tools/screenshot/CMakeLists.txt`**: declares `screenshot_tool` exe, links `wlx-core` + `windowscodecs` + `ole32`.
- **`tests/CMakeLists.txt`**: declares `tests` (markdown) and `colorizer-tests`, lists their sources by walking the mirror tree.

### Public include paths

Two options for how `runtime/` headers are included from `core_dll/`, `plugin_md/`, etc.:

- **A. Relative module paths.** `#include "runtime/layout/layout_engine.h"` after adding `src/` to the include path of consumers. Matches folder structure 1:1.
- **B. Project-prefixed paths.** `#include "wlx/runtime/layout/layout_engine.h"` after physically nesting `src/` content under a `wlx/` subfolder OR using a CMake-level mapping. More verbose; less ambiguous when grepping.

**Decision:** A. The folder is already meaningful (`runtime/`, `core_dll/`, `plugin_md/`); a `wlx/` prefix is redundant given that everything in `src/` is part of `wlx`. Each binary's `CMakeLists.txt` gets `target_include_directories(<target> PRIVATE ${CMAKE_SOURCE_DIR}/src)` so all module paths are rooted at `src/`.

### Duplicate lift (per Q2=A)

Helpers identified in both `host_adapter.cpp` and `colorizer_host_adapter.cpp`:

| Helper | New home | Parameterization |
|---|---|---|
| `apply_dark_mode` | `runtime/host/dark_mode.{h,cpp}` | none — pure HWND helper |
| `ensure_factories` | `runtime/host/factories.{h,cpp}` | none — owns global D2D/DWrite singletons (today each plugin has its own; consolidate into one set in `runtime/host/`) |
| `get_module_dir` | `runtime/host/module_path.{h,cpp}` | takes HMODULE |
| `update_scrollbar` | `runtime/host/scroll_handler.{h,cpp}` | template on a `ScrollableView` concept (members: `hwnd`, `scroll`, `content_height`, …) |
| `handle_scroll` | `runtime/host/scroll_handler.{h,cpp}` | same concept |
| `hit_test_position` | `runtime/host/hit_test.{h,cpp}` | takes `LayoutDocument&`; pure |
| `block_text_length` | `runtime/host/hit_test.{h,cpp}` | takes `LayoutBlock&`; pure |
| `copy_to_clipboard` | `runtime/host/clipboard.{h,cpp}` | takes HWND, wstring; pure |
| `clear_selection` | `runtime/host/selection_helpers.{h,cpp}` | template on a `SelectableView` concept |
| `scroll_to_match` | `runtime/host/selection_helpers.{h,cpp}` | template on a `SearchableView` concept |
| `ensure_window_class` | `runtime/host/window_class.{h,cpp}` | takes class name + WndProc |
| `ensure_theme` | likely **stays per-plugin** — both plugins load themes the same way conceptually but the call shapes differ slightly. Audit during implementation; lift only if the diff is purely cosmetic. |

The lift introduces one new file family in `runtime/host/` and removes ~600–800 lines of duplication across the two host adapters. Each host adapter shrinks correspondingly. **The lift is the largest semantic risk in this restructure** (everything else is mechanical); it gets its own commits in the implementation plan and runs both `tests` and visual-regression after each.

### Doc updates

- Update `CLAUDE.md` to reflect the new tree (the "Architecture" section's filename references all change).
- Create `.claude/rules/cpp-plugin-structure.md` (the dangling reference today). It canonicalizes the rules in this spec: namespace = folder, one type per file, POD vs class vs free-function-module distinction, `include/` policy, test mirror, scenario splits.

---

## Migration plan (high-level — implementation plan refines)

Done as staged commits on a single branch. Each commit independently compiles and passes tests. Build green is the gate between commits; if a commit fails CI, fix it before the next one.

**Phase 0 — Build extraction, no source moves.**
1. Extract grammar machinery to `cmake/grammars.cmake`. Root drops to ~290 lines. Verify build identical.
2. Add empty per-folder `CMakeLists.txt` files; root `add_subdirectory()` them. Sources still listed in root for now — folders compile zero files. Verify build identical.

**Phase 1 — File moves (no splits, no namespaces).**
3. `git mv` source files into their target folders (`src/file_service.{h,cpp}` → `src/runtime/io/`, etc.). Update root `CMakeLists.txt` paths and per-folder lists in tandem so the build stays green. **Preserves git history per file.**
4. `git mv` test files into mirrored test tree. Update `tests/CMakeLists.txt`.

**Phase 2 — Header splits (one type per file).**
5. Split multi-class headers into per-type files. One commit per source folder so each commit's diff stays scoped. Update `#include` paths across the codebase.
6. Promote internal types currently buried in `.cpp` files (e.g., `RawSpan` in `query_highlighter.cpp`) to their own headers if strict-A applies.

**Phase 3 — Namespaces.**
7. Add `namespace wlx::<area>::<sub> { ... }` wrappers to all moved files. One commit per top-level area (`core_dll`, then `runtime`, then plugins, then tools). Bulk-update call sites. Replace `static` file-local with anonymous namespaces inside `.cpp` files.

**Phase 4 — Duplicate lift.**
8. Lift the helpers listed in the table into `runtime/host/`. One helper per commit (or small grouping). After each commit: run `tests`, `colorizer-tests`, visual-regression suite. **Highest-risk phase — slow down here.**

**Phase 5 — Test scenario splits.**
9. Split `test_markdown_parser.cpp` into `tests/runtime/parser/markdown/test_*.cpp` per feature. Other large test files audited and split if they have multiple distinct scenarios.

**Phase 6 — Docs.**
10. Update `CLAUDE.md`. Create `.claude/rules/cpp-plugin-structure.md`.

### Risks

- **Build breakage during phase 1–2.** Mitigated by per-folder commits + build-green gate.
- **Regression during phase 4 (duplicate lift).** Mitigated by per-helper commits and full test+visual-regression run after each. The colorizer plugin's behavior is harder to assert mechanically (no visual-regression suite for it today) — manual smoke-test in TC after the lift commits is required.
- **Subagent coordination.** Subagents have no shared state; cross-cutting changes (rename a class → update every #include) cannot be safely parallelized. Subagents are usable inside a phase only for parallel mechanical work where the destination is fixed (e.g., "split these 8 headers into per-type files in folder X"), with central-thread coordination of the CMakeLists update and verification.
- **Scope creep.** Each phase has a fixed end. Resist the temptation to clean up adjacent code; that goes in follow-up PRs.

## Open questions

- Whether `ensure_theme` lifts into `runtime/host/` (see "Duplicate lift" table). Decided during phase 4 audit — lift only if the two implementations are behaviorally equivalent.
- Final scenario-split list for `test_markdown_parser.cpp`. Determined when the file is read in phase 5; cases cluster around markdown features (headings, lists, links, code blocks, etc.) but the precise file boundaries depend on case counts.

These are "decide when implementing" — they don't block this spec.
