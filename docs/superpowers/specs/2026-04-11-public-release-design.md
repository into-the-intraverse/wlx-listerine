# wlx-listerine Public Release Design

## Overview

Prepare the wlx-listerine project (Total Commander WLX lister plugins) for public GitHub release. Two plugins: a markdown renderer (`wlx-listerine-md`) and a syntax colorizer (`wlx-listerine-colorizer`). Both are native C++17, Direct2D/DirectWrite, 64-bit only, Win11 target.

## Decisions

| Topic | Decision |
|-------|----------|
| Canonical name | `wlx-listerine` — repo rename from `wlx-mini-markdown` |
| License | MIT |
| Distribution | Two separate ZIPs with TC `pluginst.inf` auto-install |
| Config shipping | `.toml.sample` with explanation header; hardcoded defaults handle first run |
| Versioning | Tag-derived via `-DWLX_VERSION` compile flag; no version in source files |
| Release gate | Unit tests + visual regression tests must both pass |
| Documentation | Minimal README + linked detail docs |

## 1. CI Release Workflow

**File:** `.github/workflows/release.yml`
**Trigger:** Push of tags matching `v*`

Pipeline:
1. Checkout code
2. Setup Python 3.12 + uv, install Conan 2.x, detect profile
3. Extract version from tag (strip `v` prefix, e.g. `v1.0.0` → `1.0.0`)
4. `conan install . --output-folder=build --build=missing -s build_type=Release`
5. `cmake --preset conan-default -DWLX_VERSION=1.0.0`
6. `cmake --build --preset conan-release`
7. Run `./build/Release/tests.exe`
8. Run `./build/Release/colorizer-tests.exe`
9. Install Cascadia Code font
10. Run `./scripts/visual-test.sh`
11. Package two ZIPs (see Section 2)
12. Create GitHub Release with both ZIPs attached, tag name as title, auto-generated release notes

**Version injection in CMakeLists.txt:**
- Add `option(WLX_VERSION "Plugin version" "dev")` near the top
- Add `target_compile_definitions` with `-DWLX_VERSION_STRING=\"${WLX_VERSION}\"` to both plugin targets
- Default `"dev"` for local builds; CI overrides via `-DWLX_VERSION=X.Y.Z`

**Existing `visual-tests.yml` unchanged** — continues to run on push/PR to master.

## 2. Distribution Packaging

### pluginst.inf files

Stored in `config/` as source, renamed to `pluginst.inf` inside each ZIP.

**`config/pluginst-md.inf`:**
```ini
[plugininstall]
description=Fast, lightweight Markdown renderer with minimal memory footprint
type=wlx
file=wlx-listerine-md.wlx64
defaultdir=wlx-listerine-md
```

**`config/pluginst-colorizer.inf`:**
```ini
[plugininstall]
description=Fast, lightweight syntax colorizer with minimal memory footprint (tree-sitter). Easily customizable and extendable with more languages
type=wlx
file=wlx-listerine-colorizer.wlx64
defaultdir=wlx-listerine-colorizer
```

### .toml.sample files

Source `.toml` files in `config/` stay unchanged for development. CI copies them as `.toml.sample` into the ZIP, prepending a comment header:

```toml
# wlx-listerine-md configuration
# Rename this file to wlx-listerine-md.toml to customize.
# All values shown are the built-in defaults.
# The plugin works without this file — only create it to override specific settings.
```

(Same pattern for colorizer, with adjusted filename.)

### ZIP contents

```
wlx-listerine-md-X.Y.Z.zip
├── pluginst.inf
├── wlx-listerine-md.wlx64
└── wlx-listerine-md.toml.sample

wlx-listerine-colorizer-X.Y.Z.zip
├── pluginst.inf
├── wlx-listerine-colorizer.wlx64
├── wlx-listerine-colorizer.toml.sample
├── themes/
│   └── default.toml
└── grammars/
    ├── tree-sitter-c.dll
    ├── tree-sitter-json.dll
    └── tree-sitter-python.dll
```

## 3. Documentation

### README.md (project root)

Minimal and punchy:
- Project name + one-line description
- Two screenshots (md plugin + colorizer plugin side by side)
- Feature highlights (bullet list: fast, lightweight, minimal memory, Direct2D/DirectWrite, tree-sitter, dark/light mode, customizable)
- Installation: download ZIPs from Releases, open in TC, auto-installs
- Config note: rename `.toml.sample` to `.toml` to customize; works out of the box
- Requirements: Windows 11, Total Commander 11.00+ (64-bit)
- Links to detail docs
- MIT license badge

### docs/BUILDING.md

Build from source instructions:
- Prerequisites: CMake 3.20+, Conan 2.x, MSVC, Python 3.12 (for visual tests)
- Build commands (4-liner from CLAUDE.md)
- Running unit tests
- Running visual regression tests
- Building/adding grammars

### docs/CONFIGURATION.md

Config reference:
- Config discovery: plugin looks for `.toml` next to the `.wlx64` file
- Full annotated config for both plugins
- Theme customization (light/dark palettes, syntax colors)
- Display options (line numbers, word wrap, whitespace, indent guides)

### docs/LANGUAGES.md

Colorizer language support:
- Shipped grammars: C, JSON, Python
- How to add more: download/build a tree-sitter grammar DLL, drop into `grammars/`
- File extension → grammar mapping from the colorizer config

### LICENSE

MIT license, copyright holder: the git user on the repo.

## 4. Build System Changes

### CMakeLists.txt
- Add `set(WLX_VERSION "dev" CACHE STRING "Plugin version")` near the top
- Add `target_compile_definitions(<target> PRIVATE WLX_VERSION_STRING="${WLX_VERSION}")` to both plugin shared library targets
- CI passes `-DWLX_VERSION=X.Y.Z` on the cmake configure line to override the default

### .gitignore
- Verify `build/`, `output/`, `grammars/*.dll` are covered
- Add any missing entries

### Repo rename
- Rename GitHub repo from `wlx-mini-markdown` to `wlx-listerine`
- No workflow or code changes needed — all code already uses `wlx-listerine`

## Out of Scope

- CHANGELOG (first release — no history yet)
- CONTRIBUTING.md (premature — add when there's community interest)
- 32-bit plugin builds
- Automated installer beyond TC's `pluginst.inf`
- Version display in plugin UI
