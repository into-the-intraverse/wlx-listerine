# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Total Commander WLX lister plugin that renders Markdown files via Windows RichEdit control. C++17, 64-bit only. Outputs `wlx-mini-markdown.wlx64` (a DLL with custom suffix).

## Build

Requires CMake 3.20+, Conan 2.x, MSVC (static runtime `/MT`).

```bash
# Install dependencies (md4c, tomlplusplus, doctest)
conan install . --output-folder=build --build=missing -s build_type=Release

# Configure
cmake --preset conan-default

# Build (produces wlx64 plugin + test executable)
cmake --build --preset conan-release

# Run tests (doctest)
./build/Release/tests.exe
```

No lint tool is configured.

## Architecture

```
TC WLX API (plugin.cpp)
  → reads file, creates/reuses RichEdit window
  → calls RtfBuilder::build()

RtfBuilder (rtf_builder.cpp/h)
  → wraps md4c parser with SAX callbacks (enter/leave block/span, text)
  → assembles RTF string (font table, color table, formatted body)
  → returns vector<LinkInfo> for clickable link handling

Config (config.cpp/h)
  → loads optional TOML file next to DLL
  → provides fonts, colors (light/dark), extensions, detect string
  → silent fallback to compiled defaults on any error
```

**Data flow:** MD file → md4c SAX callbacks → RTF string → `EM_STREAMIN` to RichEdit control.

**Per-window state** (`WndData` in plugin.cpp) stored in `g_windows` unordered_map keyed by parent HWND. Holds RichEdit handle, link table, dark mode flag, file path.

## Key conventions

- WLX plugin API defined in `include/listerplugin.h` — do not modify
- `plugin.def` lists DLL exports — update when adding new API functions
- Config loaded once globally on first `ListLoad`; RichEdit window reused via `ListLoadNext`
- Non-ASCII text encoded as RTF `\uN?` sequences; supplementary plane uses surrogate pairs
- Images render as `[Image: alt text]` (no embedding)
- Tables use monospace font with tab stops (not true table cells)

## Tests

Tests use doctest in `tests/`. Test targets: config loading/parsing (`test_config.cpp`), RTF generation from markdown (`test_rtf_builder.cpp`). Test main is in `test_main.cpp`.

## Configuration

`config/wlx-mini-markdown.toml` is the default config template. At runtime the plugin looks for the TOML file next to the DLL. Sections: `[options]`, `[fonts]`, `[colors.light]`, `[colors.dark]`.
