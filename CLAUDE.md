# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**wlx-listerine** — Total Commander WLX lister plugins. Minimalistic, native Direct2D/DirectWrite rendering. C++20, 64-bit only, Win11 target.

### Plugins

- **wlx-listerine-md** (`wlx-listerine-md.wlx64`) — Markdown renderer
- **wlx-listerine-colorizer** (`wlx-listerine-colorizer.wlx64`) — Syntax colorizer (tree-sitter based)

## Build

Requires CMake 3.20+, Conan 2.x, MSVC.

```bash
conan install . --output-folder=build --build=missing -s build_type=Release -s compiler.cppstd=20
cmake --preset conan-default
cmake --build --preset conan-release
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

No lint tool is configured.

## Architecture

### Source layout

```
src/
  runtime/            wlx-core static lib, shared by both plugins + tools
    io/               FileService — file reading, BOM/encoding detection, normalization
    parser/           MarkdownParser — md4c SAX callbacks -> Document (block/inline AST)
    layout/           LayoutEngine — Document -> LayoutDocument via IDWriteTextLayout;
                      lazy estimate pass + md_materialize (viewport materialization);
                      line_index (logical-line Y index for gutter + go-to-line)
    render/           RenderEngine — Direct2D painting, color emoji, gutter, search highlights
    interaction/      InteractionEngine (hit-test, link routing), text_selection, url_scanner
    search/           search_index / search_ops + SearchHud (match counter, prev/next buttons)
    host/             Win32 glue: window_class, factories, scroll_handler, clipboard,
                      context_menu, grammar_menu, goto_line, web_search, dark_mode,
                      async_loader, host_integration (WH_GETMESSAGE hook + parent subclass)
    cache/            CacheService — parse cache + layout cache with viewport bucketing
    theme/            ThemeService — TOML config, light/dark ColorPalette, spacing, fonts
  core_dll/           wlx-listerine-core.dll — C ABI, grammar cache, highlighting, Helix themes
  plugin_md/          wlx-listerine-md.wlx64 — WLX exports + WndProc (window/host_adapter.cpp)
  plugin_colorizer/   wlx-listerine-colorizer.wlx64 — WLX exports + WndProc;
                      language/path_to_language.h (ext/filename -> grammar id)
  tools/screenshot/   screenshot_tool.exe — offscreen render for visual tests + benchmarks
tests/                mirrors src/ (tests.exe = runtime; colorizer-tests.exe = core_dll +
                      plugin_colorizer + tools)
```

**Data flow (md):** File -> FileService (read+decode) -> md4c SAX -> Document AST -> LayoutEngine (IDWriteTextLayout measuring) -> LayoutDocument -> RenderEngine (ID2D1HwndRenderTarget) -> screen.

**Document model** (`src/runtime/parser/`, header-only): BlockNode tree with InlineNode leaves. InlineNodes carry flattened style flags (bold/italic/code accumulated from ancestors). LinkTarget classifies links as InternalAnchor, RelativeDoc, or ExternalUrl.

**Per-window state** (`ViewState` in each plugin's host adapter) stored in `g_views` map. Holds Document, LayoutDocument, RenderEngine, InteractionEngine, scroll/selection/search state, and `LoadState` (Loading/Ready).

### Async loading (both plugins)

`ListLoadW` returns a window immediately in `LoadState::Loading`; the parse runs on a detached worker thread (`runtime/host/async_loader.h`) and PostMessages the result back to the UI thread. Safety: a shared `ViewLiveToken` (generation counter + closed flag) defeats HWND-recycle/supersede races (`should_adopt_result`), and `pin_plugin_module_once` pins the .wlx64 so a late worker never returns into unmapped code. Never capture the ViewState in a worker.

### Lazy markdown layout

Markdown layout is always lazy: an estimate pass assigns approximate block heights, then `materialize_viewport` (`runtime/layout/md_materialize.cpp`) measures real blocks on paint and batch-shifts later blocks/anchors. The `line_tops` index is rebuilt after each materializing paint. Paint, hit-testing, and scroll all work against the partially-materialized document. When the host passes its retained `MdMaterializeCtx::recipes`, blocks that scroll more than `kEvictScreens` (±2) screens out of view drop their `IDWriteTextLayout` + per-paint decorations (keeping measured rects and run text, so geometry/line_tops/search stay exact); a recipe rebuilds them byte-identically on re-entry (delta 0, no reflow, no index rebuild). Recipe-less blocks (lists/quotes/tables, built eagerly) are never evicted. `CacheService` adds a per-cache byte budget (`kMaxBytesPerCache`, 64 MB) that LRU-evicts whole Document/LayoutDocument entries; the per-entry size comes from `runtime/cache/memory_estimate.h`, a calibrated proxy (per-alloc + per-block overhead tuned against the md bench's working-set rows).

### Viewport-scoped highlighting (colorizer)

The colorizer parses a file once into a cached tree-sitter tree, then highlights only the visible byte range per scroll/resize (ABI v5 cached-tree parse + range highlight). After the first viewport paint a detached background *sweep* (`plugin_colorizer/colorize/span_table.h` + `sweep_chunk.h`) walks the whole file in adaptive ~25 ms chunks, accumulating every color into a compact `SpanTable`, then **frees the tree** (the single biggest retained object); post-settle scrolling re-colors by slicing the table instead of querying the tree. The no-wrap layout is an *implicit grid* (`plugin_colorizer/layout/grid_geometry.h` + `grid_window.h`): `LayoutDocument.blocks` holds only a materialized viewport±overscan window, line geometry is arithmetic, and `slide_grid_window` builds entering lines / drops leaving ones (this is the colorizer's layout eviction). In grid mode `LayoutDocument.is_grid()` is true and all public block indices (TextPosition/SearchMatch/HitResult) are SOURCE LINE indices mapped through `first_block_line`; non-visible lines (search/select-all/goto) go through the raw-byte paths, never `doc.blocks`. The tree is freed and the sweep self-cancels on a generation bump; re-colorizes after `WM_DPICHANGED` relayout and dark-mode flips.

### Search, selection, go-to-line

- **Search:** TC's Lister find dialog drives `ListSearchTextW` -> `search_step` over the layout; the renderer highlights all matches plus the current one, and `SearchHud` paints a `current/total` counter with prev/next buttons. Esc clears matches.
- **Selection:** drag selection, double-click word select (`_` and `-` count as word chars), Ctrl+A/Ctrl+C; `lc_copy`/`lc_selectall` return OK. Shared via `Selectable` concept helpers in `runtime/host/view_actions.h`.
- **Go to line:** Ctrl+G opens an inline prompt (`runtime/host/goto_line.h` state machine). The md plugin can paint a line-number gutter (`[general] line_numbers`, default true).

### Host integration (TC accelerator workarounds)

`HostIntegration` (`runtime/host/host_integration.h`) installs a thread-local WH_GETMESSAGE hook plus a parent-window subclass: F2 triggers file reload (TC eats F2 before TranslateAccelerator; the reload menu ID is discovered from TC's accel resources), and top-row digit keys are routed to the go-to-line prompt while it is open (TC binds them to view-mode switches).

### Context menu

Right-click (`runtime/host/context_menu.h`): Copy, Select All, Search with Google, Open link / Copy link address (on a link hit), Copy code block (md, on a code fence), Edit config. The colorizer adds a language submenu (`grammar_menu`) that sets a session-only `force_grammar_id` override.

### wlx-listerine-core (shared DLL)

Syntax highlighting engine used by both the colorizer and md plugins (for code blocks). Built as a real Windows DLL (`wlx-listerine-core.dll`) shared by both plugin `.wlx64`s. The plugins talk to it through a C ABI in `include/wlx_core/abi.h` (extern "C" + an inline C++ shim for RAII span ownership), currently **ABI v5**: whole-file colorize with a byte range, cached-tree parse + range highlight (powers viewport-scoped highlighting), and `wlx_core_list_languages` (powers the grammar context menu). Components: `GrammarRegistry` (loads tree-sitter grammars), `QueryHighlighter` (executes tree-sitter queries, resolves colors and modifiers via theme, accepts an optional byte range), `HelixTheme` (loads Helix-compatible TOML themes with hierarchical scope resolution). Input: source code + grammar; output: colored spans (with optional bold/italic/underline/strikethrough bits) for rendering.

**Text modifiers:** `ResolvedStyle`, `ColorSpan`, and `ColorRange` carry a `uint8_t modifiers` byte (`MOD_BOLD`/`MOD_ITALIC`/`MOD_UNDERLINE`/`MOD_STRIKETHROUGH` from `include/wlx_core/text_modifier.h`). `parse_style` reads both `modifiers = ["italic", ...]` arrays and the `underline = { ... }` table form; terminal-only Helix modifiers (`reversed`/`dim`/`blink`/`hidden`) are silently dropped. `RenderEngine::paint_text_runs` applies `IDWriteTextLayout::SetFontWeight`/`SetFontStyle`/`SetUnderline`/`SetStrikethrough` per range alongside `SetDrawingEffect`. Default `make_default` themes set italic on `comment` and bold on `keyword.directive`. The markdown plugin has its own `ColorSpan`→`ColorRange` converter in `runtime/layout/code_fence_layout.cpp` for fenced code blocks; the standalone colorizer uses `plugin_colorizer/layout/colorizer_layout.cpp` — both must propagate `span.modifiers`.

**Theme system:** Helix editor-compatible TOML themes in `config/themes/`. Theme files use the same format as Helix (flat scope-to-style entries, `[palette]` section, `inherits` key). Selected in `wlx-listerine-core.toml`: `[theme] dark` (default "default") and `[theme] light` (optional override). If only `dark = "foo"` is set, the system auto-detects `foo_light.toml` for light mode.

Some languages support multiple grammar variants selected at runtime via TOML config (e.g. `[colorizer].cpp_grammar = "standard" | "unreal"` swaps standard tree-sitter-cpp for taku25's Unreal-aware fork via a build-time alias TU; the routing primitive is `apply_cpp_variant(...)` in `src/plugin_colorizer/language/routing.h`). Grammars are fetched and built by CMake (`cmake/grammars.cmake`, FetchContent) into `grammars/<lang>/tree-sitter-<lang>.dll` — 27 grammar DLLs covering 26 languages.

**Process-wide grammar cache:** `CoreRegistry` is a singleton inside the core DLL, lazy-initialized via `std::call_once` on first ABI call. It owns one `GrammarCache` (LRU + TTL eviction) shared across both plugins, plus dark/light themes. A single `std::mutex` is held for the duration of each `colorize()` call — since trees are torn down inside the same locked region, eviction can never race with active language pointers. Cache config lives in `wlx-listerine-core.toml` next to the DLL: `[grammar_cache] cap` (soft cap, default 8) and `ttl_minutes` (eviction freshness gate, default 5). The `wlx-listerine-core.dll`, both `.wlx64`s, themes, and grammars all ship together as one bundle — versions are pinned lockstep.

## WLX exports (Unicode-only)

`ListLoadW`, `ListLoadNextW`, `ListCloseWindow`, `ListGetDetectString`, `ListSendCommand`, `ListSetDefaultParams`, `ListSearchTextW`. Defined in each plugin's `exports/plugin.def`. API constants in `include/listerplugin.h`. `ListSendCommand` handles `lc_copy`, `lc_selectall`, `lc_newparams` (dark mode + wrap text), and `lc_setpercent`.

## Key conventions

- Custom window classes `WlxListerineMdView` (md) and `WlxListerineColorView` (colorizer) with DirectWrite/Direct2D rendering
- `ComPtr<T>` (from `<wrl/client.h>`) for all COM pointers
- D2D/DWrite factories are global singletons, created on first ListLoadW; in DLL_PROCESS_DETACH COM objects are deliberately leaked (never `Release()` there — moved to the heap instead)
- `NOMINMAX` must be defined before any Windows header inclusion

## Tests

~470 doctest cases across two suites; the `tests/` tree mirrors `src/`:

- `tests.exe` (~270 cases) — runtime: document model, markdown-to-AST, theme/config TOML, file encoding, cache hit/miss/bucketing, layout (incl. lazy layout + line index) with real IDWriteFactory, text selection, URL scanner, search engine/ops/HUD, async loader, goto-line, context/grammar menus, web search.
- `colorizer-tests.exe` (~200 cases) — core DLL + colorizer: grammar registry/cache, tokenization, query highlighting, Helix theme loading, C ABI, path-to-language routing, colorizer layout, end-to-end smoke.

## Visual Regression Tests

29 test cases in `test_data/cases/`. Compares `screenshot_tool` output against golden Chrome PNGs (`*_chrome.png`). Threshold: >= 95% pixel similarity = PASS.

```bash
# Run full visual regression suite (generate + compare)
./scripts/visual-test.sh

# Update ALL golden Chrome PNGs (after intentional visual changes)
bun run update-goldens

# Update a single golden
bun run update-goldens -- 01_headings_atx
```

**Pre-commit hook:** Auto-runs when `.cpp`, `.h`, `.toml`, or test `.md` files are staged. Enable with:
```bash
git config core.hooksPath .githooks
```

## Performance Benchmarks

Machine-specific baselines live in the README "Performance" section (median of 5 runs, `scripts/bench.py`).

```bash
uv run scripts/bench.py            # run suite, print current vs README baseline
uv run scripts/bench.py --update   # re-measure and rewrite the README baseline
```

First run downloads pinned inputs into `test_data/bench/fetched/`. Requires a Release build of `screenshot_tool.exe`. Run on an idle machine; numbers are only comparable on the same hardware.

## Configuration

`config/wlx-listerine-md.toml` (schema v2). Sections: `[general]` (extensions, detect_string, line_numbers), `[fonts]` (body, code, emoji + sizes), `[spacing]` (paragraph, heading, list, quote, code, line height), `[colors.light]` and `[colors.dark]` (12 colors each: background, text, heading, muted, link, link_hover, code_bg, quote_border, rule, selection, search_highlight, search_highlight_current), `[code]` (default_language). `config/wlx-listerine-colorizer.toml` adds `[display]` (line_numbers, word_wrap, tab_width, whitespace/indent-guide options) and `[colorizer]` (cpp_grammar). Shared `config/wlx-listerine-core.toml`: `[grammar_cache]` + `[theme]`. Full reference: `docs/CONFIGURATION.md`.
