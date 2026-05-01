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
```

No lint tool is configured.

## Architecture

```
HostAdapter (host_adapter.cpp)         WLX exports, WndProc, scroll, D2D/DWrite factories
  -> FileService (file_service)        File reading, BOM/encoding detection, normalization
  -> MarkdownParser (markdown_parser)  md4c SAX callbacks -> Document (block/inline AST)
  -> LayoutEngine (layout_engine)      Document -> LayoutDocument via IDWriteTextLayout
  -> RenderEngine (render_engine)      Direct2D painting with color emoji
  -> InteractionEngine (interaction)   Hit-testing, link routing (anchor/relative/external)
  -> ThemeService (theme_service)      TOML config, light/dark ColorPalette, spacing, fonts
  -> CacheService (cache_service)      Parse cache + layout cache with viewport bucketing
```

**Data flow:** File -> FileService (read+decode) -> md4c SAX -> Document AST -> LayoutEngine (IDWriteTextLayout measuring) -> LayoutDocument -> RenderEngine (ID2D1HwndRenderTarget) -> screen.

**Document model** (`document_model.h`, header-only): BlockNode tree with InlineNode leaves. InlineNodes carry flattened style flags (bold/italic/code accumulated from ancestors). LinkTarget classifies links as InternalAnchor, RelativeDoc, or ExternalUrl.

**Per-window state** (`ViewState` in host_adapter.cpp) stored in `g_views` map. Holds Document, LayoutDocument, RenderEngine, InteractionEngine, scroll state.

### wlx-listerine-core (shared DLL)

Syntax highlighting engine used by both the colorizer and md plugins (for code blocks). Built as a real Windows DLL (`wlx-listerine-core.dll`) shared by both plugin `.wlx64`s. The plugins talk to it through a C ABI in `include/wlx_core/abi.h` (extern "C" + an inline C++ shim for RAII span ownership). Components: `GrammarRegistry` (loads tree-sitter grammars), `QueryHighlighter` (executes tree-sitter queries, resolves colors and modifiers via theme), `HelixTheme` (loads Helix-compatible TOML themes with hierarchical scope resolution). Input: source code + grammar; output: colored spans (with optional bold/italic/underline/strikethrough bits) for rendering.

**Text modifiers:** `ResolvedStyle`, `ColorSpan`, and `ColorRange` carry a `uint8_t modifiers` byte (`MOD_BOLD`/`MOD_ITALIC`/`MOD_UNDERLINE`/`MOD_STRIKETHROUGH` from `src/text_modifiers.h`). `parse_style` reads both `modifiers = ["italic", ...]` arrays and the `underline = { ... }` table form; terminal-only Helix modifiers (`reversed`/`dim`/`blink`/`hidden`) are silently dropped. `RenderEngine::paint_text_runs` applies `IDWriteTextLayout::SetFontWeight`/`SetFontStyle`/`SetUnderline`/`SetStrikethrough` per range alongside `SetDrawingEffect`. Default `make_default` themes set italic on `comment` and bold on `keyword.directive`. The markdown plugin has its own `ColorSpan`→`ColorRange` converter at `layout_engine.cpp` for fenced code blocks; the standalone colorizer uses `colorizer_layout.cpp` — both must propagate `span.modifiers`.

**Theme system:** Helix editor-compatible TOML themes in `config/themes/`. Theme files use the same format as Helix (flat scope-to-style entries, `[palette]` section, `inherits` key). Config keys: `theme` (dark/default), `theme_light` (optional light override). If only `theme = "foo"` is set, the system auto-detects `foo_light.toml` for light mode.

Some languages support multiple grammar variants selected at runtime via TOML config (e.g. `[colorizer].cpp_grammar = "standard" | "unreal"` swaps standard tree-sitter-cpp for taku25's Unreal-aware fork via a build-time alias TU; the routing primitive is `apply_cpp_variant(...)` in `src/colorizer/colorizer_routing.h`).

**Process-wide grammar cache:** `CoreRegistry` is a singleton inside the core DLL, lazy-initialized via `std::call_once` on first ABI call. It owns one `GrammarCache` (LRU + TTL eviction) shared across both plugins, plus dark/light themes. A single `std::mutex` is held for the duration of each `colorize()` call — since trees are torn down inside the same locked region, eviction can never race with active language pointers. Cache config lives in `wlx-listerine-core.toml` next to the DLL: `[grammar_cache] cap` (soft cap, default 8) and `ttl_minutes` (eviction freshness gate, default 5). The `wlx-listerine-core.dll`, both `.wlx64`s, themes, and grammars all ship together as one bundle — versions are pinned lockstep.

## WLX exports (Unicode-only)

`ListLoadW`, `ListLoadNextW`, `ListCloseWindow`, `ListGetDetectString`, `ListSendCommand`, `ListSetDefaultParams`. Defined in `plugin.def`. API constants in `include/listerplugin.h`.

## Key conventions

- Custom window class `WlxListerineMdView` with DirectWrite/Direct2D rendering
- `ComPtr<T>` (from `<wrl/client.h>`) for all COM pointers
- D2D/DWrite factories are global singletons, created on first ListLoadW, released in DLL_PROCESS_DETACH
- `NOMINMAX` must be defined before any Windows header inclusion
- No text selection or search in current version (lc_copy/lc_selectall return ERROR)

## Tests

86 tests via doctest in `tests/` (markdown plugin). Coverage: document model construction, theme/config TOML parsing, file encoding detection, markdown-to-AST conversion (30+ cases), cache hit/miss/bucketing, layout engine block positioning with real IDWriteFactory. Colorizer tests in `colorizer-tests` covering grammar registry, tokenization, scope mapping, and theme loading.

## Visual Regression Tests

27 test cases in `test_data/cases/`. Compares `screenshot_tool` output against golden Chrome PNGs (`*_chrome.png`). Threshold: >= 95% pixel similarity = PASS.

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

## Configuration

`config/wlx-listerine-md.toml` (schema v2). Sections: `[general]` (extensions, detect_string), `[fonts]` (body, code, emoji + sizes), `[spacing]` (paragraph, heading, list, quote, code, line height), `[colors.light]` and `[colors.dark]` (10 colors each: background, text, heading, muted, link, link_hover, code_bg, quote_border, rule, selection).
