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
| `default_language` | `""` | Default language for unfenced code blocks |

Theme selection and grammar/theme directories now live in the shared
`wlx-listerine-core.toml` — see [wlx-listerine-core.toml](#wlx-listerine-coretoml)
below.

## Colorizer Plugin (wlx-listerine-colorizer.toml)

### [general]

| Key | Default | Description |
|-----|---------|-------------|
| `extensions` | `["c", "cpp", "h", "hpp", "py", "js", ...]` | File extensions to handle (30+ languages) |
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

To create a custom theme, copy `themes/default.toml` to a new name and set
`[theme] dark = "yourname"` in `wlx-listerine-core.toml`.

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
