# Building from Source

## Prerequisites

- CMake 3.20+
- Conan 2.x
- MSVC (Visual Studio 2022 or Build Tools)
- Python 3.12+ and [uv](https://docs.astral.sh/uv/) (for visual regression tests)

## Build

```bash
conan install . --output-folder=build --build=missing -s build_type=Release -s compiler.cppstd=20
cmake --preset conan-default
cmake --build --preset conan-release
```

Build outputs land in `output/`:
- `wlx-listerine-md.wlx64` — Markdown renderer plugin
- `wlx-listerine-colorizer.wlx64` — Syntax colorizer plugin
- `wlx-listerine-core.dll` — Shared core DLL (tree-sitter engine, themes, grammar cache)
- `wlx-listerine-md.toml` — Markdown plugin config
- `wlx-listerine-colorizer.toml` — Colorizer plugin config
- `wlx-listerine-core.toml` — Shared core config (grammar cache + theme selection)
- `themes/default.toml` — Default syntax color theme
- `grammars/*.dll` — Tree-sitter grammar DLLs (if built)

The build produces three DLL artifacts in `output/`: `wlx-listerine-md.wlx64`,
`wlx-listerine-colorizer.wlx64`, and `wlx-listerine-core.dll` (shared by both
plugins). All three are versioned lockstep — never mix builds. The plugins
link against the core's import lib (`wlx-listerine-core.lib`).

## Tests

### Unit tests

```bash
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

### Visual regression tests

Compares `screenshot_tool` output against golden Chrome PNGs. Requires Cascadia Code font installed.

```bash
./scripts/visual-test.sh
```

To update golden references after intentional visual changes:

```bash
bun run update-goldens
bun run update-goldens -- 01_headings_atx  # single case
```

## Building grammars

The colorizer uses tree-sitter grammar DLLs. To build the shipped grammars (C, JSON, Python):

```bash
./scripts/build-grammars.sh
cmake --preset conan-default
cmake --build --preset conan-release
```

Grammar DLLs are placed in `grammars/`. See [Adding Languages](LANGUAGES.md) for how to add more.
