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

Both plugins only do work for the visible part of a file — parse once on a background thread, then lay out and color just what's on screen. The colorizer first colors the viewport straight from the syntax tree, while a background sweep extracts *all* colors into a compact span table within ~1–2 s and **frees the tree** (the single biggest retained object); scrolling thereafter re-colors from the table. The no-wrap layout holds only a viewport-sized window of blocks, so memory stays roughly flat as you scroll. The **worst case** rows instead force whole-file processing; you only hit them with `word_wrap = true` or an unsupported language (`json.hpp` is a deliberately brutal stress file).

<!-- bench:begin -->
Measured on: AMD Ryzen 7 9800X3D 8-Core Processor, 62 GB RAM, Windows build 10.0.26200
Baseline: commit `bcaf847-dirty`, 2026-06-11, median of 5 runs (`scripts/bench.py`)

| Scenario | Open (ms) | Peak memory (MB) | Memory held (MB) |
|----------|-----------|------------------|------------------|
| markdown — [big.md](test_data/bench/big.md) (1.0 MB) | 166 | — | 123.1 |
| C++ header [json.hpp](https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp) (0.9 MB) | 159 | 62.8 | 28.8 |
| C file [sqlite3.c](https://www.sqlite.org/2025/sqlite-amalgamation-3500100.zip) (8.8 MB) | 893 | 270.8 | 74.2 |
| post-scroll [sqlite3.c](https://www.sqlite.org/2025/sqlite-amalgamation-3500100.zip) (20 screens) (8.8 MB) | 897 | 270.7 | 78.1 |
| worst case: markdown full layout — [big.md](test_data/bench/big.md) (1.0 MB) | 313 | — | 169.9 |
| worst case: whole-file highlight [json.hpp](https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp) (0.9 MB) | 7113 | 52.6 | 45.2 |
| worst case: whole-file highlight [sqlite3.c](https://www.sqlite.org/2025/sqlite-amalgamation-3500100.zip) (8.8 MB) | 2062 | 336.2 | 250.5 |

<!-- bench:end -->

## 🚧 TODO

- **CPP highlighting on GHA windows-2025 (dormant, watching)** — in 2026-04 the upstream tree-sitter-cpp v0.23.4 grammar (ABI 14) emitted no named-node spans on the GitHub Actions windows-2025 image (plain `.cpp` files rendered only keyword tokens; local builds with the same MSVC 14.44 toolset were fine; root cause never pinned, though parser.c is compiled with MSVC optimization off, so suspicion falls on scanner.cc / the tree-sitter runtime / the image itself). Not reproducible since: every CI run from 2026-05-06 on — including the strict `sample.cpp` token-golden diff — is green, so the `Grammar: unreal-cpp` "highlights query loads" subcase was re-enabled 2026-06-11. If it reds out again, capture the run URL and report upstream (no matching issue exists). Upstream still has no ABI 15 release (v0.23.4, Nov 2024, is the latest; master is ABI 15 but unreleased and carries the [#357](https://github.com/tree-sitter/tree-sitter-cpp/issues/357) regen regression).

Deferred from the 2026-06 code review (verified real, fix postponed):

- **Cancellable parses in the core DLL** — the process-wide registry mutex is held for the duration of every `colorize()`/`parse()`, and a superseded worker parse runs to completion, so opening a small file while a 100&nbsp;MB parse is in flight blocks `WM_PAINT` until the dead parse finishes. Needs cancellation plumbed through the C ABI (`ts_parser_set_cancellation_flag`) and/or a try-lock viewport highlight.
- **Incremental line index for lazy markdown** — `materialize_viewport` rebuilds the whole `line_tops` index per paint that materializes anything; block shifts are batched now, the index rebuild is not. Needs an incremental index design. (`apply_height_delta` is kept alive only by tests since the batching.)
- **Known limitation (accepted)** — on FAT/exFAT volumes the parse cache can serve a stale document for a same-size save within the 2-second mtime granularity window (`ParseCacheKey` is path+size+mtime; no content hash).

## 📄 License

[MIT](LICENSE)
