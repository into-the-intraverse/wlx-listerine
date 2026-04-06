# WLX Mini Markdown — Design Spec

## Context

The existing `wlx-markdown-viewer-github-style` plugin is a 3-layer C++/C#/.NET 8 AOT architecture using WebView2. It works well but has significant overhead: WebView2 dependency, slow startup for quick Ctrl+Q preview, and a complex codebase.

**wlx-mini-markdown** is a super-lightweight alternative that trades visual polish for speed and simplicity. It targets users who want instant markdown preview in Total Commander without external dependencies.

## Goals

1. **No WebView2** — use Windows built-in RichEdit control
2. **Fast startup** — single DLL, no inter-process communication, no COM initialization
3. **Simple codebase** — ~5 source files, under 2000 LOC total
4. **Maximal markdown coverage** within RichEdit's limitations

## Non-Goals

- Pixel-perfect GitHub-style rendering
- Image embedding (display alt text instead)
- Mermaid.js / diagram rendering
- Translation support
- 32-bit support

---

## Architecture

Single DLL, single layer. No bridge DLLs, no .NET, no browser controls.

```
┌─────────────────────────────────────────────┐
│          wlx-mini-markdown.wlx64            │
│                                             │
│  TC WLX API  →  md4c parser  →  RTF gen    │
│                                             │
│              →  RichEdit control             │
│              →  TOML config loader           │
└─────────────────────────────────────────────┘
```

**Data flow**: File → md4c parse (SAX callbacks) → RTF string builder → EM_STREAMIN → RichEdit displays

**Dependencies** (all statically linked):
- **md4c** (Conan, `md4c/0.5.2`) — CommonMark + GFM markdown parser, MIT license
- **tomlplusplus** (Conan, header-only) — TOML config parser
- **msftedit.dll** — Windows built-in RichEdit 4.1 control (present since Windows XP)

**Deployment**: `.wlx64` file + `.toml` config file.

---

## WLX Plugin Interface

Exported functions (standard TC lister contract):

| Function | Purpose |
|---|---|
| `ListLoad` | Create RichEdit child window, parse markdown file, load RTF |
| `ListLoadNext` | Reuse existing RichEdit window, parse new file, reload RTF |
| `ListCloseWindow` | Destroy RichEdit window, free resources |
| `ListGetDetectString` | Return detect string from config (default: `EXT="MD"`) |
| `ListSearchText` | Forward ANSI search to RichEdit EM_FINDTEXT |
| `ListSearchTextW` | Forward Unicode search to RichEdit EM_FINDTEXTW, highlight matches |
| `ListPrint` | Forward to RichEdit printing support |
| `ListSendCommand` | Handle dark mode toggle (`lcp_darkmode`), copy selection |

### Dark Mode

TC sends `lcp_darkmode` (flag 128) in `ListLoad`'s `ShowFlags` parameter. On receiving it:
1. Switch to dark color palette
2. Re-render RTF with dark colors
3. Set RichEdit background via `EM_SETBKGNDCOLOR`

### Search

RichEdit has built-in `EM_FINDTEXTW`. Forward TC search requests directly. Highlight found text with `EM_SETCHARFORMAT` (background highlight color) on the matched range.

### Clickable Links

1. Enable link detection: `SendMessage(hwnd, EM_SETEVENTMASK, 0, ENM_LINK)`
2. Mark link text with `CFE_LINK` char format in RTF. Store URL in a side-table keyed by character range.
3. Handle `EN_LINK` notification in the parent window proc → look up URL from side-table → `ShellExecuteW` to open in default browser

---

## Markdown → RTF Rendering

md4c uses a SAX-like callback API. We register 4 callbacks (`enter_block`, `leave_block`, `enter_span`, `leave_span`) plus a `text` callback. Each callback appends RTF codes to a string buffer. After parsing completes, the full RTF string is sent to RichEdit via `EM_STREAMIN(SF_RTF)`.

### Block Elements

| Markdown | RTF Rendering |
|---|---|
| `# H1` — `###### H6` | Bold, decreasing font sizes (28pt → 12pt). H1/H2 get a bottom border (`\brdrb\brdrs`) |
| Paragraph | `\par` with spacing (`\sa200`) |
| Bullet list | `\pntext{•}` with left indent (`\li360`) per nesting level |
| Numbered list | `\pntext{N.}` with left indent, auto-incrementing counter |
| Blockquote | Left indent + gray left border (`\brdrbar`) + dimmed text color |
| Fenced code block | Monospace font (Consolas), highlighted background (`\highlight`), preserved whitespace (`\pard\plain`) |
| Horizontal rule | Bottom border on empty paragraph (`\brdrb\brdrs\brdrw10`) |
| Table | Monospace rendering with cell padding via RTF tab stops (`\tqc\tx...`). Column widths calculated from content. Header row bold. |
| Task list | Checkbox characters (`☐` / `☑`) before list item text |

### Inline Elements

| Markdown | RTF |
|---|---|
| `**bold**` | `\b` / `\b0` |
| `*italic*` | `\i` / `\i0` |
| `~~strikethrough~~` | `\strike` / `\strike0` |
| `` `inline code` `` | Monospace font switch + `\highlight` background |
| `[text](url)` | Blue + underline (`\cf{link_color}\ul`), clickable via EN_LINK |
| `![alt](url)` | Render as `[Image: alt text]` in italic (no image embedding) |
| Autolinks | Same as explicit links |

### RTF Document Structure

```rtf
{\rtf1\ansi\deff0
{\fonttbl
  {\f0\fswiss Segoe UI;}
  {\f1\fmodern Consolas;}
}
{\colortbl;
  \red36\green41\blue46;    /* text */
  \red3\green102\blue214;   /* link */
  \red106\green115\blue125; /* blockquote */
  \red246\green248\blue250; /* code bg */
  ...
}
\f0\fs22
... body content ...
}
```

The color table is rebuilt on each render based on the active theme (light/dark).

---

## Configuration

**File**: `wlx-mini-markdown.toml` (next to the DLL)

```toml
[options]
extensions = ["md", "markdown", "mdown", "mkd", "mkdn"]
detect_string = 'EXT="MD" | EXT="MARKDOWN"'

[fonts]
body = "Segoe UI"
body_size = 11
code = "Consolas"
code_size = 10

[colors.light]
background = "#FFFFFF"
text = "#24292E"
heading = "#24292E"
code_background = "#F6F8FA"
blockquote = "#6A737D"
link = "#0366D6"

[colors.dark]
background = "#1E1E1E"
text = "#D4D4D4"
heading = "#E0E0E0"
code_background = "#2D2D2D"
blockquote = "#9E9E9E"
link = "#58A6FF"
```

All values have sensible defaults. The TOML file is optional — if missing, hardcoded defaults are used.

Config is loaded once in `ListLoad` (or on DLL attach) and cached. Path is resolved relative to the DLL location via `GetModuleFileName`.

---

## Project Structure

```
wlx-mini-markdown/
├── CMakeLists.txt              # Build configuration
├── conanfile.txt               # Dependencies: md4c, tomlplusplus
├── CMakePresets.json            # Conan-generated build presets
├── src/
│   ├── plugin.cpp              # WLX exports (ListLoad, ListLoadNext, etc.)
│   ├── plugin.def              # DLL export definitions
│   ├── rtf_builder.h           # RTF builder class declaration
│   ├── rtf_builder.cpp         # md4c callbacks → RTF string conversion
│   ├── config.h                # Config struct and loader declaration
│   ├── config.cpp              # TOML config loading
│   └── resource.h / resource.rc # Version info resource
├── include/
│   └── listerplugin.h         # TC WLX plugin API header (from TC SDK)
├── config/
│   └── wlx-mini-markdown.toml # Default configuration file
└── docs/
    └── superpowers/specs/      # This design doc
```

~5 source files. Target: under 2000 LOC total.

---

## Build System

**CMake + Conan 2.x**

`conanfile.txt`:
```
[requires]
md4c/0.5.2
tomlplusplus/3.4.0

[generators]
CMakeToolchain
CMakeDeps
```

`CMakeLists.txt` key points:
- `add_library(wlx-mini-markdown SHARED ...)` 
- Output name: `wlx-mini-markdown.wlx64` (custom suffix)
- Static linking: `/MT` runtime to avoid MSVCRT dependency
- Link `md4c::md4c` (static library from Conan)
- `tomlplusplus` is header-only, just `find_package` + `target_link_libraries`
- Target: x64 only
- `plugin.def` for controlled symbol exports

**Build commands**:
```bash
conan install . --output-folder=build --build=missing -s build_type=Release
cmake --preset conan-default
cmake --build --preset conan-release
```

---

## Verification Plan

1. **Build**: `conan install` + `cmake` + `cmake --build` succeeds with zero warnings
2. **Load in TC**: Copy `.wlx64` + `.toml` to TC plugin dir, configure in TC options, Ctrl+Q on a `.md` file
3. **Rendering**: Test with a markdown file containing all supported elements:
   - H1–H6 headings (check sizes, H1/H2 borders)
   - Bold, italic, strikethrough, inline code
   - Bullet and numbered lists (nested)
   - Code blocks (fenced with language tag)
   - Blockquotes (nested)
   - Tables (varying column widths)
   - Task lists (checked/unchecked)
   - Links (verify clickable, opens browser)
   - Horizontal rules
   - Images (verify alt text fallback)
4. **Dark mode**: Toggle TC dark mode, verify colors switch correctly
5. **Search**: Ctrl+F in lister, verify text found and highlighted
6. **Config**: Modify `.toml` colors/fonts, reload file, verify changes apply
7. **Edge cases**: Empty file, huge file (>1MB), binary file with .md extension, UTF-8 with emoji
