# Query-Based Syntax Highlighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the hardcoded node-type scope mapper with tree-sitter highlight queries (.scm), restructure grammar distribution into per-language subdirectories, and expand the scope/palette system from 13 to ~20 variants.

**Architecture:** GrammarRegistry scans subdirectories for DLL + highlights.scm pairs. QueryHighlighter replaces Tokenizer + ScopeMapper by running ts_query_cursor_next_capture() against compiled .scm queries. Capture names map to an expanded Scope enum via longest-prefix matching. Public Colorizer API unchanged.

**Tech Stack:** C++17, tree-sitter 0.25.9 (Conan), toml++ for themes, doctest for tests, CMake 3.20+

**Spec:** `docs/superpowers/specs/2026-04-11-query-highlighter-design.md`

---

## File Map

### New files
| File | Responsibility |
|------|---------------|
| `src/colorizer/scope.h` | Expanded Scope enum, `capture_to_scope()`, `scope_to_color()` |
| `src/colorizer/scope.cpp` | Implementation of capture-to-scope mapping and color conversion |
| `src/colorizer/query_highlighter.h` | QueryHighlighter class declaration |
| `src/colorizer/query_highlighter.cpp` | Query execution, capture iteration, ColorSpan production |
| `tests/test_colorizer_scope_new.cpp` | Tests for capture-to-scope mapping and expanded palette |
| `tests/test_colorizer_query_highlighter.cpp` | Tests for QueryHighlighter with real grammars |
| `scripts/fetch-grammars.sh` | Script to download grammar sources + highlights.scm |

### Modified files
| File | Change |
|------|--------|
| `src/colorizer/theme_loader.h` | Expand SyntaxPalette with ~7 new fields |
| `src/colorizer/theme_loader.cpp` | Expand defaults(), read new TOML keys, fallback chain |
| `src/colorizer/grammar_registry.h` | Add query_source, TSQuery* to GrammarEntry; add parse(), get_query() |
| `src/colorizer/grammar_registry.cpp` | Subdirectory scanning, .scm loading, ; inherits: handling, parse() |
| `src/colorizer/colorizer.cpp` | Wire new pipeline: parse() -> get_query() -> QueryHighlighter::highlight() |
| `src/colorizer/colorizer.h` | Remove ThemeLoader forward decl if unused publicly |
| `src/colorizer/colorizer_host_adapter.cpp` | Expand kExtLangMap, update grammar path checks in skip conditions |
| `tests/test_colorizer.cpp` | Update skip conditions for subdirectory grammar paths |
| `tests/test_colorizer_grammar.cpp` | Update skip conditions and test subdirectory discovery |
| `tests/test_colorizer_theme.cpp` | Add tests for new palette fields |
| `CMakeLists.txt` | Update source lists, grammar build targets, post-build copy |

### Deleted files
| File | Reason |
|------|--------|
| `src/colorizer/tokenizer.h` | Replaced by QueryHighlighter |
| `src/colorizer/tokenizer.cpp` | Replaced by QueryHighlighter |
| `src/colorizer/scope_mapper.h` | Replaced by scope.h |
| `src/colorizer/scope_mapper.cpp` | Replaced by scope.cpp |
| `tests/test_colorizer_scope.cpp` | Replaced by test_colorizer_scope_new.cpp |
| `tests/test_colorizer_tokenizer.cpp` | Replaced by test_colorizer_query_highlighter.cpp |

---

### Task 1: Expand Scope Enum and Capture Mapping

**Files:**
- Create: `src/colorizer/scope.h`
- Create: `src/colorizer/scope.cpp`
- Create: `tests/test_colorizer_scope_new.cpp`

- [ ] **Step 1: Write tests for capture-to-scope mapping**

Create `tests/test_colorizer_scope_new.cpp`:

```cpp
#include <doctest/doctest.h>
#include "scope.h"

// --- Exact matches ---

TEST_CASE("capture_to_scope maps keyword to Keyword") {
    CHECK(capture_to_scope("keyword") == Scope::Keyword);
}

TEST_CASE("capture_to_scope maps function to Function") {
    CHECK(capture_to_scope("function") == Scope::Function);
}

TEST_CASE("capture_to_scope maps function.builtin to FunctionBuiltin") {
    CHECK(capture_to_scope("function.builtin") == Scope::FunctionBuiltin);
}

TEST_CASE("capture_to_scope maps function.call to FunctionCall") {
    CHECK(capture_to_scope("function.call") == Scope::FunctionCall);
}

TEST_CASE("capture_to_scope maps string to String") {
    CHECK(capture_to_scope("string") == Scope::String);
}

TEST_CASE("capture_to_scope maps string.escape to StringEscape") {
    CHECK(capture_to_scope("string.escape") == Scope::StringEscape);
}

TEST_CASE("capture_to_scope maps string.special to StringSpecial") {
    CHECK(capture_to_scope("string.special") == Scope::StringSpecial);
}

TEST_CASE("capture_to_scope maps string.regexp to StringSpecial") {
    CHECK(capture_to_scope("string.regexp") == Scope::StringSpecial);
}

TEST_CASE("capture_to_scope maps boolean to Boolean") {
    CHECK(capture_to_scope("boolean") == Scope::Boolean);
}

TEST_CASE("capture_to_scope maps number to Number") {
    CHECK(capture_to_scope("number") == Scope::Number);
}

TEST_CASE("capture_to_scope maps comment to Comment") {
    CHECK(capture_to_scope("comment") == Scope::Comment);
}

TEST_CASE("capture_to_scope maps operator to Operator") {
    CHECK(capture_to_scope("operator") == Scope::Operator);
}

TEST_CASE("capture_to_scope maps type to Type") {
    CHECK(capture_to_scope("type") == Scope::Type);
}

TEST_CASE("capture_to_scope maps type.builtin to Keyword2") {
    CHECK(capture_to_scope("type.builtin") == Scope::Keyword2);
}

TEST_CASE("capture_to_scope maps constant.builtin to ConstantBuiltin") {
    CHECK(capture_to_scope("constant.builtin") == Scope::ConstantBuiltin);
}

TEST_CASE("capture_to_scope maps constructor to Constructor") {
    CHECK(capture_to_scope("constructor") == Scope::Constructor);
}

TEST_CASE("capture_to_scope maps property to Property") {
    CHECK(capture_to_scope("property") == Scope::Property);
}

TEST_CASE("capture_to_scope maps variable to Variable") {
    CHECK(capture_to_scope("variable") == Scope::Variable);
}

TEST_CASE("capture_to_scope maps label to Label") {
    CHECK(capture_to_scope("label") == Scope::Label);
}

TEST_CASE("capture_to_scope maps module to Namespace") {
    CHECK(capture_to_scope("module") == Scope::Namespace);
}

TEST_CASE("capture_to_scope maps tag to Tag") {
    CHECK(capture_to_scope("tag") == Scope::Tag);
}

TEST_CASE("capture_to_scope maps tag.delimiter to TagDelimiter") {
    CHECK(capture_to_scope("tag.delimiter") == Scope::TagDelimiter);
}

TEST_CASE("capture_to_scope maps attribute to Attribute") {
    CHECK(capture_to_scope("attribute") == Scope::Attribute);
}

TEST_CASE("capture_to_scope maps keyword.directive to Preprocessor") {
    CHECK(capture_to_scope("keyword.directive") == Scope::Preprocessor);
}

TEST_CASE("capture_to_scope maps keyword.directive.define to Preprocessor") {
    CHECK(capture_to_scope("keyword.directive.define") == Scope::Preprocessor);
}

TEST_CASE("capture_to_scope maps punctuation.bracket to Punctuation") {
    CHECK(capture_to_scope("punctuation.bracket") == Scope::Punctuation);
}

TEST_CASE("capture_to_scope maps punctuation.delimiter to Punctuation") {
    CHECK(capture_to_scope("punctuation.delimiter") == Scope::Punctuation);
}

// --- Prefix fallback ---

TEST_CASE("capture_to_scope falls back keyword.return to Keyword") {
    CHECK(capture_to_scope("keyword.return") == Scope::Keyword);
}

TEST_CASE("capture_to_scope falls back keyword.conditional to Keyword") {
    CHECK(capture_to_scope("keyword.conditional") == Scope::Keyword);
}

TEST_CASE("capture_to_scope falls back function.method to Function") {
    CHECK(capture_to_scope("function.method") == Scope::Function);
}

TEST_CASE("capture_to_scope falls back string.documentation to String") {
    CHECK(capture_to_scope("string.documentation") == Scope::String);
}

TEST_CASE("capture_to_scope falls back comment.documentation to Comment") {
    CHECK(capture_to_scope("comment.documentation") == Scope::Comment);
}

TEST_CASE("capture_to_scope falls back variable.parameter to Variable") {
    CHECK(capture_to_scope("variable.parameter") == Scope::Variable);
}

TEST_CASE("capture_to_scope falls back number.float to Number") {
    CHECK(capture_to_scope("number.float") == Scope::Number);
}

TEST_CASE("capture_to_scope falls back type.definition to Type") {
    CHECK(capture_to_scope("type.definition") == Scope::Type);
}

TEST_CASE("capture_to_scope maps character to String") {
    CHECK(capture_to_scope("character") == Scope::String);
}

TEST_CASE("capture_to_scope maps tag.attribute to Attribute") {
    CHECK(capture_to_scope("tag.attribute") == Scope::Attribute);
}

TEST_CASE("capture_to_scope maps tag.builtin to Tag") {
    CHECK(capture_to_scope("tag.builtin") == Scope::Tag);
}

// --- Unknown ---

TEST_CASE("capture_to_scope maps unknown capture to Plain") {
    CHECK(capture_to_scope("totally_unknown") == Scope::Plain);
}

TEST_CASE("capture_to_scope maps empty string to Plain") {
    CHECK(capture_to_scope("") == Scope::Plain);
}

// --- scope_to_color ---

TEST_CASE("scope_to_color returns correct color for all expanded scopes") {
    SyntaxPalette pal = SyntaxPalette::defaults(false);
    CHECK(scope_to_color(Scope::Keyword, pal) == pal.keyword);
    CHECK(scope_to_color(Scope::Keyword2, pal) == pal.keyword2);
    CHECK(scope_to_color(Scope::Function, pal) == pal.function);
    CHECK(scope_to_color(Scope::FunctionBuiltin, pal) == pal.function_builtin);
    CHECK(scope_to_color(Scope::FunctionCall, pal) == pal.function_call);
    CHECK(scope_to_color(Scope::String, pal) == pal.string);
    CHECK(scope_to_color(Scope::StringEscape, pal) == pal.string_escape);
    CHECK(scope_to_color(Scope::StringSpecial, pal) == pal.string_special);
    CHECK(scope_to_color(Scope::Number, pal) == pal.number);
    CHECK(scope_to_color(Scope::Boolean, pal) == pal.boolean_lit);
    CHECK(scope_to_color(Scope::Comment, pal) == pal.comment);
    CHECK(scope_to_color(Scope::Operator, pal) == pal.op);
    CHECK(scope_to_color(Scope::Type, pal) == pal.type);
    CHECK(scope_to_color(Scope::ConstantBuiltin, pal) == pal.constant_builtin);
    CHECK(scope_to_color(Scope::Constructor, pal) == pal.constructor);
    CHECK(scope_to_color(Scope::Preprocessor, pal) == pal.preprocessor);
    CHECK(scope_to_color(Scope::Namespace, pal) == pal.ns);
    CHECK(scope_to_color(Scope::Variable, pal) == pal.variable);
    CHECK(scope_to_color(Scope::Property, pal) == pal.property);
    CHECK(scope_to_color(Scope::Label, pal) == pal.label);
    CHECK(scope_to_color(Scope::Punctuation, pal) == pal.punctuation);
    CHECK(scope_to_color(Scope::Tag, pal) == pal.tag);
    CHECK(scope_to_color(Scope::TagDelimiter, pal) == pal.tag_delimiter);
    CHECK(scope_to_color(Scope::Attribute, pal) == pal.attribute);
    CHECK(scope_to_color(Scope::Plain, pal) == pal.plain);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset conan-release --target colorizer-tests && ./build/Release/colorizer-tests.exe -tc="capture_to_scope*"`
Expected: Compilation failure — `scope.h` does not exist yet.

- [ ] **Step 3: Create scope.h**

Create `src/colorizer/scope.h`:

```cpp
#pragma once

#include "theme_loader.h"
#include <string>
#include <string_view>

enum class Scope {
    // Original 13
    Keyword,
    Keyword2,
    Function,
    String,
    Number,
    Comment,
    Operator,
    Type,
    Preprocessor,
    Namespace,
    Variable,
    Punctuation,
    Plain,
    // Expanded
    ConstantBuiltin,
    FunctionBuiltin,
    FunctionCall,
    StringEscape,
    StringSpecial,
    Boolean,
    Tag,
    TagDelimiter,
    Attribute,
    Constructor,
    Property,
    Label,
};

// Map a tree-sitter capture name (without @) to a Scope.
// Uses longest-prefix matching: "keyword.return" -> Keyword.
Scope capture_to_scope(std::string_view capture_name);

// Map a Scope to an RGB color from the palette.
uint32_t scope_to_color(Scope scope, const SyntaxPalette& palette);
```

- [ ] **Step 4: Expand SyntaxPalette in theme_loader.h**

Add new fields after the existing 13 in `SyntaxPalette` (in `src/colorizer/theme_loader.h`):

```cpp
struct SyntaxPalette {
    // Original 13
    uint32_t keyword = 0;
    uint32_t keyword2 = 0;
    uint32_t function = 0;
    uint32_t string = 0;
    uint32_t number = 0;
    uint32_t comment = 0;
    uint32_t op = 0;
    uint32_t type = 0;
    uint32_t preprocessor = 0;
    uint32_t ns = 0;
    uint32_t variable = 0;
    uint32_t punctuation = 0;
    uint32_t plain = 0;
    // Expanded
    uint32_t constant_builtin = 0;
    uint32_t function_builtin = 0;
    uint32_t function_call = 0;
    uint32_t string_escape = 0;
    uint32_t string_special = 0;
    uint32_t boolean_lit = 0;     // "boolean" is not a C++ keyword, but boolean_lit is clearer
    uint32_t tag = 0;
    uint32_t tag_delimiter = 0;
    uint32_t attribute = 0;
    uint32_t constructor = 0;
    uint32_t property = 0;
    uint32_t label = 0;

    static SyntaxPalette defaults(bool dark_mode);
};
```

- [ ] **Step 5: Update SyntaxPalette::defaults() in theme_loader.cpp**

Add new default colors after the existing 13 in both light and dark branches. New scopes default to their fallback scope's color (differentiated slightly where helpful):

```cpp
// Light mode — after existing fields:
pal.constant_builtin = 0x0000FF;   // same as keyword2
pal.function_builtin = 0x795E26;   // same as function
pal.function_call    = 0x795E26;   // same as function
pal.string_escape    = 0xEE0000;   // brighter red
pal.string_special   = 0x811F3F;   // distinct from string
pal.boolean_lit      = 0x0000FF;   // same as keyword2
pal.tag              = 0x800000;   // maroon
pal.tag_delimiter    = 0x800000;   // same as tag
pal.attribute        = 0xFF0000;   // red
pal.constructor      = 0x267F99;   // same as type
pal.property         = 0x001080;   // same as variable
pal.label            = 0x001080;   // same as variable

// Dark mode — after existing fields:
pal.constant_builtin = 0x569CD6;   // same as keyword2
pal.function_builtin = 0xDCDCAA;   // same as function
pal.function_call    = 0xDCDCAA;   // same as function
pal.string_escape    = 0xD7BA7D;   // golden
pal.string_special   = 0xD16969;   // reddish
pal.boolean_lit      = 0x569CD6;   // same as keyword2
pal.tag              = 0x569CD6;   // blue
pal.tag_delimiter    = 0x808080;   // gray
pal.attribute        = 0x9CDCFE;   // light blue
pal.constructor      = 0x4EC9B0;   // same as type
pal.property         = 0x9CDCFE;   // same as variable
pal.label            = 0x9CDCFE;   // same as variable
```

- [ ] **Step 6: Create scope.cpp**

Create `src/colorizer/scope.cpp`:

```cpp
#include "scope.h"
#include <unordered_map>

// Exact capture name -> Scope mapping.
// More specific entries (e.g. "keyword.directive") are checked before
// prefix fallback strips suffixes.
static const std::unordered_map<std::string, Scope> g_capture_map = {
    // Keywords
    {"keyword",                      Scope::Keyword},
    {"keyword.directive",            Scope::Preprocessor},
    {"keyword.directive.define",     Scope::Preprocessor},

    // Functions
    {"function",                     Scope::Function},
    {"function.builtin",             Scope::FunctionBuiltin},
    {"function.call",                Scope::FunctionCall},
    {"function.method",              Scope::Function},
    {"function.method.call",         Scope::FunctionCall},
    {"function.macro",               Scope::Function},

    // Strings
    {"string",                       Scope::String},
    {"string.escape",                Scope::StringEscape},
    {"string.special",               Scope::StringSpecial},
    {"string.regexp",                Scope::StringSpecial},
    {"string.documentation",         Scope::String},
    {"string.special.symbol",        Scope::StringSpecial},
    {"string.special.url",           Scope::StringSpecial},
    {"string.special.path",          Scope::StringSpecial},
    {"character",                    Scope::String},
    {"character.special",            Scope::StringEscape},

    // Numerics
    {"number",                       Scope::Number},
    {"number.float",                 Scope::Number},
    {"boolean",                      Scope::Boolean},

    // Comments
    {"comment",                      Scope::Comment},
    {"comment.documentation",        Scope::Comment},

    // Operators
    {"operator",                     Scope::Operator},

    // Types
    {"type",                         Scope::Type},
    {"type.builtin",                 Scope::Keyword2},
    {"type.definition",              Scope::Type},

    // Constants
    {"constant",                     Scope::Variable},
    {"constant.builtin",             Scope::ConstantBuiltin},
    {"constant.macro",               Scope::Variable},

    // Constructors
    {"constructor",                  Scope::Constructor},

    // Namespace / modules
    {"module",                       Scope::Namespace},
    {"module.builtin",               Scope::Namespace},
    {"namespace",                    Scope::Namespace},

    // Variables
    {"variable",                     Scope::Variable},
    {"variable.builtin",             Scope::Variable},
    {"variable.parameter",           Scope::Variable},
    {"variable.member",              Scope::Variable},

    // Properties
    {"property",                     Scope::Property},

    // Labels
    {"label",                        Scope::Label},

    // Punctuation
    {"punctuation",                  Scope::Punctuation},
    {"punctuation.bracket",          Scope::Punctuation},
    {"punctuation.delimiter",        Scope::Punctuation},
    {"punctuation.special",          Scope::Punctuation},

    // Tags (HTML/XML)
    {"tag",                          Scope::Tag},
    {"tag.builtin",                  Scope::Tag},
    {"tag.delimiter",                Scope::TagDelimiter},
    {"tag.attribute",                Scope::Attribute},

    // Attributes
    {"attribute",                    Scope::Attribute},
    {"attribute.builtin",            Scope::Attribute},

    // Preprocessor / directives
    {"preproc",                      Scope::Preprocessor},
};

Scope capture_to_scope(std::string_view name) {
    if (name.empty()) return Scope::Plain;

    // Try exact match first
    std::string key(name);
    auto it = g_capture_map.find(key);
    if (it != g_capture_map.end())
        return it->second;

    // Strip suffixes one dot at a time for prefix fallback
    while (true) {
        auto dot = key.rfind('.');
        if (dot == std::string::npos)
            break;
        key.resize(dot);
        it = g_capture_map.find(key);
        if (it != g_capture_map.end())
            return it->second;
    }

    return Scope::Plain;
}

uint32_t scope_to_color(Scope scope, const SyntaxPalette& palette) {
    switch (scope) {
        case Scope::Keyword:         return palette.keyword;
        case Scope::Keyword2:        return palette.keyword2;
        case Scope::Function:        return palette.function;
        case Scope::FunctionBuiltin: return palette.function_builtin;
        case Scope::FunctionCall:    return palette.function_call;
        case Scope::String:          return palette.string;
        case Scope::StringEscape:    return palette.string_escape;
        case Scope::StringSpecial:   return palette.string_special;
        case Scope::Number:          return palette.number;
        case Scope::Boolean:         return palette.boolean_lit;
        case Scope::Comment:         return palette.comment;
        case Scope::Operator:        return palette.op;
        case Scope::Type:            return palette.type;
        case Scope::ConstantBuiltin: return palette.constant_builtin;
        case Scope::Constructor:     return palette.constructor;
        case Scope::Preprocessor:    return palette.preprocessor;
        case Scope::Namespace:       return palette.ns;
        case Scope::Variable:        return palette.variable;
        case Scope::Property:        return palette.property;
        case Scope::Label:           return palette.label;
        case Scope::Punctuation:     return palette.punctuation;
        case Scope::Tag:             return palette.tag;
        case Scope::TagDelimiter:    return palette.tag_delimiter;
        case Scope::Attribute:       return palette.attribute;
        case Scope::Plain:           return palette.plain;
    }
    return palette.plain;
}
```

- [ ] **Step 7: Add to CMake and run tests**

In `CMakeLists.txt`, add `src/colorizer/scope.cpp` to `colorizer-core` sources and `tests/test_colorizer_scope_new.cpp` to `colorizer-tests` sources (keeping old test files for now).

Run: `cmake --build --preset conan-release --target colorizer-tests && ./build/Release/colorizer-tests.exe -tc="capture_to_scope*,scope_to_color*"`
Expected: All new tests PASS.

- [ ] **Step 8: Commit**

```bash
git add src/colorizer/scope.h src/colorizer/scope.cpp tests/test_colorizer_scope_new.cpp CMakeLists.txt src/colorizer/theme_loader.h src/colorizer/theme_loader.cpp
git commit -m "feat: expanded Scope enum with capture-to-scope mapping"
```

---

### Task 2: Restructure Grammar Directory

**Files:**
- Modify: `grammars/` directory structure (move DLLs into subdirectories)
- Create: `grammars/c/highlights.scm`, `grammars/json/highlights.scm`, `grammars/python/highlights.scm`
- Create: `scripts/fetch-grammars.sh`

- [ ] **Step 1: Create subdirectory structure and move existing DLLs**

```bash
cd grammars
mkdir -p c json python
mv tree-sitter-c.dll c/
mv tree-sitter-json.dll json/
mv tree-sitter-python.dll python/
```

- [ ] **Step 2: Download highlights.scm for each grammar**

```bash
cd grammars
curl -sL https://raw.githubusercontent.com/tree-sitter/tree-sitter-c/master/queries/highlights.scm -o c/highlights.scm
curl -sL https://raw.githubusercontent.com/tree-sitter/tree-sitter-json/master/queries/highlights.scm -o json/highlights.scm
curl -sL https://raw.githubusercontent.com/tree-sitter/tree-sitter-python/master/queries/highlights.scm -o python/highlights.scm
```

- [ ] **Step 3: Verify directory structure**

```bash
find grammars -type f | sort
```

Expected:
```
grammars/c/highlights.scm
grammars/c/tree-sitter-c.dll
grammars/json/highlights.scm
grammars/json/tree-sitter-json.dll
grammars/python/highlights.scm
grammars/python/tree-sitter-python.dll
```

- [ ] **Step 4: Create fetch-grammars.sh for future use**

Create `scripts/fetch-grammars.sh` — a script that clones grammar repos, compiles DLLs, and copies highlights.scm. This is the reference for adding new grammars:

```bash
#!/usr/bin/env bash
set -euo pipefail

# Usage: ./scripts/fetch-grammars.sh [language...]
# Without args, fetches all starter grammars.
# With args, fetches only specified languages.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
GRAMMAR_OUT="$ROOT_DIR/grammars"
BUILD_DIR="$ROOT_DIR/build/grammars"

declare -A GRAMMAR_REPOS=(
    [c]="tree-sitter/tree-sitter-c"
    [cpp]="tree-sitter/tree-sitter-cpp"
    [python]="tree-sitter/tree-sitter-python"
    [javascript]="tree-sitter/tree-sitter-javascript"
    [typescript]="tree-sitter/tree-sitter-typescript"
    [rust]="tree-sitter/tree-sitter-rust"
    [go]="tree-sitter/tree-sitter-go"
    [java]="tree-sitter/tree-sitter-java"
    [c-sharp]="tree-sitter/tree-sitter-c-sharp"
    [json]="tree-sitter/tree-sitter-json"
    [html]="tree-sitter/tree-sitter-html"
    [css]="tree-sitter/tree-sitter-css"
    [bash]="tree-sitter/tree-sitter-bash"
    [toml]="tree-sitter-grammars/tree-sitter-toml"
    [yaml]="tree-sitter-grammars/tree-sitter-yaml"
    [lua]="MunifTanjim/tree-sitter-lua"
    [php]="tree-sitter/tree-sitter-php"
    [powershell]="airbus-cert/tree-sitter-powershell"
    [vim]="neovim/tree-sitter-vim"
    [dockerfile]="camdencheek/tree-sitter-dockerfile"
    [cmake]="uyha/tree-sitter-cmake"
    [markdown]="MDeiml/tree-sitter-markdown"
)

# Grammars where highlights.scm is in a subdirectory
declare -A QUERY_SUBDIR=(
    [vim]="queries/vim"
)

LANGS=("$@")
if [ ${#LANGS[@]} -eq 0 ]; then
    LANGS=("${!GRAMMAR_REPOS[@]}")
fi

for lang in "${LANGS[@]}"; do
    repo="${GRAMMAR_REPOS[$lang]:-}"
    if [ -z "$repo" ]; then
        echo "Unknown language: $lang"
        continue
    fi

    echo "--- Fetching $lang from $repo ---"
    clone_dir="$BUILD_DIR/$lang"

    if [ ! -d "$clone_dir" ]; then
        git clone --depth 1 "https://github.com/$repo.git" "$clone_dir"
    else
        git -C "$clone_dir" pull --ff-only 2>/dev/null || true
    fi

    # Copy highlights.scm
    query_dir="${QUERY_SUBDIR[$lang]:-queries}"
    mkdir -p "$GRAMMAR_OUT/$lang"
    if [ -f "$clone_dir/$query_dir/highlights.scm" ]; then
        cp "$clone_dir/$query_dir/highlights.scm" "$GRAMMAR_OUT/$lang/highlights.scm"
        echo "  -> highlights.scm copied"
    else
        echo "  WARNING: no highlights.scm found at $query_dir/highlights.scm"
    fi
done

echo "Done. Grammar sources are in $BUILD_DIR. Run CMake to compile DLLs."
```

- [ ] **Step 5: Commit**

```bash
git add grammars/ scripts/fetch-grammars.sh
git commit -m "refactor: restructure grammars into per-language subdirectories"
```

---

### Task 3: Update GrammarRegistry for Subdirectories and Queries

**Files:**
- Modify: `src/colorizer/grammar_registry.h`
- Modify: `src/colorizer/grammar_registry.cpp`
- Modify: `tests/test_colorizer_grammar.cpp`

- [ ] **Step 1: Update grammar registry tests**

Rewrite `tests/test_colorizer_grammar.cpp` for subdirectory structure:

```cpp
#include <doctest/doctest.h>
#include <filesystem>
#include "grammar_registry.h"

TEST_CASE("GrammarRegistry with nonexistent dir has no languages") {
    GrammarRegistry reg(L"nonexistent_grammar_dir");
    CHECK(reg.available_languages().empty());
    CHECK_FALSE(reg.supports("c"));
}

TEST_CASE("GrammarRegistry get_grammar for unsupported language returns nullptr") {
    GrammarRegistry reg(L"nonexistent_grammar_dir");
    CHECK(reg.get_grammar("c") == nullptr);
}

TEST_CASE("GrammarRegistry get_query for unsupported language returns nullptr") {
    GrammarRegistry reg(L"nonexistent_grammar_dir");
    CHECK(reg.get_query("c") == nullptr);
}

TEST_CASE("GrammarRegistry discovers subdirectory grammars"
    * doctest::skip(!std::filesystem::exists("grammars/c/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    CHECK(reg.supports("c"));
    CHECK(reg.supports("json"));
    CHECK(reg.supports("python"));
}

TEST_CASE("GrammarRegistry loads C grammar from subdirectory"
    * doctest::skip(!std::filesystem::exists("grammars/c/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto* lang = reg.get_grammar("c");
    REQUIRE(lang != nullptr);
}

TEST_CASE("GrammarRegistry compiles query from highlights.scm"
    * doctest::skip(!std::filesystem::exists("grammars/c/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto* query = reg.get_query("c");
    REQUIRE(query != nullptr);
}

TEST_CASE("GrammarRegistry parse produces a tree"
    * doctest::skip(!std::filesystem::exists("grammars/c/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto* tree = reg.parse("c", "int x = 1;");
    REQUIRE(tree != nullptr);
    ts_tree_delete(tree);
}

TEST_CASE("GrammarRegistry parse returns nullptr for unsupported language") {
    GrammarRegistry reg(L"nonexistent_grammar_dir");
    CHECK(reg.parse("c", "int x;") == nullptr);
}

TEST_CASE("GrammarRegistry available_languages lists all discovered grammars"
    * doctest::skip(!std::filesystem::exists("grammars/c/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto langs = reg.available_languages();
    CHECK_FALSE(langs.empty());
    for (size_t i = 1; i < langs.size(); i++) {
        CHECK(langs[i] > langs[i - 1]);
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset conan-release --target colorizer-tests && ./build/Release/colorizer-tests.exe -tc="GrammarRegistry*"`
Expected: Compilation failure — `get_query()` and `parse()` don't exist yet.

- [ ] **Step 3: Update grammar_registry.h**

Replace `src/colorizer/grammar_registry.h`:

```cpp
#pragma once

#include <tree_sitter/api.h>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class GrammarRegistry {
public:
    explicit GrammarRegistry(const std::wstring& grammar_dir);
    ~GrammarRegistry();

    GrammarRegistry(const GrammarRegistry&) = delete;
    GrammarRegistry& operator=(const GrammarRegistry&) = delete;

    bool supports(const std::string& language) const;
    std::vector<std::string> available_languages() const;

    // Returns nullptr if language not available or DLL fails to load.
    const TSLanguage* get_grammar(const std::string& language);

    // Returns compiled highlight query, or nullptr. Lazily compiled on first call.
    // Returned pointer valid for lifetime of this GrammarRegistry.
    const TSQuery* get_query(const std::string& language);

    // Parse source code. Caller owns the returned TSTree (must call ts_tree_delete).
    // Returns nullptr on failure.
    TSTree* parse(const std::string& language, const std::string& source);

private:
    void scan_directory();
    std::string resolve_query_source(const std::string& language, int depth = 0);

    std::wstring grammar_dir_;

    struct GrammarEntry {
        std::wstring dll_path;
        std::string query_source;     // raw highlights.scm content
        HMODULE handle = nullptr;
        const TSLanguage* language = nullptr;
        TSQuery* query = nullptr;
        bool load_attempted = false;
        bool query_compiled = false;
    };

    std::unordered_map<std::string, GrammarEntry> grammars_;
};
```

- [ ] **Step 4: Update grammar_registry.cpp**

Replace `src/colorizer/grammar_registry.cpp`:

```cpp
#define NOMINMAX
#include "grammar_registry.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

using GrammarFn = const TSLanguage* (*)();

GrammarRegistry::GrammarRegistry(const std::wstring& grammar_dir)
    : grammar_dir_(grammar_dir) {
    scan_directory();
}

GrammarRegistry::~GrammarRegistry() {
    for (auto& [name, entry] : grammars_) {
        if (entry.query)
            ts_query_delete(entry.query);
        if (entry.handle)
            FreeLibrary(entry.handle);
    }
}

void GrammarRegistry::scan_directory() {
    std::error_code ec;
    if (!fs::is_directory(grammar_dir_, ec))
        return;

    for (auto& dir_entry : fs::directory_iterator(grammar_dir_, ec)) {
        if (!dir_entry.is_directory()) continue;

        std::string lang = dir_entry.path().filename().string();

        // Find tree-sitter-*.dll in subdirectory
        std::wstring dll_path;
        std::string query_src;

        for (auto& file : fs::directory_iterator(dir_entry.path(), ec)) {
            if (!file.is_regular_file()) continue;
            auto fname = file.path().filename().string();

            if (fname.size() >= 16 &&
                fname.substr(0, 12) == "tree-sitter-" &&
                fname.substr(fname.size() - 4) == ".dll") {
                dll_path = file.path().wstring();
            }
            else if (fname == "highlights.scm") {
                std::ifstream ifs(file.path());
                if (ifs) {
                    std::ostringstream ss;
                    ss << ifs.rdbuf();
                    query_src = ss.str();
                }
            }
        }

        if (!dll_path.empty()) {
            GrammarEntry ge;
            ge.dll_path = dll_path;
            ge.query_source = query_src;
            grammars_[lang] = ge;
        }
    }
}

bool GrammarRegistry::supports(const std::string& language) const {
    return grammars_.find(language) != grammars_.end();
}

std::vector<std::string> GrammarRegistry::available_languages() const {
    std::vector<std::string> result;
    result.reserve(grammars_.size());
    for (auto& [name, entry] : grammars_)
        result.push_back(name);
    std::sort(result.begin(), result.end());
    return result;
}

const TSLanguage* GrammarRegistry::get_grammar(const std::string& language) {
    auto it = grammars_.find(language);
    if (it == grammars_.end()) return nullptr;

    auto& entry = it->second;
    if (entry.language) return entry.language;
    if (entry.load_attempted) return nullptr;

    entry.load_attempted = true;
    entry.handle = LoadLibraryW(entry.dll_path.c_str());
    if (!entry.handle) return nullptr;

    std::string fn_name = "tree_sitter_" + language;
    std::replace(fn_name.begin(), fn_name.end(), '-', '_');

    auto fn = reinterpret_cast<GrammarFn>(GetProcAddress(entry.handle, fn_name.c_str()));
    if (!fn) {
        FreeLibrary(entry.handle);
        entry.handle = nullptr;
        return nullptr;
    }

    entry.language = fn();
    return entry.language;
}

std::string GrammarRegistry::resolve_query_source(const std::string& language, int depth) {
    if (depth > 5) return {};  // prevent infinite recursion

    auto it = grammars_.find(language);
    if (it == grammars_.end()) return {};

    const std::string& src = it->second.query_source;
    if (src.empty()) return {};

    std::string result;

    // Check for ; inherits: directive on first line
    if (src.size() > 12 && src.substr(0, 12) == "; inherits: ") {
        auto eol = src.find('\n');
        std::string inherits_line = (eol != std::string::npos)
            ? src.substr(12, eol - 12) : src.substr(12);

        // Parse comma-separated language list (ignore optional parens)
        std::istringstream iss(inherits_line);
        std::string parent;
        while (std::getline(iss, parent, ',')) {
            // Trim whitespace and optional parens
            auto start = parent.find_first_not_of(" \t(");
            auto end = parent.find_last_not_of(" \t)");
            if (start != std::string::npos && end != std::string::npos) {
                std::string parent_lang = parent.substr(start, end - start + 1);
                result += resolve_query_source(parent_lang, depth + 1);
                result += "\n";
            }
        }

        // Append the rest of the file (after the inherits line)
        if (eol != std::string::npos && eol + 1 < src.size())
            result += src.substr(eol + 1);
    } else {
        result = src;
    }

    return result;
}

const TSQuery* GrammarRegistry::get_query(const std::string& language) {
    auto it = grammars_.find(language);
    if (it == grammars_.end()) return nullptr;

    auto& entry = it->second;
    if (entry.query) return entry.query;
    if (entry.query_compiled) return nullptr;  // previous attempt failed

    // Need the grammar loaded first
    const TSLanguage* lang = get_grammar(language);
    if (!lang) return nullptr;

    entry.query_compiled = true;

    std::string resolved = resolve_query_source(language);
    if (resolved.empty()) return nullptr;

    uint32_t err_offset = 0;
    TSQueryError err_type = TSQueryErrorNone;
    entry.query = ts_query_new(lang, resolved.c_str(),
                               static_cast<uint32_t>(resolved.size()),
                               &err_offset, &err_type);

    return entry.query;
}

TSTree* GrammarRegistry::parse(const std::string& language, const std::string& source) {
    const TSLanguage* lang = get_grammar(language);
    if (!lang) return nullptr;

    TSParser* parser = ts_parser_new();
    if (!parser) return nullptr;

    if (!ts_parser_set_language(parser, lang)) {
        ts_parser_delete(parser);
        return nullptr;
    }

    TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                           source.c_str(),
                                           static_cast<uint32_t>(source.size()));
    ts_parser_delete(parser);
    return tree;
}
```

- [ ] **Step 5: Run tests**

Run: `cmake --build --preset conan-release --target colorizer-tests && ./build/Release/colorizer-tests.exe -tc="GrammarRegistry*"`
Expected: All GrammarRegistry tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/colorizer/grammar_registry.h src/colorizer/grammar_registry.cpp tests/test_colorizer_grammar.cpp
git commit -m "feat: GrammarRegistry subdirectory scan with query loading"
```

---

### Task 4: QueryHighlighter

**Files:**
- Create: `src/colorizer/query_highlighter.h`
- Create: `src/colorizer/query_highlighter.cpp`
- Create: `tests/test_colorizer_query_highlighter.cpp`

- [ ] **Step 1: Write tests**

Create `tests/test_colorizer_query_highlighter.cpp`:

```cpp
#include <doctest/doctest.h>
#include <filesystem>
#include "query_highlighter.h"
#include "grammar_registry.h"
#include "scope.h"

TEST_CASE("QueryHighlighter returns empty for null tree") {
    SyntaxPalette pal = SyntaxPalette::defaults(false);
    auto spans = QueryHighlighter::highlight(nullptr, nullptr, pal);
    CHECK(spans.empty());
}

TEST_CASE("QueryHighlighter produces spans for C code"
    * doctest::skip(!std::filesystem::exists("grammars/c/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    std::string source = "// comment\nint x = 1;";

    auto* tree = reg.parse("c", source);
    REQUIRE(tree != nullptr);
    auto* query = reg.get_query("c");
    REQUIRE(query != nullptr);

    SyntaxPalette pal = SyntaxPalette::defaults(false);
    auto spans = QueryHighlighter::highlight(tree, query, pal);

    CHECK_FALSE(spans.empty());

    // Spans must be sorted by start
    for (size_t i = 1; i < spans.size(); i++) {
        CHECK(spans[i].start >= spans[i - 1].start);
    }

    ts_tree_delete(tree);
}

TEST_CASE("QueryHighlighter produces spans for JSON"
    * doctest::skip(!std::filesystem::exists("grammars/json/tree-sitter-json.dll"))) {
    GrammarRegistry reg(L"grammars");
    std::string source = R"({"key": "value", "num": 42})";

    auto* tree = reg.parse("json", source);
    REQUIRE(tree != nullptr);
    auto* query = reg.get_query("json");
    REQUIRE(query != nullptr);

    SyntaxPalette pal = SyntaxPalette::defaults(false);
    auto spans = QueryHighlighter::highlight(tree, query, pal);
    CHECK_FALSE(spans.empty());

    ts_tree_delete(tree);
}

TEST_CASE("QueryHighlighter produces spans for Python"
    * doctest::skip(!std::filesystem::exists("grammars/python/tree-sitter-python.dll"))) {
    GrammarRegistry reg(L"grammars");
    std::string source = "def foo(x):\n    return x + 1\n";

    auto* tree = reg.parse("python", source);
    REQUIRE(tree != nullptr);
    auto* query = reg.get_query("python");
    REQUIRE(query != nullptr);

    SyntaxPalette pal = SyntaxPalette::defaults(false);
    auto spans = QueryHighlighter::highlight(tree, query, pal);
    CHECK_FALSE(spans.empty());

    ts_tree_delete(tree);
}

TEST_CASE("QueryHighlighter spans are non-overlapping"
    * doctest::skip(!std::filesystem::exists("grammars/c/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    std::string source = "int x = 1; // comment\nfloat y = 2.0;";

    auto* tree = reg.parse("c", source);
    REQUIRE(tree != nullptr);
    auto* query = reg.get_query("c");
    REQUIRE(query != nullptr);

    SyntaxPalette pal = SyntaxPalette::defaults(false);
    auto spans = QueryHighlighter::highlight(tree, query, pal);
    CHECK_FALSE(spans.empty());

    for (size_t i = 1; i < spans.size(); i++) {
        uint32_t prev_end = spans[i - 1].start + spans[i - 1].length;
        CHECK(spans[i].start >= prev_end);
    }

    ts_tree_delete(tree);
}

TEST_CASE("QueryHighlighter dark mode produces different colors"
    * doctest::skip(!std::filesystem::exists("grammars/c/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    std::string source = "int x = 1;";

    auto* tree = reg.parse("c", source);
    REQUIRE(tree != nullptr);
    auto* query = reg.get_query("c");
    REQUIRE(query != nullptr);

    auto light = QueryHighlighter::highlight(tree, query, SyntaxPalette::defaults(false));
    auto dark  = QueryHighlighter::highlight(tree, query, SyntaxPalette::defaults(true));

    CHECK_FALSE(light.empty());
    CHECK_FALSE(dark.empty());
    // At least one span should have a different color
    bool found_diff = false;
    size_t n = std::min(light.size(), dark.size());
    for (size_t i = 0; i < n; i++) {
        if (light[i].color != dark[i].color) { found_diff = true; break; }
    }
    CHECK(found_diff);

    ts_tree_delete(tree);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset conan-release --target colorizer-tests`
Expected: Compilation failure — `query_highlighter.h` does not exist.

- [ ] **Step 3: Create query_highlighter.h**

Create `src/colorizer/query_highlighter.h`:

```cpp
#pragma once

#include <tree_sitter/api.h>
#include "colorizer.h"  // for ColorSpan
#include "theme_loader.h"
#include <vector>

class QueryHighlighter {
public:
    // Run highlight query on a parsed tree. Returns sorted, non-overlapping ColorSpans.
    // tree and query must both be non-null, or returns empty.
    static std::vector<ColorSpan> highlight(
        const TSTree* tree,
        const TSQuery* query,
        const SyntaxPalette& palette);
};
```

- [ ] **Step 4: Create query_highlighter.cpp**

Create `src/colorizer/query_highlighter.cpp`:

```cpp
#include "query_highlighter.h"
#include "scope.h"
#include <algorithm>
#include <vector>
#include <string_view>

std::vector<ColorSpan> QueryHighlighter::highlight(
    const TSTree* tree,
    const TSQuery* query,
    const SyntaxPalette& palette)
{
    if (!tree || !query) return {};

    // Build capture_index -> Scope lookup
    uint32_t capture_count = ts_query_capture_count(query);
    std::vector<Scope> capture_scopes(capture_count, Scope::Plain);
    for (uint32_t i = 0; i < capture_count; i++) {
        uint32_t name_len = 0;
        const char* name = ts_query_capture_name_for_id(query, i, &name_len);
        capture_scopes[i] = capture_to_scope(std::string_view(name, name_len));
    }

    // Execute query
    TSQueryCursor* cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, ts_tree_root_node(tree));

    // Collect raw captures in source order
    struct RawSpan {
        uint32_t start;
        uint32_t end;
        uint32_t pattern_index;
        Scope scope;
    };
    std::vector<RawSpan> raw;

    TSQueryMatch match;
    uint32_t capture_index;
    while (ts_query_cursor_next_capture(cursor, &match, &capture_index)) {
        const TSQueryCapture& cap = match.captures[capture_index];
        uint32_t start = ts_node_start_byte(cap.node);
        uint32_t end = ts_node_end_byte(cap.node);
        if (start >= end) continue;

        Scope scope = capture_scopes[cap.index];
        if (scope == Scope::Plain) continue;  // skip uncolored captures

        raw.push_back({start, end, match.pattern_index, scope});
    }

    ts_query_cursor_delete(cursor);

    if (raw.empty()) return {};

    // Sort by start position, then by pattern_index descending (later patterns win)
    std::sort(raw.begin(), raw.end(), [](const RawSpan& a, const RawSpan& b) {
        if (a.start != b.start) return a.start < b.start;
        return a.pattern_index > b.pattern_index;
    });

    // Flatten overlapping spans: sweep left-to-right, later patterns take precedence
    std::vector<ColorSpan> result;
    result.reserve(raw.size());

    uint32_t covered_until = 0;
    for (auto& r : raw) {
        uint32_t eff_start = std::max(r.start, covered_until);
        if (eff_start >= r.end) continue;

        ColorSpan cs;
        cs.start = eff_start;
        cs.length = r.end - eff_start;
        cs.color = scope_to_color(r.scope, palette);
        result.push_back(cs);

        covered_until = r.end;
    }

    return result;
}
```

- [ ] **Step 5: Add to CMake and run tests**

Add `src/colorizer/query_highlighter.cpp` to `colorizer-core` sources and `tests/test_colorizer_query_highlighter.cpp` to `colorizer-tests` sources in `CMakeLists.txt`.

Run: `cmake --build --preset conan-release --target colorizer-tests && ./build/Release/colorizer-tests.exe -tc="QueryHighlighter*"`
Expected: All QueryHighlighter tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/colorizer/query_highlighter.h src/colorizer/query_highlighter.cpp tests/test_colorizer_query_highlighter.cpp CMakeLists.txt
git commit -m "feat: QueryHighlighter using tree-sitter highlight queries"
```

---

### Task 5: Wire Up Colorizer Pipeline

**Files:**
- Modify: `src/colorizer/colorizer.cpp`
- Modify: `tests/test_colorizer.cpp`

- [ ] **Step 1: Update end-to-end tests for new grammar paths**

Update skip conditions in `tests/test_colorizer.cpp` to use subdirectory paths:

```cpp
#include <doctest/doctest.h>
#include <filesystem>
#include "colorizer.h"

TEST_CASE("Colorizer with no grammar dir returns empty result") {
    Colorizer c(L"nonexistent", L"nonexistent");
    auto result = c.colorize("int x = 1;", "c", false);
    CHECK(result.spans.empty());
}

TEST_CASE("Colorizer with no grammar dir supports returns false") {
    Colorizer c(L"nonexistent", L"nonexistent");
    CHECK_FALSE(c.supports("c"));
}

TEST_CASE("Colorizer available_languages with no grammar dir is empty") {
    Colorizer c(L"nonexistent", L"nonexistent");
    CHECK(c.available_languages().empty());
}

TEST_CASE("Colorizer end-to-end with C grammar"
    * doctest::skip(!std::filesystem::exists("grammars/c/tree-sitter-c.dll"))) {
    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("c"));

    auto result = c.colorize("// comment\nint x = 1;", "c", false);
    CHECK_FALSE(result.spans.empty());

    for (size_t i = 1; i < result.spans.size(); i++) {
        CHECK(result.spans[i].start >= result.spans[i - 1].start);
    }
}

TEST_CASE("Colorizer dark mode produces different colors"
    * doctest::skip(!std::filesystem::exists("grammars/c/tree-sitter-c.dll"))) {
    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("c"));

    auto light = c.colorize("int x = 1;", "c", false);
    auto dark  = c.colorize("int x = 1;", "c", true);

    CHECK_FALSE(light.spans.empty());
    CHECK_FALSE(dark.spans.empty());
    CHECK(light.spans[0].color != dark.spans[0].color);
}

TEST_CASE("Colorizer end-to-end with JSON grammar"
    * doctest::skip(!std::filesystem::exists("grammars/json/tree-sitter-json.dll"))) {
    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("json"));

    auto result = c.colorize(R"({"key": "value", "num": 42})", "json", false);
    CHECK_FALSE(result.spans.empty());
}

TEST_CASE("Colorizer end-to-end with Python grammar"
    * doctest::skip(!std::filesystem::exists("grammars/python/tree-sitter-python.dll"))) {
    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("python"));

    auto result = c.colorize("def foo(x):\n    return x + 1\n", "python", false);
    CHECK_FALSE(result.spans.empty());
}
```

- [ ] **Step 2: Rewrite Colorizer::colorize to use new pipeline**

Replace `src/colorizer/colorizer.cpp`:

```cpp
#include "colorizer.h"
#include "grammar_registry.h"
#include "query_highlighter.h"
#include "theme_loader.h"

Colorizer::Colorizer(const std::wstring& grammar_dir, const std::wstring& theme_dir)
    : grammar_registry_(std::make_unique<GrammarRegistry>(grammar_dir))
    , theme_loader_(std::make_unique<ThemeLoader>(theme_dir)) {}

Colorizer::~Colorizer() = default;

bool Colorizer::supports(const std::string& language) const {
    return grammar_registry_->supports(language);
}

std::vector<std::string> Colorizer::available_languages() const {
    return grammar_registry_->available_languages();
}

void Colorizer::set_language_theme(const std::string& language, const std::string& theme_name) {
    theme_loader_->set_language_theme(language, theme_name);
}

ColorizeResult Colorizer::colorize(const std::string& source,
                                   const std::string& language,
                                   bool dark_mode) {
    ColorizeResult result;

    auto* tree = grammar_registry_->parse(language, source);
    if (!tree) return result;

    auto* query = grammar_registry_->get_query(language);
    if (!query) {
        ts_tree_delete(tree);
        return result;
    }

    auto palette = theme_loader_->palette_for(language, dark_mode);
    result.spans = QueryHighlighter::highlight(tree, query, palette);

    ts_tree_delete(tree);
    return result;
}
```

- [ ] **Step 3: Run tests**

Run: `cmake --build --preset conan-release --target colorizer-tests && ./build/Release/colorizer-tests.exe -tc="Colorizer*"`
Expected: All Colorizer end-to-end tests PASS.

- [ ] **Step 4: Commit**

```bash
git add src/colorizer/colorizer.cpp tests/test_colorizer.cpp
git commit -m "feat: wire Colorizer to query-based highlighting pipeline"
```

---

### Task 6: Delete Old Files and Update CMake

**Files:**
- Delete: `src/colorizer/tokenizer.h`, `src/colorizer/tokenizer.cpp`
- Delete: `src/colorizer/scope_mapper.h`, `src/colorizer/scope_mapper.cpp`
- Delete: `tests/test_colorizer_scope.cpp`, `tests/test_colorizer_tokenizer.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Remove old files**

```bash
rm src/colorizer/tokenizer.h src/colorizer/tokenizer.cpp
rm src/colorizer/scope_mapper.h src/colorizer/scope_mapper.cpp
rm tests/test_colorizer_scope.cpp tests/test_colorizer_tokenizer.cpp
```

- [ ] **Step 2: Update CMakeLists.txt**

In the `colorizer-core` target, replace:
```cmake
    src/colorizer/scope_mapper.cpp
    src/colorizer/tokenizer.cpp
```
with:
```cmake
    src/colorizer/scope.cpp
    src/colorizer/query_highlighter.cpp
```

In the `colorizer-tests` target, replace:
```cmake
    tests/test_colorizer_scope.cpp
    tests/test_colorizer_tokenizer.cpp
```
with:
```cmake
    tests/test_colorizer_scope_new.cpp
    tests/test_colorizer_query_highlighter.cpp
```

Update the grammar post-build to copy subdirectories:
```cmake
add_custom_command(TARGET wlx-listerine-colorizer POST_BUILD
    # ... existing config copies ...
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/grammars"
        "${CMAKE_SOURCE_DIR}/output/grammars"
)
```
(This already copies the whole directory, so it works with subdirectories.)

Update the `add_grammar` function to output into subdirectories:

```cmake
function(add_grammar LANG SOURCE_DIR)
    if(EXISTS "${SOURCE_DIR}/src/parser.c")
        set(GRAMMAR_SOURCES "${SOURCE_DIR}/src/parser.c")
        if(EXISTS "${SOURCE_DIR}/src/scanner.c")
            list(APPEND GRAMMAR_SOURCES "${SOURCE_DIR}/src/scanner.c")
        elseif(EXISTS "${SOURCE_DIR}/src/scanner.cc")
            list(APPEND GRAMMAR_SOURCES "${SOURCE_DIR}/src/scanner.cc")
        endif()
        add_library(tree-sitter-${LANG} SHARED ${GRAMMAR_SOURCES})
        target_include_directories(tree-sitter-${LANG} PRIVATE "${SOURCE_DIR}/src")
        target_link_libraries(tree-sitter-${LANG} PRIVATE tree-sitter::tree-sitter)
        set_target_properties(tree-sitter-${LANG} PROPERTIES
            PREFIX ""
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/grammars/${LANG}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/grammars/${LANG}"
        )
        # Copy highlights.scm if present in source
        if(EXISTS "${SOURCE_DIR}/queries/highlights.scm")
            add_custom_command(TARGET tree-sitter-${LANG} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SOURCE_DIR}/queries/highlights.scm"
                    "${CMAKE_SOURCE_DIR}/grammars/${LANG}/highlights.scm"
            )
        endif()
    endif()
endfunction()
```

- [ ] **Step 3: Build and run all tests**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe`
Expected: All colorizer tests PASS. No references to deleted files remain.

Also build the main plugin:
Run: `cmake --build --preset conan-release --target wlx-listerine-colorizer`
Expected: Builds successfully.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "refactor: remove Tokenizer and ScopeMapper, update CMake for subdirectory grammars"
```

---

### Task 7: Expand ThemeLoader for New Palette Keys

**Files:**
- Modify: `src/colorizer/theme_loader.cpp`
- Modify: `tests/test_colorizer_theme.cpp`

- [ ] **Step 1: Add tests for new palette fields**

Append to `tests/test_colorizer_theme.cpp`:

```cpp
TEST_CASE("SyntaxPalette defaults include expanded fields") {
    SyntaxPalette light = SyntaxPalette::defaults(false);
    CHECK(light.function_builtin != 0);
    CHECK(light.string_escape != 0);
    CHECK(light.boolean_lit != 0);
    CHECK(light.tag != 0);
    CHECK(light.constructor != 0);
    CHECK(light.property != 0);

    SyntaxPalette dark = SyntaxPalette::defaults(true);
    CHECK(dark.function_builtin != 0);
    CHECK(dark.string_escape != 0);
    CHECK(dark.boolean_lit != 0);
    CHECK(dark.tag != 0);
    CHECK(dark.constructor != 0);
    CHECK(dark.property != 0);
}

TEST_CASE("SyntaxPalette light and dark expanded fields differ") {
    SyntaxPalette light = SyntaxPalette::defaults(false);
    SyntaxPalette dark = SyntaxPalette::defaults(true);
    CHECK(light.string_escape != dark.string_escape);
    CHECK(light.tag != dark.tag);
}
```

- [ ] **Step 2: Update ThemeLoader to read new TOML keys**

In `src/colorizer/theme_loader.cpp`, inside the `read_palette` lambda, add reads for the new fields after the existing 13:

```cpp
read("constant_builtin", pal.constant_builtin);
read("function_builtin", pal.function_builtin);
read("function_call",    pal.function_call);
read("string_escape",    pal.string_escape);
read("string_special",   pal.string_special);
read("boolean",          pal.boolean_lit);
read("tag",              pal.tag);
read("tag_delimiter",    pal.tag_delimiter);
read("attribute",        pal.attribute);
read("constructor",      pal.constructor);
read("property",         pal.property);
read("label",            pal.label);
```

These are all optional — missing keys keep the value from `SyntaxPalette::defaults()` which is already set before parsing.

- [ ] **Step 3: Run tests**

Run: `cmake --build --preset conan-release --target colorizer-tests && ./build/Release/colorizer-tests.exe -tc="*Palette*,*ThemeLoader*"`
Expected: All theme tests PASS.

- [ ] **Step 4: Commit**

```bash
git add src/colorizer/theme_loader.cpp tests/test_colorizer_theme.cpp
git commit -m "feat: ThemeLoader reads expanded palette keys from TOML"
```

---

### Task 8: Expand Extension Map

**Files:**
- Modify: `src/colorizer/colorizer_host_adapter.cpp`

- [ ] **Step 1: Expand kExtLangMap**

Replace the `kExtLangMap` array in `src/colorizer/colorizer_host_adapter.cpp` with the full starter set:

```cpp
static const struct { const wchar_t* ext; const char* lang; } kExtLangMap[] = {
    // C / C++
    { L"c",           "c"          },
    { L"h",           "c"          },
    { L"cpp",         "cpp"        },
    { L"cc",          "cpp"        },
    { L"cxx",         "cpp"        },
    { L"hpp",         "cpp"        },
    { L"hxx",         "cpp"        },
    // Python
    { L"py",          "python"     },
    { L"pyi",         "python"     },
    // JavaScript / TypeScript
    { L"js",          "javascript" },
    { L"mjs",         "javascript" },
    { L"cjs",         "javascript" },
    { L"jsx",         "javascript" },
    { L"ts",          "typescript" },
    { L"tsx",         "typescript" },
    { L"mts",         "typescript" },
    // Rust
    { L"rs",          "rust"       },
    // Go
    { L"go",          "go"         },
    // Java
    { L"java",        "java"       },
    // C#
    { L"cs",          "c-sharp"    },
    // PHP
    { L"php",         "php"        },
    // Lua
    { L"lua",         "lua"        },
    // Shell
    { L"sh",          "bash"       },
    { L"bash",        "bash"       },
    { L"zsh",         "bash"       },
    // PowerShell
    { L"ps1",         "powershell" },
    { L"psm1",        "powershell" },
    { L"psd1",        "powershell" },
    // Vim
    { L"vim",         "vim"        },
    { L"vimrc",       "vim"        },
    // Data / Config
    { L"json",        "json"       },
    { L"jsonc",       "json"       },
    { L"toml",        "toml"       },
    { L"yaml",        "yaml"       },
    { L"yml",         "yaml"       },
    // Markup
    { L"html",        "html"       },
    { L"htm",         "html"       },
    { L"xml",         "xml"        },
    { L"svg",         "xml"        },
    { L"css",         "css"        },
    { L"md",          "markdown"   },
    { L"markdown",    "markdown"   },
    // Build / DevOps
    { L"cmake",       "cmake"      },
    { L"sql",         "sql"        },
    // Git
    { L"gitconfig",   "gitconfig"  },
    { L"gitignore",   "gitignore"  },
    { L"gitattributes", "gitattributes" },
};
```

Also add filename-based detection for files without extensions. Add a helper after `ext_to_language`:

```cpp
static std::string filename_to_language(const std::wstring& path) {
    auto slash = path.find_last_of(L"\\/");
    std::wstring filename = (slash != std::wstring::npos)
        ? path.substr(slash + 1) : path;
    // lowercase
    for (auto& c : filename) c = static_cast<wchar_t>(towlower(c));

    if (filename == L"dockerfile" || filename == L"containerfile")
        return "dockerfile";
    if (filename == L"cmakelists.txt")
        return "cmake";
    if (filename == L"makefile" || filename == L"gnumakefile")
        return "make";
    if (filename == L".gitconfig")
        return "gitconfig";
    if (filename == L".gitignore")
        return "gitignore";
    if (filename == L".gitattributes")
        return "gitattributes";

    return {};
}
```

Update `load_document` to try filename detection when extension detection returns empty:

In the `load_document` function, change:
```cpp
std::string language = ext_to_language(vs->file_path);
```
to:
```cpp
std::string language = ext_to_language(vs->file_path);
if (language.empty())
    language = filename_to_language(vs->file_path);
```

Apply the same change in the `lc_newparams` handler.

- [ ] **Step 2: Build and verify**

Run: `cmake --build --preset conan-release --target wlx-listerine-colorizer`
Expected: Builds successfully.

- [ ] **Step 3: Commit**

```bash
git add src/colorizer/colorizer_host_adapter.cpp
git commit -m "feat: expand extension and filename detection for all starter languages"
```

---

### Task 9: Update Colorizer Config Extensions

**Files:**
- Modify: `config/wlx-listerine-colorizer.toml`

- [ ] **Step 1: Expand extensions list and detect_string**

Update the `[general]` section to include all file extensions the colorizer handles:

```toml
[general]
extensions = [
    "c", "h", "cpp", "cc", "cxx", "hpp", "hxx",
    "py", "pyi",
    "js", "mjs", "cjs", "jsx", "ts", "tsx", "mts",
    "rs", "go", "java", "cs",
    "php", "lua",
    "sh", "bash", "zsh",
    "ps1", "psm1", "psd1",
    "vim", "vimrc",
    "json", "jsonc", "toml", "yaml", "yml",
    "html", "htm", "xml", "svg", "css",
    "md", "markdown",
    "cmake", "sql",
    "gitconfig", "gitignore", "gitattributes",
]
```

Update `detect_string` to match (TC uses `|` separated `EXT="X"` conditions):

```toml
detect_string = 'EXT="C" | EXT="H" | EXT="CPP" | EXT="CC" | EXT="CXX" | EXT="HPP" | EXT="HXX" | EXT="PY" | EXT="PYI" | EXT="JS" | EXT="MJS" | EXT="CJS" | EXT="JSX" | EXT="TS" | EXT="TSX" | EXT="MTS" | EXT="RS" | EXT="GO" | EXT="JAVA" | EXT="CS" | EXT="PHP" | EXT="LUA" | EXT="SH" | EXT="BASH" | EXT="ZSH" | EXT="PS1" | EXT="PSM1" | EXT="PSD1" | EXT="VIM" | EXT="VIMRC" | EXT="JSON" | EXT="JSONC" | EXT="TOML" | EXT="YAML" | EXT="YML" | EXT="HTML" | EXT="HTM" | EXT="XML" | EXT="SVG" | EXT="CSS" | EXT="MD" | EXT="MARKDOWN" | EXT="CMAKE" | EXT="SQL" | EXT="GITCONFIG" | EXT="GITIGNORE" | EXT="GITATTRIBUTES"'
```

- [ ] **Step 2: Commit**

```bash
git add config/wlx-listerine-colorizer.toml
git commit -m "feat: expand colorizer detect_string for all starter languages"
```

---

### Task 10: Fetch and Build Remaining Starter Grammars

**Files:**
- Modify: `CMakeLists.txt` (add grammar targets)
- Create: grammar subdirectories in `grammars/`

- [ ] **Step 1: Run fetch script for all starter grammars**

```bash
chmod +x scripts/fetch-grammars.sh
./scripts/fetch-grammars.sh
```

This clones grammar repos into `build/grammars/` and copies `highlights.scm` files into `grammars/{lang}/`.

- [ ] **Step 2: Add grammar build targets to CMakeLists.txt**

Add all 28 starter grammars (replacing the existing 3):

```cmake
# --- Grammar DLLs ---
add_grammar(c          "${CMAKE_SOURCE_DIR}/build/grammars/c")
add_grammar(cpp        "${CMAKE_SOURCE_DIR}/build/grammars/cpp")
add_grammar(python     "${CMAKE_SOURCE_DIR}/build/grammars/python")
add_grammar(javascript "${CMAKE_SOURCE_DIR}/build/grammars/javascript")
add_grammar(typescript "${CMAKE_SOURCE_DIR}/build/grammars/typescript")
add_grammar(rust       "${CMAKE_SOURCE_DIR}/build/grammars/rust")
add_grammar(go         "${CMAKE_SOURCE_DIR}/build/grammars/go")
add_grammar(java       "${CMAKE_SOURCE_DIR}/build/grammars/java")
add_grammar(c-sharp    "${CMAKE_SOURCE_DIR}/build/grammars/c-sharp")
add_grammar(json       "${CMAKE_SOURCE_DIR}/build/grammars/json")
add_grammar(html       "${CMAKE_SOURCE_DIR}/build/grammars/html")
add_grammar(css        "${CMAKE_SOURCE_DIR}/build/grammars/css")
add_grammar(bash       "${CMAKE_SOURCE_DIR}/build/grammars/bash")
add_grammar(toml       "${CMAKE_SOURCE_DIR}/build/grammars/toml")
add_grammar(yaml       "${CMAKE_SOURCE_DIR}/build/grammars/yaml")
add_grammar(lua        "${CMAKE_SOURCE_DIR}/build/grammars/lua")
add_grammar(php        "${CMAKE_SOURCE_DIR}/build/grammars/php")
add_grammar(powershell "${CMAKE_SOURCE_DIR}/build/grammars/powershell")
add_grammar(vim        "${CMAKE_SOURCE_DIR}/build/grammars/vim")
add_grammar(dockerfile "${CMAKE_SOURCE_DIR}/build/grammars/dockerfile")
add_grammar(cmake      "${CMAKE_SOURCE_DIR}/build/grammars/cmake")
add_grammar(markdown   "${CMAKE_SOURCE_DIR}/build/grammars/markdown")
add_grammar(gitcommit  "${CMAKE_SOURCE_DIR}/build/grammars/gitcommit")
add_grammar(gitconfig  "${CMAKE_SOURCE_DIR}/build/grammars/gitconfig")
add_grammar(gitignore  "${CMAKE_SOURCE_DIR}/build/grammars/gitignore")
add_grammar(gitattributes "${CMAKE_SOURCE_DIR}/build/grammars/gitattributes")
add_grammar(git_rebase "${CMAKE_SOURCE_DIR}/build/grammars/git_rebase")
add_grammar(unreal-cpp "${CMAKE_SOURCE_DIR}/build/grammars/unreal-cpp")
```

Note: TypeScript may need special handling — tree-sitter-typescript has `typescript/src/parser.c` and `tsx/src/parser.c` as subdirectories. The `add_grammar` function may need the `SOURCE_DIR` pointed at the specific sub-grammar. Check the repo structure and adjust paths.

- [ ] **Step 3: Build all grammars**

Run: `cmake --preset conan-default && cmake --build --preset conan-release`
Expected: All grammar DLLs compile. Some may fail if repo structure differs — fix `add_grammar` paths as needed.

- [ ] **Step 4: Run all tests**

Run: `./build/Release/colorizer-tests.exe`
Expected: All tests PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt grammars/ scripts/
git commit -m "feat: add 28 starter grammar build targets"
```

---

### Task 11: Grammar Pack Zip Script

**Files:**
- Create: `scripts/package-grammars.sh`

- [ ] **Step 1: Create packaging script**

Create `scripts/package-grammars.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
GRAMMAR_DIR="$ROOT_DIR/grammars"
OUT="$ROOT_DIR/build/grammars-all.zip"

cd "$GRAMMAR_DIR"

# Only include directories that have both a DLL and highlights.scm
dirs_to_include=()
for d in */; do
    lang="${d%/}"
    if ls "$lang"/tree-sitter-*.dll 1>/dev/null 2>&1 && [ -f "$lang/highlights.scm" ]; then
        dirs_to_include+=("$lang")
    fi
done

if [ ${#dirs_to_include[@]} -eq 0 ]; then
    echo "No complete grammar packages found."
    exit 1
fi

echo "Packaging ${#dirs_to_include[@]} grammars into $OUT"

# Create zip with grammars/ prefix so extraction creates the right structure
cd "$ROOT_DIR"
rm -f "$OUT"

zip_args=()
for lang in "${dirs_to_include[@]}"; do
    zip_args+=("grammars/$lang/")
done

zip -r "$OUT" "${zip_args[@]}"

echo "Done: $OUT ($(du -h "$OUT" | cut -f1))"
```

- [ ] **Step 2: Test the script**

```bash
chmod +x scripts/package-grammars.sh
./scripts/package-grammars.sh
```

Expected: Creates `build/grammars-all.zip` containing the `grammars/{lang}/` directory structure.

- [ ] **Step 3: Commit**

```bash
git add scripts/package-grammars.sh
git commit -m "feat: grammar pack zip packaging script"
```

---

### Task 12: Final Integration Test

- [ ] **Step 1: Full build**

```bash
conan install . --output-folder=build --build=missing -s build_type=Release
cmake --preset conan-default
cmake --build --preset conan-release
```

Expected: Both plugins and all grammar DLLs build successfully.

- [ ] **Step 2: Run all test suites**

```bash
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

Expected: All tests PASS in both suites.

- [ ] **Step 3: Manual smoke test**

Load a file in Total Commander using the colorizer plugin. Verify syntax highlighting works for at least C, Python, and JSON files. Compare output quality with the old node-type-based highlighting.

- [ ] **Step 4: Final commit if any fixups needed**

```bash
git add -A
git commit -m "fix: integration test fixups"
```
