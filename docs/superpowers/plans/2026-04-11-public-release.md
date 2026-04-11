# wlx-listerine Public Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prepare the wlx-listerine project for public GitHub release with CI-built artifacts, TC auto-install ZIPs, and user documentation.

**Architecture:** Add a tag-triggered GitHub Actions release workflow that builds, tests, packages two plugin ZIPs (with `pluginst.inf` for TC auto-install), and publishes a GitHub Release. Add MIT license, README, and linked docs. Inject version from git tags via CMake.

**Tech Stack:** GitHub Actions, CMake, PowerShell (ZIP packaging in CI), Markdown (docs)

---

### Task 1: Version injection in CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt:1-5` (add version cache variable)
- Modify: `CMakeLists.txt:53-65` (add compile definition to md plugin)
- Modify: `CMakeLists.txt:86-107` (add compile definition to colorizer plugin)

- [ ] **Step 1: Add version cache variable**

Add after line 2 (`project(wlx-listerine LANGUAGES C CXX)`):

```cmake
set(WLX_VERSION "dev" CACHE STRING "Plugin version string, overridden by CI")
```

- [ ] **Step 2: Add compile definition to md plugin target**

Add after the `set_target_properties(wlx-listerine-md ...)` block (after line 72):

```cmake
target_compile_definitions(wlx-listerine-md PRIVATE WLX_VERSION_STRING="${WLX_VERSION}")
```

- [ ] **Step 3: Add compile definition to colorizer plugin target**

Add after the `set_target_properties(wlx-listerine-colorizer ...)` block (after line 107):

```cmake
target_compile_definitions(wlx-listerine-colorizer PRIVATE WLX_VERSION_STRING="${WLX_VERSION}")
```

- [ ] **Step 4: Verify build still works**

Run:
```bash
conan install . --output-folder=build --build=missing -s build_type=Release
cmake --preset conan-default
cmake --build --preset conan-release
```
Expected: Build succeeds. Both `.wlx64` files in `output/`.

- [ ] **Step 5: Run unit tests**

Run:
```bash
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add WLX_VERSION cache variable for CI version injection"
```

---

### Task 2: pluginst.inf files

**Files:**
- Create: `config/pluginst-md.inf`
- Create: `config/pluginst-colorizer.inf`

- [ ] **Step 1: Create md plugin pluginst.inf**

Create `config/pluginst-md.inf`:

```ini
[plugininstall]
description=Fast, lightweight Markdown renderer with minimal memory footprint
type=wlx
file=wlx-listerine-md.wlx64
defaultdir=wlx-listerine-md
```

- [ ] **Step 2: Create colorizer plugin pluginst.inf**

Create `config/pluginst-colorizer.inf`:

```ini
[plugininstall]
description=Fast, lightweight syntax colorizer with minimal memory footprint (tree-sitter). Easily customizable and extendable with more languages
type=wlx
file=wlx-listerine-colorizer.wlx64
defaultdir=wlx-listerine-colorizer
```

- [ ] **Step 3: Commit**

```bash
git add config/pluginst-md.inf config/pluginst-colorizer.inf
git commit -m "dist: add TC pluginst.inf files for auto-install from ZIP"
```

---

### Task 3: GitHub Actions release workflow

**Files:**
- Create: `.github/workflows/release.yml`

- [ ] **Step 1: Create the release workflow**

Create `.github/workflows/release.yml`:

```yaml
name: Release

on:
  push:
    tags: ["v*"]

jobs:
  release:
    runs-on: windows-latest
    permissions:
      contents: write

    steps:
      - uses: actions/checkout@v4

      - name: Extract version from tag
        id: version
        shell: bash
        run: echo "version=${GITHUB_REF_NAME#v}" >> "$GITHUB_OUTPUT"

      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: "3.12"

      - name: Install uv
        uses: astral-sh/setup-uv@v5

      - name: Install Conan
        run: |
          uv pip install conan
          conan profile detect

      - name: Install C++ dependencies
        run: conan install . --output-folder=build --build=missing -s build_type=Release

      - name: Configure CMake
        run: cmake --preset conan-default -DWLX_VERSION=${{ steps.version.outputs.version }}

      - name: Build
        run: cmake --build --preset conan-release

      - name: Run unit tests
        run: |
          ./build/Release/tests.exe
          ./build/Release/colorizer-tests.exe

      - name: Install Cascadia Code font
        shell: pwsh
        run: |
          $url = "https://github.com/microsoft/cascadia-code/releases/download/v2404.23/CascadiaCode-2404.23.zip"
          Invoke-WebRequest -Uri $url -OutFile cascadia.zip
          Expand-Archive cascadia.zip -DestinationPath cascadia
          $shell = New-Object -ComObject Shell.Application
          $fonts = $shell.Namespace(0x14)
          Get-ChildItem cascadia/ttf/static/*.ttf | ForEach-Object {
              Copy-Item $_.FullName "$env:LOCALAPPDATA\Microsoft\Windows\Fonts\" -Force
              New-ItemProperty -Path "HKCU:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts" `
                  -Name $_.BaseName -Value $_.FullName -PropertyType String -Force | Out-Null
          }

      - name: Run visual regression tests
        shell: bash
        run: ./scripts/visual-test.sh

      - name: Package md plugin ZIP
        shell: pwsh
        run: |
          $version = "${{ steps.version.outputs.version }}"
          $staging = "staging/wlx-listerine-md"
          New-Item -ItemType Directory -Path $staging -Force

          Copy-Item "config/pluginst-md.inf" "$staging/pluginst.inf"
          Copy-Item "output/wlx-listerine-md.wlx64" "$staging/"

          $header = @"
          # wlx-listerine-md configuration
          # Rename this file to wlx-listerine-md.toml to customize.
          # All values shown are the built-in defaults.
          # The plugin works without this file — only create it to override specific settings.

          "@
          $config = Get-Content "config/wlx-listerine-md.toml" -Raw
          Set-Content -Path "$staging/wlx-listerine-md.toml.sample" -Value ($header + $config) -NoNewline

          Compress-Archive -Path "$staging/*" -DestinationPath "wlx-listerine-md-$version.zip"

      - name: Package colorizer plugin ZIP
        shell: pwsh
        run: |
          $version = "${{ steps.version.outputs.version }}"
          $staging = "staging/wlx-listerine-colorizer"
          New-Item -ItemType Directory -Path $staging -Force

          Copy-Item "config/pluginst-colorizer.inf" "$staging/pluginst.inf"
          Copy-Item "output/wlx-listerine-colorizer.wlx64" "$staging/"

          $header = @"
          # wlx-listerine-colorizer configuration
          # Rename this file to wlx-listerine-colorizer.toml to customize.
          # All values shown are the built-in defaults.
          # The plugin works without this file — only create it to override specific settings.

          "@
          $config = Get-Content "config/wlx-listerine-colorizer.toml" -Raw
          Set-Content -Path "$staging/wlx-listerine-colorizer.toml.sample" -Value ($header + $config) -NoNewline

          Copy-Item "output/themes" "$staging/themes" -Recurse
          Copy-Item "output/grammars" "$staging/grammars" -Recurse

          Compress-Archive -Path "$staging/*" -DestinationPath "wlx-listerine-colorizer-$version.zip"

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          generate_release_notes: true
          files: |
            wlx-listerine-md-*.zip
            wlx-listerine-colorizer-*.zip

      - name: Upload diff artifacts on failure
        if: failure()
        uses: actions/upload-artifact@v4
        with:
          name: visual-diffs
          path: test_data/cases/*_diff.png
          retention-days: 14
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: add tag-triggered release workflow with TC auto-install ZIPs"
```

---

### Task 4: Add colorizer tests to existing CI workflow

The existing `visual-tests.yml` only runs `tests.exe` but not `colorizer-tests.exe`. Fix this so PR CI also gates on colorizer tests.

**Files:**
- Modify: `.github/workflows/visual-tests.yml:37-38`

- [ ] **Step 1: Add colorizer tests to the unit test step**

Change the "Run unit tests" step from:

```yaml
      - name: Run unit tests
        run: ./build/Release/tests.exe
```

to:

```yaml
      - name: Run unit tests
        run: |
          ./build/Release/tests.exe
          ./build/Release/colorizer-tests.exe
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/visual-tests.yml
git commit -m "ci: add colorizer-tests to PR workflow"
```

---

### Task 5: LICENSE file

**Files:**
- Create: `LICENSE`

- [ ] **Step 1: Create MIT license file**

Create `LICENSE` with the standard MIT license text. Copyright holder: use the git user name from the repo. Year: 2025 (project start year based on commit history) through 2026.

```
MIT License

Copyright (c) 2025-2026 Aleksej Pawlowskij

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

- [ ] **Step 2: Commit**

```bash
git add LICENSE
git commit -m "docs: add MIT license"
```

---

### Task 6: README.md

**Files:**
- Create: `README.md`

- [ ] **Step 1: Write README**

Create `README.md` with:

```markdown
# wlx-listerine

Total Commander lister plugins for Markdown rendering and syntax highlighting. Fast, lightweight, minimal memory footprint.

## Plugins

**wlx-listerine-md** — Markdown renderer
- Native Direct2D/DirectWrite rendering
- Light and dark mode (follows Windows theme)
- Syntax-highlighted code blocks (tree-sitter)
- Clickable links (anchors, relative docs, external URLs)
- Configurable fonts, spacing, and colors

**wlx-listerine-colorizer** — Syntax colorizer
- Tree-sitter based tokenization
- Line numbers, indent guides, whitespace markers
- Ships with C, JSON, Python grammars
- Easily extendable — drop tree-sitter grammar DLLs into `grammars/`
- Customizable color themes

## Requirements

- Windows 11
- Total Commander 11.00+ (64-bit)

## Installation

1. Download the plugin ZIPs from [Releases](../../releases)
2. Open each ZIP in Total Commander — it will offer to auto-install
3. Done — the plugins work out of the box with built-in defaults

## Configuration

Each plugin ships with a `.toml.sample` file showing all available settings. To customize:

1. Copy `wlx-listerine-md.toml.sample` to `wlx-listerine-md.toml` (same directory as the `.wlx64`)
2. Edit the values you want to change
3. Restart Total Commander

See [docs/CONFIGURATION.md](docs/CONFIGURATION.md) for the full reference.

## Documentation

- [Configuration Reference](docs/CONFIGURATION.md) — all settings for both plugins
- [Adding Languages](docs/LANGUAGES.md) — how to add more syntax grammars
- [Building from Source](docs/BUILDING.md) — build instructions for developers

## License

[MIT](LICENSE)
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: add README"
```

---

### Task 7: docs/BUILDING.md

**Files:**
- Create: `docs/BUILDING.md`

- [ ] **Step 1: Write build instructions**

Create `docs/BUILDING.md`:

```markdown
# Building from Source

## Prerequisites

- CMake 3.20+
- Conan 2.x
- MSVC (Visual Studio 2022 or Build Tools)
- Python 3.12+ and [uv](https://docs.astral.sh/uv/) (for visual regression tests)

## Build

```bash
conan install . --output-folder=build --build=missing -s build_type=Release
cmake --preset conan-default
cmake --build --preset conan-release
```

Build outputs land in `output/`:
- `wlx-listerine-md.wlx64` — Markdown renderer plugin
- `wlx-listerine-colorizer.wlx64` — Syntax colorizer plugin
- `wlx-listerine-md.toml` — Markdown plugin config
- `wlx-listerine-colorizer.toml` — Colorizer plugin config
- `themes/default.toml` — Default syntax color theme
- `grammars/*.dll` — Tree-sitter grammar DLLs (if built)

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
```

- [ ] **Step 2: Commit**

```bash
git add docs/BUILDING.md
git commit -m "docs: add build-from-source instructions"
```

---

### Task 8: docs/CONFIGURATION.md

**Files:**
- Create: `docs/CONFIGURATION.md`

- [ ] **Step 1: Write configuration reference**

Create `docs/CONFIGURATION.md`:

```markdown
# Configuration Reference

Both plugins look for their `.toml` config file in the same directory as the `.wlx64` file. If no config file is found, built-in defaults are used — the plugins work out of the box.

Each release includes a `.toml.sample` showing all defaults. To customize, rename it to `.toml` and edit.

## Markdown Plugin (wlx-listerine-md.toml)

### [general]

| Key | Default | Description |
|-----|---------|-------------|
| `extensions` | `["md", "markdown", "mdown", "mkd", "mkdn"]` | File extensions to handle |
| `detect_string` | `EXT="MD" \| EXT="MARKDOWN"` | TC detect string |

### [fonts]

| Key | Default | Description |
|-----|---------|-------------|
| `body` | `"Segoe UI"` | Body text font |
| `body_size` | `14.0` | Body font size in points |
| `code` | `"Cascadia Code"` | Code block font |
| `code_size` | `13.0` | Code font size in points |
| `emoji` | `"Segoe UI Emoji"` | Emoji font |

### [spacing]

| Key | Default | Description |
|-----|---------|-------------|
| `paragraph` | `12.0` | Space between paragraphs |
| `heading_above` | `24.0` | Space above headings |
| `heading_below` | `12.0` | Space below headings |
| `list_indent` | `24.0` | List indentation |
| `quote_indent` | `16.0` | Blockquote indentation |
| `quote_border_width` | `3.0` | Blockquote border width |
| `code_padding` | `8.0` | Code block padding |
| `line_height_factor` | `1.5` | Line height multiplier |

### [colors.light] / [colors.dark]

| Key | Light Default | Dark Default | Description |
|-----|--------------|--------------|-------------|
| `background` | `#FFFFFF` | `#1E1E1E` | Background |
| `text` | `#1F2328` | `#D4D4D4` | Body text |
| `heading` | `#1F2328` | `#E0E0E0` | Heading text |
| `muted` | `#57606A` | `#808080` | Secondary text |
| `link` | `#0969DA` | `#58A6FF` | Link text |
| `link_hover` | `#0550AE` | `#79C0FF` | Link hover |
| `code_bg` | `#E8ECF0` | `#2D2D2D` | Code background |
| `quote_border` | `#D0D7DE` | `#404040` | Blockquote border |
| `rule` | `#D8DEE4` | `#404040` | Horizontal rule |
| `selection` | `#DDEBFF` | `#264F78` | Text selection |

### [code]

| Key | Default | Description |
|-----|---------|-------------|
| `grammar_dir` | `"grammars"` | Directory for tree-sitter grammar DLLs (relative to plugin) |
| `theme_dir` | `"themes"` | Directory for syntax color themes (relative to plugin) |
| `default_language` | `""` | Default language for unfenced code blocks |
| `theme` | `"default"` | Syntax color theme name |

## Colorizer Plugin (wlx-listerine-colorizer.toml)

### [general]

| Key | Default | Description |
|-----|---------|-------------|
| `extensions` | `["c", "cpp", "h", "hpp", "py", "js", ...]` | File extensions to handle (30+ languages) |
| `detect_string` | `EXT="C" \| EXT="CPP" \| ...` | TC detect string |
| `grammar_dir` | `"grammars"` | Directory for tree-sitter grammar DLLs |
| `theme_dir` | `"themes"` | Directory for syntax color themes |

### [display]

| Key | Default | Description |
|-----|---------|-------------|
| `line_numbers` | `true` | Show line numbers |
| `word_wrap` | `false` | Wrap long lines |
| `tab_width` | `4` | Tab display width |
| `show_whitespace` | `"boundary"` | Whitespace markers: `"none"`, `"all"`, or `"boundary"` |
| `show_indent_guides` | `true` | Show indentation guides |
| `highlight_trailing` | `true` | Highlight trailing whitespace |

### [fonts]

| Key | Default | Description |
|-----|---------|-------------|
| `code` | `"Cascadia Code"` | Monospace font |
| `code_size` | `13.0` | Font size in points |

### [spacing]

| Key | Default | Description |
|-----|---------|-------------|
| `line_height_factor` | `1.4` | Line height multiplier |

### [colors.light] / [colors.dark]

Same keys as the markdown plugin (see above).

### [themes]

| Key | Default | Description |
|-----|---------|-------------|
| `default` | `"default"` | Syntax color theme name |

## Syntax Color Themes

Theme files live in the `themes/` directory. The default theme (`themes/default.toml`) defines colors for both light and dark modes:

| Token | Description |
|-------|-------------|
| `keyword` | Language keywords (`if`, `for`, `return`) |
| `keyword2` | Secondary keywords (`int`, `bool`, type keywords) |
| `function` | Function names |
| `string` | String literals |
| `number` | Numeric literals |
| `comment` | Comments |
| `operator` | Operators |
| `type` | Type names |
| `preprocessor` | Preprocessor directives |
| `namespace` | Namespace identifiers |
| `variable` | Variable names |
| `punctuation` | Punctuation (braces, semicolons) |
| `plain` | Default/unmatched text |

To create a custom theme, copy `themes/default.toml` to a new name and set `theme = "yourname"` in the config.
```

- [ ] **Step 2: Commit**

```bash
git add docs/CONFIGURATION.md
git commit -m "docs: add configuration reference"
```

---

### Task 9: docs/LANGUAGES.md

**Files:**
- Create: `docs/LANGUAGES.md`

- [ ] **Step 1: Write language support docs**

Create `docs/LANGUAGES.md`:

```markdown
# Language Support

## Shipped Grammars

The colorizer plugin ships with grammars for:

| Language | Grammar DLL | Extensions |
|----------|------------|------------|
| C | `tree-sitter-c.dll` | `.c`, `.h` |
| JSON | `tree-sitter-json.dll` | `.json` |
| Python | `tree-sitter-python.dll` | `.py` |

Files with other extensions are displayed as plain text with line numbers and whitespace markers, but without syntax highlighting.

## Adding More Languages

The colorizer uses [tree-sitter](https://tree-sitter.github.io/) grammars. To add support for a new language:

1. **Get the grammar DLL.** Either:
   - Build it from a tree-sitter grammar repository (see below)
   - Download a pre-built DLL if available

2. **Drop it in `grammars/`.** Name it `tree-sitter-<language>.dll` (e.g., `tree-sitter-rust.dll`).

3. **Add the extension mapping** in `wlx-listerine-colorizer.toml`:
   ```toml
   [general]
   extensions = ["c", "cpp", "h", "hpp", "py", "json", "rs"]  # add "rs" for Rust
   ```

4. **Restart Total Commander.**

## Building a Grammar DLL

Requires CMake, MSVC, and the tree-sitter library (already available if you build the project from source).

1. Clone the grammar repository:
   ```bash
   git clone --depth 1 https://github.com/tree-sitter/tree-sitter-rust build/grammars/rust
   ```

2. Add it to `CMakeLists.txt` (or use the `add_grammar` function already defined there):
   ```cmake
   add_grammar(rust "${CMAKE_SOURCE_DIR}/build/grammars/rust")
   ```

3. Rebuild:
   ```bash
   cmake --preset conan-default
   cmake --build --preset conan-release
   ```

The DLL appears in `grammars/tree-sitter-rust.dll`.

## Tree-sitter Grammar Repositories

Common grammars available at:
- [tree-sitter-rust](https://github.com/tree-sitter/tree-sitter-rust)
- [tree-sitter-go](https://github.com/tree-sitter/tree-sitter-go)
- [tree-sitter-javascript](https://github.com/tree-sitter/tree-sitter-javascript)
- [tree-sitter-typescript](https://github.com/tree-sitter/tree-sitter-typescript)
- [tree-sitter-cpp](https://github.com/tree-sitter/tree-sitter-cpp)
- [tree-sitter-java](https://github.com/tree-sitter/tree-sitter-java)
- [tree-sitter-bash](https://github.com/tree-sitter/tree-sitter-bash)

Full list: [tree-sitter GitHub org](https://github.com/tree-sitter)
```

- [ ] **Step 2: Commit**

```bash
git add docs/LANGUAGES.md
git commit -m "docs: add language support guide"
```

---

### Task 10: .gitignore cleanup

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: Add missing entries**

Add to `.gitignore`:

```gitignore
# Staging directories used by CI packaging
staging/

# Release ZIPs
*.zip

# Claude Code
.claude/

# Playwright MCP
.playwright-mcp/

# Superpowers plugin cache
.superpowers/
```

- [ ] **Step 2: Commit**

```bash
git add .gitignore
git commit -m "chore: update .gitignore for release artifacts and tool dirs"
```

---

### Task 11: Set git remote and verify

**Files:** None (git config only)

- [ ] **Step 1: Add remote origin if not set**

```bash
git remote add origin git@github.com:into-the-intraverse/wlx-listerine.git
```

If remote already exists, update it:

```bash
git remote set-url origin git@github.com:into-the-intraverse/wlx-listerine.git
```

- [ ] **Step 2: Verify remote**

```bash
git remote -v
```

Expected:
```
origin  git@github.com:into-the-intraverse/wlx-listerine.git (fetch)
origin  git@github.com:into-the-intraverse/wlx-listerine.git (push)
```

Do NOT push yet — user will push and tag when ready.
