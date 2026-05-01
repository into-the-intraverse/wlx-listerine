# 🧴 wlx-listerine

💾 **Total Commander** lister plugins for **Markdown** rendering and **syntax highlighting**.

⚡ Fast · 🪶 Lightweight · 💪 Memory efficient

---

## 📦 Plugins

### 📝 wlx-listerine-md — Markdown renderer
- Native **Direct2D/DirectWrite** rendering
- **Light** and **dark mode** (follows Windows theme)
- **Syntax-highlighted** code blocks [tree-sitter](https://github.com/tree-sitter/tree-sitter)
- Clickable **links** (anchors, relative docs, external URLs)
- **Configurable** fonts, spacing, and colors

### 🖍️ wlx-listerine-colorizer — Syntax colorizer
- **Tree-sitter** based tokenization
- Line numbers, indent guides, whitespace markers
- Ships with **25+ grammars** (git, vim, c++, Unreal C++ opt-in, and more)
- **Easily extendable** — drop tree-sitter grammar DLLs into [grammars/](grammars)
- **Helix-compatible** color themes with **text modifiers** (bold, italic, underline, strikethrough) — drop in themes from the [Helix community](https://github.com/helix-editor/helix/tree/master/runtime/themes)

---

## 🖥️ Requirements

- 🪟 Windows 11
- 💾 Total Commander 11.00+ (64-bit)

## 📥 Installation

1. Download both `wlx-listerine-md-<version>.zip` and `wlx-listerine-colorizer-<version>.zip` from [**Releases**](../../releases)
2. Open `wlx-listerine-md-<version>.zip` in Total Commander first — it carries the shared core DLL, themes, and grammars alongside the markdown plugin
3. Then open `wlx-listerine-colorizer-<version>.zip` to register the colorizer — it lands in the same `wlx-listerine\` folder and reuses the shared payload
4. ✅ Done — both plugins share one folder, no duplication on disk

> Two ZIPs (instead of one) because Total Commander's `pluginst.inf` only auto-registers a single WLX plugin per install. Both ZIPs target the same `defaultdir`, so the install layout stays unified.

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

## 🚧 TODO

- **CPP highlighting on GHA windows-2025** — the upstream tree-sitter-cpp v0.23.4 grammar (ABI 14) emits no named-node spans on the GitHub Actions windows-2025 image, so plain `.cpp` files render only keyword tokens. Local builds with the same MSVC 14.44 toolset and conan binary work fine; root cause not yet pinned. The `Grammar: unreal-cpp` "highlights query loads" subcase is disabled until upstream ships an ABI 15 release or we route cpp through the taku25 fork.

## 📄 License

[MIT](LICENSE)
