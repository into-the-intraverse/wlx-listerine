# 🧴 wlx-listerine

💾 **Total Commander** lister plugins for **Markdown** rendering and **syntax highlighting**.

⚡ Fast · 🪶 Lightweight · 💪 Memory efficient

---

## 📦 Plugins

### 📝 wlx-listerine-md — Markdown renderer
- Native **Direct2D/DirectWrite** rendering with **color emoji**
- GitHub-flavored Markdown: **tables**, **task lists**, **strikethrough**
- **Light** and **dark mode** (follows Windows theme)
- **Syntax-highlighted** code blocks via [tree-sitter](https://github.com/tree-sitter/tree-sitter)
- Clickable **links** (anchors, relative docs, external URLs)
- **Instant open** — files parse on a background thread, layout is viewport-lazy
- Optional **line-number gutter**
- **Configurable** fonts, spacing, and colors

### 🖍️ wlx-listerine-colorizer — Syntax colorizer
- **Tree-sitter** based tokenization
- Ships with grammars for **26 languages** (git, vim, c++, Unreal C++ opt-in, and more)
- Line numbers, indent guides, whitespace markers
- Clickable **URLs** in any colorized text
- **Per-file grammar override** — pick a language from the right-click menu
- **Easily extendable** — drop tree-sitter grammar DLLs into [grammars/](grammars)
- **Helix-compatible** color themes with **text modifiers** (bold, italic, underline, strikethrough) — drop in themes from the [Helix community](https://github.com/helix-editor/helix/tree/master/runtime/themes)
- **Instant open** — background parse, viewport-scoped highlighting on scroll

### 🖱️ Both plugins
- **Text selection** with clipboard copy (drag, double-click word select)
- **Text search** via Lister's find dialog, with match highlighting and a **counter HUD** (`3/17`, prev/next)
- **Go to line** prompt (`Ctrl+G`)
- **Right-click context menu** — copy, select all, search Google for selection, link actions, copy code block, edit config

| Key | Action |
|-----|--------|
| `Ctrl+C` / `Ctrl+A` | Copy selection / select all |
| `Ctrl+G` | Go to line |
| `F2` | Reload file |
| `Esc` | Clear selection → clear search matches → close Lister |

---

## 🖥️ Requirements

- 🪟 Windows 11
- 💾 Total Commander 11.00+ (64-bit)

## 📥 Installation

1. Download `wlx-listerine-md-<version>.zip` and `wlx-listerine-colorizer-<version>.zip` from [**Releases**](../../releases)
2. Open each ZIP in Total Commander — both target the same `wlx-listerine\` plugin folder, so TC will offer to overwrite shared files on the second install (the shared core DLL, themes, and grammars are byte-identical between the two ZIPs)
3. ✅ Done — install order does not matter; either ZIP works standalone if you only want one plugin

> Two ZIPs (instead of one) because Total Commander's `pluginst.inf` only auto-registers a single WLX plugin per install. Both ZIPs are self-contained and target the same `defaultdir`, so the on-disk layout stays unified after both are installed.

## ⚙️ Configuration

Each plugin ships with a `.toml.sample` file showing all available settings. To customize:

1. Copy `wlx-listerine-md.toml.sample` → `wlx-listerine-md.toml` (same directory as the `.wlx64`)
2. Edit the values you want to change
3. Restart Total Commander

See [docs/CONFIGURATION.md](docs/CONFIGURATION.md) for the full reference.

## 📖 Documentation

- [Configuration Reference](docs/CONFIGURATION.md) — all settings for both plugins
- [Adding Languages](docs/LANGUAGES.md) — how to add more syntax grammars
- [Building from Source](docs/BUILDING.md) — build instructions for developers

## 📈 Performance

Both plugins only do work for the visible part of a file — parse once on a background thread, then lay out and color just what's on screen. The colorizer first colors the viewport straight from the syntax tree, while a background sweep extracts *all* colors into a compact span table within a second or two and **frees the tree** (the single biggest retained object); scrolling thereafter re-colors from the table. Both wrap modes share the same windowed grid — the layout holds only a viewport-sized slice of lines — so memory stays roughly flat as you scroll. The markdown renderer likewise evicts the text layouts of blocks scrolled far off-screen (rebuilding them byte-for-byte on return), so a large document's held memory stays bounded too. The **worst case** rows force whole-file processing instead: for the colorizer that is the fallback it takes only on an unsupported language or a parse failure, and for markdown it is the eager (non-lazy) layout the plugin no longer uses — both kept as regression sentinels (`json.hpp` is a deliberately brutal stress file for the syntax-query engine).

<!-- bench:begin -->
Measured on: AMD Ryzen 7 9800X3D 8-Core Processor, 62 GB RAM, Windows build 10.0.26200
Baseline: commit `21f6660-dirty`, 2026-06-14, median of 5 runs (`scripts/bench.py`)

| Scenario | Open (ms) | Peak memory (MB) | Memory held (MB) |
|----------|-----------|------------------|------------------|
| markdown — [big.md](test_data/bench/big.md) (1.0 MB) | 181 | — | 61.5 |
| C++ header [json.hpp](https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp) (0.9 MB) | 160 | 57.0 | 28.7 |
| C file [sqlite3.c](https://www.sqlite.org/2025/sqlite-amalgamation-3500100.zip) (8.8 MB) | 892 | 266.3 | 74.5 |
| post-scroll [sqlite3.c](https://www.sqlite.org/2025/sqlite-amalgamation-3500100.zip) (20 screens) (8.8 MB) | 891 | 266.2 | 78.7 |
| worst case: markdown full layout — [big.md](test_data/bench/big.md) (1.0 MB) | 315 | — | 169.9 |
| worst case: whole-file highlight [json.hpp](https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp) (0.9 MB) | 7343 | 52.5 | 45.1 |
| worst case: whole-file highlight [sqlite3.c](https://www.sqlite.org/2025/sqlite-amalgamation-3500100.zip) (8.8 MB) | 2049 | 336.2 | 250.2 |

<!-- bench:end -->

## 📄 License

[MIT](LICENSE)
