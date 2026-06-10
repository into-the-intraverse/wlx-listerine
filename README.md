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

Both plugins only do work for the visible part of a file — parse once on a background thread, then lay out and color just what's on screen. The **worst case** rows instead force whole-file processing; you only hit them with `word_wrap = true` or an unsupported language (`json.hpp` is a deliberately brutal stress file). "Memory held" is mostly the parsed syntax tree — kept while the file is open so scrolling re-colors instantly, freed on close.

<!-- bench:begin -->
Measured on: AMD Ryzen 7 9800X3D 8-Core Processor, 62 GB RAM, Windows build 10.0.26200
Baseline: commit `123b4a7-dirty`, 2026-06-10, median of 5 runs (`scripts/bench.py`)

| Scenario | Open (ms) | Peak memory (MB) | Memory held (MB) |
|----------|-----------|------------------|------------------|
| markdown (1.0 MB) | 172 | — | 123.1 |
| C++ header json.hpp (0.9 MB) | 166 | 61.0 | 53.7 |
| C file sqlite3.c (8.8 MB) | 1020 | 393.3 | 349.5 |
| worst case: markdown full layout (1.0 MB) | 312 | — | 170.1 |
| worst case: whole-file highlight json.hpp (0.9 MB) | 7355 | 52.5 | 45.2 |
| worst case: whole-file highlight sqlite3.c (8.8 MB) | 2041 | 336.2 | 250.7 |

<!-- bench:end -->

## 🚧 TODO

- **CPP highlighting on GHA windows-2025** — the upstream tree-sitter-cpp v0.23.4 grammar (ABI 14) emits no named-node spans on the GitHub Actions windows-2025 image, so plain `.cpp` files render only keyword tokens. Local builds with the same MSVC 14.44 toolset and conan binary work fine; root cause not yet pinned. The `Grammar: unreal-cpp` "highlights query loads" subcase is disabled until upstream ships an ABI 15 release or we route cpp through the taku25 fork.

Deferred from the 2026-06 code review (verified real, fix postponed):

- **Cancellable parses in the core DLL** — the process-wide registry mutex is held for the duration of every `colorize()`/`parse()`, and a superseded worker parse runs to completion, so opening a small file while a 100&nbsp;MB parse is in flight blocks `WM_PAINT` until the dead parse finishes. Needs cancellation plumbed through the C ABI (`ts_parser_set_cancellation_flag`) and/or a try-lock viewport highlight.
- **Unify the UTF-8↔UTF-16 offset converters** — `code_fence_layout.cpp` and `colorizer_layout.cpp` each carry their own byte↔wchar offset mapping (both now correct, still duplicated ~80 lines). Extract one shared offset-map primitive.
- **Incremental line index for lazy markdown** — `materialize_viewport` rebuilds the whole `line_tops` index per paint that materializes anything; block shifts are batched now, the index rebuild is not. Needs an incremental index design. (`apply_height_delta` is kept alive only by tests since the batching.)
- **Aligned table cells measure hit-rects pre-alignment** — center/right-aligned cells hit-test span/code-bg rects before `SetTextAlignment`, the same class of bug fixed for header bold; fixing it means passing alignment into `build_inline_layout`.
- **`/W4 /WX`** — no warning level is configured anywhere (MSVC default /W3); enabling it needs a one-time warning cleanup pass.
- **Small cleanups** — `search_step` returns the full match vector by value (copied per F3); `layout_source` carries a dead `source` parameter (call sites span both plugins and the screenshot tool); `abi_spans_to_result` is duplicated between the colorizer adapter and the screenshot tool.
- **Known limitation (accepted)** — on FAT/exFAT volumes the parse cache can serve a stale document for a same-size save within the 2-second mtime granularity window (`ParseCacheKey` is path+size+mtime; no content hash).

## 📄 License

[MIT](LICENSE)
