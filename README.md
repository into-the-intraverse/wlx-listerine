# 🧴 wlx-listerine

💾 **Total Commander** lister plugins for **Markdown** rendering and **syntax highlighting**.

⚡ Fast · 🪶 Lightweight · 💪 Memory efficient

---

## 📦 Plugins

### 📝 wlx-listerine-md — Markdown renderer
- Native **Direct2D/DirectWrite** rendering
- **Light** and **dark mode** (follows Windows theme)
- **Syntax-highlighted** code blocks (tree-sitter)
- Clickable **links** (anchors, relative docs, external URLs)
- **Configurable** fonts, spacing, and colors

### 🖍️ wlx-listerine-colorizer — Syntax colorizer
- **Tree-sitter** based tokenization
- Line numbers, indent guides, whitespace markers
- Ships with **25+ grammars** (git, vim, c++, UE and more)
- **Easily extendable** — drop tree-sitter grammar DLLs into `grammars/`
- **Helix-compatible** color themes — drop in themes from the [Helix community](https://github.com/helix-editor/helix/tree/master/runtime/themes)

---

## 🖥️ Requirements

- 🪟 Windows 11
- 💾 Total Commander 11.00+ (64-bit)

## 📥 Installation

1. Download the plugin ZIPs from [**Releases**](../../releases)
2. Open each ZIP in Total Commander — it will offer to auto-install
3. ✅ Done — the plugins work out of the box with built-in defaults

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

### Deferred grammars
- **Markdown** — tree-sitter-markdown uses split block/inline parsers with injection support; needs non-trivial integration
- **Unreal C++** (`taku25/tree-sitter-unreal-cpp`) — exports `tree_sitter_cpp()` which collides with standard C++ grammar; needs custom export wrapper
- ~~**Predicate evaluation**~~ — ✅ implemented (`#eq?`, `#match?`, `#any-of?`, `#not-eq?`, `#not-match?`)
- **Theme modifiers** — Helix themes can specify text modifiers (bold, italic, underline) per scope; currently only foreground/background colors are applied

## 📄 License

[MIT](LICENSE)
