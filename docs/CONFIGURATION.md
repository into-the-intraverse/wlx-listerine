# Configuration Reference

Both plugins look for their `.toml` config file in the same directory as the `.wlx64` file. If no config file is found, built-in defaults are used — the plugins work out of the box.

Each release includes a `.toml.sample` showing all defaults. To customize, rename it to `.toml` and edit.

## Markdown Plugin (wlx-listerine-md.toml)

### [general]

| Key | Default | Description |
|-----|---------|-------------|
| `extensions` | `["md", "markdown", "mdown", "mkd", "mkdn"]` | File extensions to handle |
| `detect_string` | `EXT="MD" \| EXT="MARKDOWN"` | TC detect string |
| `line_numbers` | `true` | Show the line-number gutter |

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
| `search_highlight` | `#FFE066` | `#A87800` | Search match highlight |
| `search_highlight_current` | `#FFA500` | `#E89820` | Current search match |

### [code]

| Key | Default | Description |
|-----|---------|-------------|
| `default_language` | `""` | Default language for unfenced code blocks |

Theme selection and grammar/theme directories now live in the shared
`wlx-listerine-core.toml` — see [wlx-listerine-core.toml](#wlx-listerine-coretoml)
below.

## Colorizer Plugin (wlx-listerine-colorizer.toml)

### [general]

| Key | Default | Description |
|-----|---------|-------------|
| `extensions` | `["c", "cpp", "h", "hpp", "py", "js", ...]` | File extensions to handle (~80 extensions: source languages, shell/git dotfiles, VS/MSBuild XML project files, lockfiles, `txt`) |
| `detect_string` | `EXT="C" \| EXT="CPP" \| ...` | TC detect string |

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

Theme selection lives in the shared `wlx-listerine-core.toml` — see
[wlx-listerine-core.toml](#wlx-listerine-coretoml) below.

### [colorizer]

| Key            | Type     | Default      | Description |
|----------------|----------|--------------|-------------|
| `cpp_grammar`  | string   | `"standard"` | Which tree-sitter grammar handles `.c/.h/.cpp/.cc/.cxx/.hpp/.hxx`. `"standard"` uses upstream tree-sitter-cpp; `"unreal"` uses the taku25 Unreal-aware fork. See [LANGUAGES.md → Switching to Unreal C++](LANGUAGES.md#switching-to-unreal-c). |

## Syntax Color Themes

Theme files live in the `themes/` directory. The install ships `themes/default.toml.sample` and `themes/default_light.toml.sample`; they are *not* loaded as-is. If no `themes/<name>.toml` exists, the plugin falls back to a built-in default that's close to `default.toml.sample` (the same VS Code Dark+/Light+ palette, with hierarchical-fallback covering most scope variants). To customize, rename a sample file (drop the `.sample` suffix) or copy it to a new name.

Themes use the [Helix editor theme format](https://docs.helix-editor.com/themes.html): flat scope-to-style entries, an optional `[palette]` section for named colors, and an `inherits` key for deriving from another theme. A style is either a bare color or a table with modifiers:

```toml
"keyword"           = "#C586C0"
"keyword.directive" = { fg = "#C586C0", modifiers = ["bold"] }
"comment"           = { fg = "#6A9955", modifiers = ["italic"] }
```

Scopes resolve hierarchically — `keyword.directive` falls back to `keyword` when not defined. Common scopes: `keyword`, `function`, `string`, `constant.numeric`, `comment`, `operator`, `type`, `namespace`, `variable`, `punctuation`, `tag`, `attribute`, `markup.*`, `diff.*`. Supported modifiers: `bold`, `italic`, `underline` (also as an `underline = { ... }` table), `strikethrough`; terminal-only Helix modifiers (`reversed`, `dim`, `blink`, `hidden`) are silently ignored, so themes from the [Helix community](https://github.com/helix-editor/helix/tree/master/runtime/themes) can be dropped in unmodified.

To create a custom theme, copy `themes/default.toml.sample` to a new file (e.g. `mytheme.toml`) and set `[theme] dark = "mytheme"` in `wlx-listerine-core.toml`. The `.sample` file itself is overwritten on every install — never edit it directly.

## wlx-listerine-core.toml

Shared by both plugins. Lives next to `wlx-listerine-core.dll` in the install
directory. All values optional — defaults shown.

```toml
[grammar_cache]
cap = 8              # soft LRU cap on loaded grammar DLLs
ttl_minutes = 5      # entries idle longer than this are eviction candidates

[theme]
dark  = "default"    # Helix-format theme used in dark mode
light = ""           # optional light-mode override; "" auto-detects "<dark>_light.toml"
```

`cap` is a *soft* cap: the cache may briefly exceed it if every entry on
the LRU tail is younger than `ttl_minutes`. The eviction sweep runs only
on a miss that pushes the cache above `cap`, and stops at the first fresh
entry from the LRU tail. This means a busy session never thrashes; an
idle session releases stale grammars on the next miss.

Themes live in the shared `wlx-listerine/themes/` directory. Drop additional
Helix-compatible `.toml` files there and reference them by name (without
the `.toml` suffix) in `[theme] dark` / `[theme] light`.
