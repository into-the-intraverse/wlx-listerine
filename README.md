# 🧴 wlx-listerine

💾 **Total Commander** lister plugins for **Markdown** rendering and **syntax highlighting**.

⚡ Fast · 🪶 Lightweight · 💪 Memory efficient

---

## 📦 Plugins

### 📝 wlx-listerine-md — Markdown renderer
- Native **Direct2D/DirectWrite** rendering with **color emoji**
- GitHub-flavored Markdown: **tables**, **task lists**, **strikethrough**
- **Light** and **dark mode** (follows Windows theme)
- **Syntax-highlighted** code blocks via [Lexilla](https://www.scintilla.org/Lexilla.html)
- Clickable **links** (anchors, relative docs, external URLs)
- **Instant open** — files parse on a background thread, layout is viewport-lazy
- Optional **line-number gutter**
- **Configurable** fonts, spacing, and colors

### 🖍️ wlx-listerine-colorizer — Syntax colorizer
- **Lexilla** based tokenization (Scintilla's GUI-independent lexer library)
- Ships with lexers for **19 languages** (C/C++, Python, Rust, Java, JS, TS, and more); 7 additional file types render as plain text
- Line numbers, indent guides, whitespace markers
- Clickable **URLs** in any colorized text
- **Per-file language override** — pick a language from the right-click menu
- **Helix-compatible** color themes with **text modifiers** (bold, italic, underline, strikethrough) — drop in themes from the [Helix community](https://github.com/helix-editor/helix/tree/master/runtime/themes)
- **Instant open** — background sweep, viewport-scoped highlighting on scroll

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
2. Open each ZIP in Total Commander — both target the same `wlx-listerine\` plugin folder, so TC will offer to overwrite shared files on the second install (the shared core DLL and themes are byte-identical between the two ZIPs)
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
- [Language Support](docs/LANGUAGES.md) — supported languages and plain-text fallback list
- [Building from Source](docs/BUILDING.md) — build instructions for developers

## 📈 Performance

Both plugins only do work for the visible part of a file — parse once on a background thread, then lay out and color just what's on screen. The colorizer first colors the viewport, while a background sweep extracts *all* colors into a compact span table within a second or two; scrolling thereafter re-colors from the table. Both wrap modes share the same windowed grid — the layout holds only a viewport-sized slice of lines — so memory stays roughly flat as you scroll. The markdown renderer likewise evicts the text layouts of blocks scrolled far off-screen (rebuilding them byte-for-byte on return), so a large document's held memory stays bounded too. The **worst case** rows force whole-file processing instead: for the colorizer that is the fallback it takes only on an unsupported language, and for markdown it is the eager (non-lazy) layout the plugin no longer uses — both kept as regression sentinels (`json.hpp` is a deliberately brutal stress file for the lexer).

<!-- bench:begin -->
Measured on: AMD Ryzen 7 9800X3D 8-Core Processor, 62 GB RAM, Windows build 10.0.26200
Baseline: commit `112b8dc-dirty`, 2026-06-19, median of 5 runs (`scripts/bench.py`)

| Scenario | Open (ms) | Peak memory (MB) | Memory held (MB) |
|----------|-----------|------------------|------------------|
| markdown — [big.md](test_data/bench/big.md) (1.0 MB) | 131 | — | 57.9 |
| worst case: markdown full layout — [big.md](test_data/bench/big.md) (1.0 MB) | 254 | — | 168.5 |
| worst case: whole-file highlight [json.hpp](https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp) (0.9 MB) | 55 | 46.4 | 39.0 |
| worst case: whole-file highlight [sqlite3.c](https://www.sqlite.org/2025/sqlite-amalgamation-3500100.zip) (8.8 MB) | 353 | 259.2 | 207.5 |

<!-- bench:end -->

## 📄 License

[MIT](LICENSE)
