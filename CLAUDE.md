# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Total Commander WLX lister plugin that renders Markdown via DirectWrite/Direct2D. C++17, 64-bit only, Win11 target. Outputs `wlx-mini-markdown.wlx64`.

## Build

Requires CMake 3.20+, Conan 2.x, MSVC.

```bash
conan install . --output-folder=build --build=missing -s build_type=Release
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

## WLX exports (Unicode-only)

`ListLoadW`, `ListLoadNextW`, `ListCloseWindow`, `ListGetDetectString`, `ListSendCommand`, `ListSetDefaultParams`. Defined in `plugin.def`. API constants in `include/listerplugin.h`.

## Key conventions

- Custom window class `WlxMiniMarkdownView` with DirectWrite/Direct2D rendering
- `ComPtr<T>` (from `<wrl/client.h>`) for all COM pointers
- D2D/DWrite factories are global singletons, created on first ListLoadW, released in DLL_PROCESS_DETACH
- `NOMINMAX` must be defined before any Windows header inclusion
- No text selection or search in current version (lc_copy/lc_selectall return ERROR)

## Tests

86 tests via doctest in `tests/`. Coverage: document model construction, theme/config TOML parsing, file encoding detection, markdown-to-AST conversion (30+ cases), cache hit/miss/bucketing, layout engine block positioning with real IDWriteFactory.

## Configuration

`config/wlx-mini-markdown.toml` (schema v2). Sections: `[general]` (extensions, detect_string), `[fonts]` (body, code, emoji + sizes), `[spacing]` (paragraph, heading, list, quote, code, line height), `[colors.light]` and `[colors.dark]` (10 colors each: background, text, heading, muted, link, link_hover, code_bg, quote_border, rule, selection).
