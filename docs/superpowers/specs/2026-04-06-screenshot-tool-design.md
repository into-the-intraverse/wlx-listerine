# Screenshot Tool for Visual Testing

**Date:** 2026-04-06
**Status:** Approved

## Problem

The WLX plugin renders Markdown via Direct2D to an HWND render target. There is no way to programmatically capture the rendered output, making it impossible for Claude Code (or any automated process) to evaluate how rendered markdown looks and identify visual problems.

## Solution

A standalone CLI tool (`screenshot_tool.exe`) that renders a markdown file offscreen to a bitmap and saves it as PNG. Claude Code invokes it from the terminal, then reads the resulting image to evaluate rendering quality.

## CMake Restructuring

Extract shared sources into a static library. Three targets link against it:

```
wlx-core          (STATIC library)
  markdown_parser.cpp, layout_engine.cpp, render_engine.cpp,
  theme_service.cpp, file_service.cpp, cache_service.cpp,
  interaction_engine.cpp, document_model.h (header-only)

wlx-mini-markdown  (SHARED/MODULE -> .wlx64)
  host_adapter.cpp, plugin.def
  links: wlx-core

screenshot_tool    (EXECUTABLE)
  screenshot_main.cpp
  links: wlx-core

tests              (EXECUTABLE)
  test_*.cpp
  links: wlx-core
```

This eliminates the current source duplication between plugin and test targets.

## Offscreen Rendering

`RenderEngine` currently creates an `ID2D1HwndRenderTarget`. The tool needs a bitmap-backed target.

Changes to `RenderEngine`:

- Store `ComPtr<ID2D1RenderTarget>` (base interface) instead of `ComPtr<ID2D1HwndRenderTarget>`.
- Add `create_bitmap_resources(int width, int height)` — creates a WIC bitmap-backed render target.
- Add `save_to_png(const wchar_t* path)` — encodes the WIC bitmap to PNG via `IWICBitmapEncoder`.
- `paint()` unchanged — already works against the `ID2D1RenderTarget` base interface.
- `resize()` guarded to only call `Resize` on HWND targets.

## CLI Interface

```
screenshot_tool.exe <input.md> [options]

Options:
  --width <px>      Viewport width (default: 800)
  --height <px>     Viewport height for viewport mode (default: 600)
  --full            Render entire document as one tall image
  --scroll <px>     Scroll offset in viewport mode (default: 0)
  --config <path>   Path to TOML config (default: config/wlx-mini-markdown.toml)
  --dark            Force dark mode (otherwise follows config)
```

### Output

PNG saved to `test_data/<stem>.png` (or `test_data/<stem>_dark.png` with `--dark`). The `test_data` path is relative to the current working directory.

### Exit codes

- 0: success
- 1: error (message to stderr)

## Rendering Modes

**Viewport mode (default):** Creates bitmap at `(width, height)`, renders with the specified `--scroll` offset.

**Full-document mode (`--full`):** Runs layout to get `total_height`, creates bitmap at `(width, total_height)`, renders with `scroll_y = 0`.

## New File

`src/screenshot_main.cpp` (~150 lines):
- Parse CLI args
- Initialize D2D/DWrite factories
- Pipeline: `FileService::read()` -> `MarkdownParser::parse()` -> `LayoutEngine::layout()` -> `RenderEngine::paint()` -> `save_to_png()`

## Unchanged Components

- `host_adapter.cpp` — plugin path untouched
- `LayoutEngine`, `MarkdownParser`, `ThemeService`, `FileService`, `CacheService` — used as-is
- `InteractionEngine` — not needed for screenshots

## Dependencies

Same as plugin: d2d1, dwrite, windowscodecs (WIC), md4c, tomlplusplus. No new external dependencies.

## Configuration

Reads the same TOML config as the plugin (`config/wlx-mini-markdown.toml`). Dark mode follows config unless `--dark` flag is passed.
