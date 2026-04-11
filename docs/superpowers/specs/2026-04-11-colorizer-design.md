# wlx-listerine-colorizer: Design Spec

## Overview

A syntax colorizer for Total Commander's WLX lister plugin system, delivered as two artifacts:

1. **`colorizer-core`** (static lib) — tree-sitter-based tokenizer + theme engine. Takes source text + language, returns colored spans. No UI dependencies.
2. **`wlx-listerine-colorizer`** (WLX plugin, `.wlx64`) — standalone syntax-highlighted file viewer for Total Commander.

The existing `wlx-listerine-md` plugin links `colorizer-core` statically to syntax-highlight fenced code blocks.

## Architecture

### Approach: Static lib + two thin WLX shells

```
colorizer-core (STATIC lib)
  GrammarRegistry      — discovers + lazy-loads tree-sitter grammar DLLs
  Tokenizer            — tree-sitter parse, walk syntax tree, emit scope names
  ScopeMapper          — node types -> semantic scopes (per-grammar lookup tables)
  ThemeLoader          — per-language TOML theme files (light/dark palettes)

wlx-listerine-colorizer (WLX plugin)
  host_adapter          — WLX exports, WndProc, scroll, D2D/DWrite setup
  colorizer_layout      — source lines + ColorSpans -> LayoutDocument

wlx-listerine-md (existing, modified)
  layout_engine         — code fence blocks call colorizer-core for colored spans
```

### Public API (`colorizer.h`)

```cpp
struct ColorSpan {
    uint32_t start;      // byte offset in source
    uint32_t length;
    uint32_t color;      // 0xAARRGGBB, resolved from theme
};

struct ColorizeResult {
    std::vector<ColorSpan> spans;  // sorted by start, non-overlapping
};

class Colorizer {
public:
    Colorizer(const std::wstring& grammar_dir, const std::wstring& theme_dir);

    ColorizeResult colorize(const std::string& source,
                            const std::string& language,
                            bool dark_mode) const;

    bool supports(const std::string& language) const;
};
```

## Data Flow

### Standalone colorizer plugin

```
File (.cpp)
  -> FileService::read()
  -> extension -> language name (TOML mapping)
  -> Colorizer::colorize()
  -> colorizer_layout (lines + spans -> LayoutDocument)
  -> RenderEngine::paint()
  -> screen
```

### MD plugin integration

```
Code fence block with language tag
  -> Colorizer::supports(language)? If no, monochrome as before.
  -> Colorizer::colorize(source, language, dark_mode)
  -> Apply ColorSpan as SetColor() ranges on existing IDWriteTextLayout
```

No new files in the md plugin. Modification is limited to `layout_engine.cpp` at the `CodeFence` block layout point.

## Grammar Management

### Tree-sitter dependency

Added to `conanfile.txt`. Tree-sitter runtime (~300KB) links into `colorizer-core`.

### Grammar DLLs

Each language grammar compiles to a separate `tree-sitter-{language}.dll`. Loaded via `LoadLibrary()` on first use for that language. Discovery: scan `grammar_dir` on `Colorizer` construction, index available languages without loading.

### Grammar unload strategy

- **MD plugin:** never unload. Grammars stay loaded for the process lifetime.
- **Standalone plugin:** TBD — start with never-unload, add LRU eviction later if needed.

### Shipped grammars (~27)

**Languages:** C, C++, Python, JavaScript, TypeScript, Rust, Go, Java, C#, Ruby, PHP, Lua

**Shell:** Bash, PowerShell, Vim script

**Markup/Data:** JSON, TOML, YAML, XML, HTML, CSS, Markdown

**Config/Tooling:** Gitconfig, Gitignore, Gitattributes, Dockerfile, CMake, INI, SQL

Users can add more by dropping `tree-sitter-{language}.dll` into the grammars directory.

## Scope Mapping

Tree-sitter node types (e.g., `function_definition`, `string_literal`, `comment`) are mapped to a fixed set of semantic scopes:

`keyword`, `keyword2`, `function`, `string`, `number`, `comment`, `operator`, `type`, `preprocessor`, `namespace`, `variable`, `punctuation`, `plain`

Mapping is per-grammar, hardcoded in lookup tables in `scope_mapper.cpp`. This is stable — tree-sitter grammar node types don't change between versions.

## Theme System

### Structure

```
config/
  themes/
    default.toml       # default theme (light + dark)
    cpp.toml           # C++ specific overrides (optional)
    python.toml        # Python specific (optional)
```

### Theme file format

```toml
[light]
keyword = "#AF00DB"
keyword2 = "#0000FF"
function = "#795E26"
string = "#A31515"
number = "#098658"
comment = "#008000"
operator = "#000000"
type = "#267F99"
preprocessor = "#AF00DB"
namespace = "#267F99"
variable = "#001080"
punctuation = "#000000"
plain = "#1F2328"

[dark]
keyword = "#C586C0"
keyword2 = "#569CD6"
function = "#DCDCAA"
string = "#CE9178"
number = "#B5CEA8"
comment = "#6A9955"
operator = "#D4D4D4"
type = "#4EC9B0"
preprocessor = "#C586C0"
namespace = "#4EC9B0"
variable = "#9CDCFE"
punctuation = "#D4D4D4"
plain = "#D4D4D4"
```

Default theme based on VS Code Dark+/Light+.

### Language-to-theme mapping (in main config)

```toml
[themes]
default = "default"
cpp = "default"
python = "default"
```

If no mapping exists for a language, falls back to `default`.

## Standalone Plugin Config

**`config/wlx-listerine-colorizer.toml`:**

```toml
[general]
extensions = ["c", "cpp", "h", "hpp", "py", "js", "ts", "rs", "go", "java", "cs", "rb", "php", "lua", "sh", "bash", "ps1", "vim", "json", "toml", "yaml", "yml", "xml", "html", "css", "md", "dockerfile", "cmake", "ini", "sql", "gitconfig", "gitignore", "gitattributes"]
detect_string = 'EXT="C" | EXT="CPP" | EXT="H" | EXT="HPP" | EXT="PY" | ...'
grammar_dir = "grammars"
theme_dir = "themes"

[display]
line_numbers = true
word_wrap = false
tab_width = 4

[fonts]
code = "Cascadia Code"
code_size = 13.0

[spacing]
line_height_factor = 1.4

[themes]
default = "default"
```

## MD Plugin Config Addition

Added to `config/wlx-listerine-md.toml`:

```toml
[code]
grammar_dir = "grammars"
theme_dir = "themes"
default_language = ""       # empty = untagged blocks stay monochrome
theme = "default"           # which theme file for code blocks
```

### Graceful degradation

If `grammar_dir` doesn't exist, grammars are missing, or `colorizer-core` can't parse a language, code blocks render monochrome as before. No user-visible errors.

## Rendering (Standalone Plugin)

Configurable via TOML. User controls:
- `line_numbers` — on/off
- `word_wrap` — on/off
- `tab_width` — spaces per tab

Rendering uses the same `RenderEngine` and `ThemeService` from `wlx-core`. The `colorizer_layout` component converts source lines + ColorSpans into a `LayoutDocument` that the existing render engine can paint.

Dark mode detected via DWM (same as md plugin).

## CMake Build Structure

### New targets

```
colorizer-core (STATIC)
  Sources: src/colorizer/colorizer.cpp, grammar_registry.cpp, tokenizer.cpp,
           scope_mapper.cpp, theme_loader.cpp
  Links: tree-sitter (conan), tomlplusplus (conan)

wlx-listerine-colorizer (SHARED, .wlx64)
  Sources: src/colorizer/host_adapter.cpp, colorizer_layout.cpp,
           plugin.def, resource.rc
  Links: colorizer-core, wlx-core (file_service, render_engine, theme_service)
  Output: output/wlx-listerine-colorizer.wlx64

colorizer-tests (EXECUTABLE)
  Sources: tests/colorizer_*.cpp
  Links: colorizer-core, doctest
```

### Updated targets

```
wlx-listerine-md (SHARED)
  Links: wlx-core, colorizer-core  (added)
```

### Source layout

```
src/
  colorizer/
    colorizer.h/.cpp            # public API
    grammar_registry.cpp/.h     # DLL discovery + lazy LoadLibrary
    tokenizer.cpp/.h            # tree-sitter parse + tree walk
    scope_mapper.cpp/.h         # per-grammar node type -> scope tables
    theme_loader.cpp/.h         # TOML theme parsing
    host_adapter.cpp/.h         # standalone plugin WLX exports + WndProc
    colorizer_layout.cpp/.h     # source lines -> LayoutDocument
    plugin.def
    resource.rc
config/
  wlx-listerine-colorizer.toml
  themes/
    default.toml
output/
  wlx-listerine-colorizer.wlx64
  wlx-listerine-colorizer.toml
  themes/default.toml
  grammars/                      # user-provided tree-sitter-*.dll files
```

## Testing

### Unit tests (colorizer-tests)

- **Grammar registry:** DLL discovery, available languages, missing/corrupt DLL handling
- **Tokenizer:** parse source with shipped grammars, verify correct node types
- **Scope mapper:** per-grammar node type -> scope name correctness
- **Theme loader:** TOML parsing, light/dark palettes, missing keys default, malformed files
- **Colorizer end-to-end:** source + language -> ColorSpans with expected colors, sorted, non-overlapping
- **MD integration:** code fence with language tag produces colored IDWriteTextLayout ranges

Tests use shipped grammars (C, Python, JSON, etc.) — no separate test-only grammars.

### Visual regression tests

Extend existing `test_data/cases/` framework:
- MD cases: fenced code blocks with language tags showing syntax-highlighted output
- Standalone cases: source files (.cpp, .py) rendered through screenshot_tool

Same threshold (>= 95% pixel similarity) and workflow (`scripts/visual-test.sh`).

## Out of Scope

- Text selection / copy in colorizer plugin (same limitation as md plugin)
- Incremental re-parsing (tree-sitter supports it, but not needed for a read-only viewer)
- Code folding
- Minimap
- Search/find within file
