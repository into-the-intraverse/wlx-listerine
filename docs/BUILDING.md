# Building from Source

## Prerequisites

- CMake 3.20+
- Conan 2.x
- MSVC (Visual Studio 2022 or Build Tools)
- Python 3.12+ and [uv](https://docs.astral.sh/uv/) (for visual regression tests and benchmarks)
- [Bun](https://bun.sh/) (for updating golden screenshots)

## Build

```bash
conan install . --output-folder=build --build=missing -s build_type=Release -s compiler.cppstd=20
cmake --preset conan-default
cmake --build --preset conan-release
```

Build outputs land in `output/`:
- `wlx-listerine-md.wlx64` — Markdown renderer plugin
- `wlx-listerine-colorizer.wlx64` — Syntax colorizer plugin
- `wlx-listerine-core.dll` — Shared core DLL (Lexilla engine, themes)
- `wlx-listerine-md.toml` — Markdown plugin config
- `wlx-listerine-colorizer.toml` — Colorizer plugin config
- `wlx-listerine-core.toml` — Shared core config (theme selection)
- `themes/default.toml`, `themes/default_light.toml` — Default syntax color themes

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

### Performance benchmarks

```bash
uv run scripts/bench.py            # run suite, compare against the README baseline
uv run scripts/bench.py --update   # re-measure and rewrite the README baseline
```

Requires a Release build of `screenshot_tool.exe`. Baselines are machine-specific — see the README "Performance" section.

## Packaging

```powershell
./scripts/package.ps1 -Version 1.2.3
```

Produces two self-contained release ZIPs (one per plugin), both targeting the same `wlx-listerine\` TC plugin directory. Each carries the full shared payload (core DLL, themes, `.toml.sample` configs) plus its own `pluginst.inf` — TC's installer only auto-registers one WLX plugin per ZIP.

## CI

GitHub Actions: `visual-tests.yml` runs the visual regression suite on every push/PR to `master`; `release.yml` builds, packages, and publishes a GitHub Release on version-tag push.
