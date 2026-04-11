# Query-Based Syntax Highlighting Design

**Date**: 2026-04-11
**Status**: Draft
**Scope**: Replace node-type scope mapping with tree-sitter highlight queries; restructure grammar distribution

## Problem

The current colorizer maps tree-sitter node type names to 13 color scopes via ~380 lines of hardcoded C++ keyword tables (`scope_mapper.cpp`). This approach does not scale to 30+ languages because:

- Every tree-sitter grammar invents its own node types. Each language requires manual audit and table population.
- The keyword-as-leaf-node trick is grammar-dependent. Some grammars use named nodes for keywords.
- 13 scopes is too coarse for good highlighting across diverse languages.

## Solution

Use tree-sitter highlight queries (`.scm` files) that ship with every grammar. These are community-maintained, battle-tested files that map AST patterns to standard capture names (`@keyword`, `@function`, `@string`, etc.). This is the same mechanism VS Code, Neovim, Helix, and Zed use.

## Architecture

### Grammar Directory Restructure

**Current**: Flat directory with `tree-sitter-*.dll` files.

**New**: Subdirectories per language, each containing a DLL and highlight query:

```
grammars/
  c/
    tree-sitter-c.dll
    highlights.scm
  cpp/
    tree-sitter-cpp.dll
    highlights.scm
  python/
    tree-sitter-python.dll
    highlights.scm
  ...
```

Users add a language by dropping a folder with DLL + `highlights.scm` into `grammars/`. No configuration needed.

### GrammarRegistry Changes

`scan_directory()` iterates subdirectories instead of flat files. For each subdirectory, finds the first `.dll` matching `tree-sitter-*.dll` and loads `highlights.scm` next to it.

`GrammarEntry` gains:
- `std::string query_source` — raw `.scm` text, loaded eagerly on scan
- `TSQuery* query` — compiled query, lazily compiled on first use

New methods:
- `TSTree* parse(const std::string& language, const std::string& source)` — creates parser, parses source, returns tree (caller owns the tree)
- `const TSQuery* get_query(const std::string& language)` — lazily compiles and caches the highlight query

The `; inherits: lang` directive (first line of some `.scm` files) is handled at query compile time: parse the comment, recursively load the referenced language's `.scm`, prepend it before passing to `ts_query_new`.

### QueryHighlighter (replaces Tokenizer + ScopeMapper)

**Deleted files**: `tokenizer.h/.cpp`, `scope_mapper.h/.cpp`

**New file**: `query_highlighter.h/.cpp`

```cpp
class QueryHighlighter {
public:
    static std::vector<ColorSpan> highlight(
        TSTree* tree,
        const TSQuery* query,
        const SyntaxPalette& palette);
};
```

Internally:
1. Builds a `capture_index -> Scope` lookup table from `ts_query_capture_name_for_id` (once per query invocation)
2. Runs `ts_query_cursor_next_capture()` to iterate captures in source order
3. Maps each capture's scope to a color via the palette
4. Resolves overlapping captures: later patterns take precedence (standard tree-sitter convention)
5. Returns sorted, non-overlapping `ColorSpan` vector — same output type as today

**Predicate handling**:
- Initial implementation: Skip predicates. Most `highlights.scm` files produce acceptable results without them.
- Follow-up (separate task, not in scope of this spec): Implement `#eq?`, `#match?`, `#any-of?` — the three predicates that materially affect highlight quality. The tree-sitter C API does not evaluate predicates automatically; `ts_query_predicates_for_pattern()` returns structured predicate data that must be interpreted manually.

### Colorizer Pipeline

**Current**:
```
Colorizer::colorize(source, language, dark_mode)
  -> Tokenizer::tokenize(grammar, source)         // parse tree + walk leaves
  -> ScopeMapper::for_language(language)           // keyword table lookup
  -> for each token: ScopeMapper::map -> scope_to_color
  -> vector<ColorSpan>
```

**New**:
```
Colorizer::colorize(source, language, dark_mode)
  -> grammar_registry_->parse(language, source)    // TSTree*
  -> grammar_registry_->get_query(language)        // TSQuery* from highlights.scm
  -> theme_loader_->palette_for(language, dark_mode)
  -> QueryHighlighter::highlight(tree, query, palette)
  -> vector<ColorSpan>
```

**Public API unchanged**: `Colorizer::colorize()` signature and `ColorizeResult` type are identical. Host adapter, layout engine, and render engine require no changes.

### Expanded Scope Enum

Current 13 scopes expand to ~20. New additions based on captures that upstream `.scm` files actually produce:

| New Scope | Capture source | Fallback |
|---|---|---|
| `ConstantBuiltin` | `@constant.builtin` | `Keyword2` |
| `FunctionBuiltin` | `@function.builtin` | `Function` |
| `FunctionCall` | `@function.call` | `Function` |
| `StringEscape` | `@string.escape` | `String` |
| `StringSpecial` | `@string.special`, `@string.regexp` | `String` |
| `Boolean` | `@boolean` | `Number` |
| `Tag` | `@tag`, `@tag.builtin` | `Type` |
| `TagDelimiter` | `@tag.delimiter` | `Punctuation` |
| `Attribute` | `@attribute`, `@tag.attribute` | `Preprocessor` |
| `Constructor` | `@constructor` | `Type` |
| `Property` | `@property` | `Variable` |
| `Label` | `@label` | `Variable` |

**Capture-to-scope mapping** uses longest prefix match:
- `@keyword.return` -> no exact match -> strip suffix -> `@keyword` -> `Scope::Keyword`
- `@string.escape` -> exact match -> `Scope::StringEscape`
- `@function.builtin` -> exact match -> `Scope::FunctionBuiltin`

### Theme System

**Hardcoded defaults** (`SyntaxPalette::defaults()`) expand to include colors for all ~20 scopes. New scopes get slightly differentiated variants of their fallback color (e.g. `StringEscape` slightly brighter than `String`).

**Theme TOML** (optional user override) can specify any of the ~20 keys. Missing keys inherit from hardcoded defaults via the fallback chain. Existing 13-key theme files remain fully compatible.

**No default theme file shipped**. The hardcoded palette is the default. Theme TOML is purely an override mechanism for users who want custom colors.

**Per-language theme mapping** remains available via `ThemeLoader::set_language_theme()` but is not used by default.

## Grammar Distribution

### Starter Set (28 grammars, bundled with plugin)

| Language | Grammar source |
|---|---|
| C | tree-sitter/tree-sitter-c |
| C++ | tree-sitter/tree-sitter-cpp |
| Python | tree-sitter/tree-sitter-python |
| JavaScript | tree-sitter/tree-sitter-javascript |
| TypeScript | tree-sitter/tree-sitter-typescript |
| Rust | tree-sitter/tree-sitter-rust |
| Go | tree-sitter/tree-sitter-go |
| Java | tree-sitter/tree-sitter-java |
| C# | tree-sitter/tree-sitter-c-sharp |
| JSON | tree-sitter/tree-sitter-json |
| HTML | tree-sitter/tree-sitter-html |
| CSS | tree-sitter/tree-sitter-css |
| Bash | tree-sitter/tree-sitter-bash |
| TOML | tree-sitter-grammars/tree-sitter-toml |
| YAML | tree-sitter-grammars/tree-sitter-yaml |
| Lua | MunifTanjim/tree-sitter-lua |
| PHP | tree-sitter/tree-sitter-php |
| PowerShell | airbus-cert/tree-sitter-powershell |
| Vim | neovim/tree-sitter-vim |
| Unreal C++ | taku25/tree-sitter-unreal-cpp |
| Dockerfile | camdencheek/tree-sitter-dockerfile |
| CMake | uyha/tree-sitter-cmake |
| Markdown | MDeiml/tree-sitter-markdown |
| gitcommit | the-mikedavis/tree-sitter-git-commit |
| gitconfig | the-mikedavis/tree-sitter-git-config |
| gitignore | shuber/tree-sitter-gitignore |
| gitattributes | ObserverOfTime/tree-sitter-gitattributes |
| git_rebase | the-mikedavis/tree-sitter-git-rebase |

### Downloadable Grammar Pack

CI builds all grammars (starter + extras: Ruby, Kotlin, Swift, Scala, Haskell, Zig, SQL, XML, Scala, etc.) and publishes `grammars-all.zip` as a GitHub release asset.

Zip structure mirrors the `grammars/` directory:
```
grammars/
  rust/
    tree-sitter-rust.dll
    highlights.scm
  ruby/
    tree-sitter-ruby.dll
    highlights.scm
  ...
```

Users extract the entire zip or cherry-pick individual language folders.

### Build System

CMake compiles each grammar from source (`parser.c` + optional `scanner.c`). Grammar sources are either vendored or fetched via `FetchContent`. Each grammar produces a DLL target. Post-build copies DLL + `highlights.scm` into `output/grammars/{lang}/`.

### Extension Map

`kExtLangMap` in `colorizer_host_adapter.cpp` expands to cover all starter languages plus common extras. Extensions for languages without a grammar DLL present are harmlessly ignored — file opens as plain text.

## What Stays the Same

- `ColorSpan`, `ColorizeResult` types
- `Colorizer` public API (`colorize`, `supports`, `available_languages`)
- Host adapter (`colorizer_host_adapter.cpp`) — unchanged except `kExtLangMap` expansion
- Layout engine, render engine — unchanged
- `config/wlx-listerine-colorizer.toml` format
- Grammar auto-discovery principle — same concept, subdirectories instead of flat
- `SyntaxPalette::defaults()` as ultimate fallback when no theme TOML present

## Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Some `.scm` files use Neovim-specific predicates (`vim-match?`, `lua-match?`) | Use upstream grammar repo `.scm` files only, not editor-specific forks |
| Predicate-less highlighting may look wrong for some languages | Phase 2 adds `#eq?`, `#match?`, `#any-of?` which covers the vast majority of predicate usage |
| `; inherits:` chains could be deep or circular | Depth-limit recursive loading (e.g. max 5); track visited set |
| Large grammar set increases plugin size | 28 DLLs at ~100-600 KB each = ~5-10 MB total; acceptable for a TC plugin |
| Overlapping captures in query results | tree-sitter convention: last pattern wins; `next_capture` yields in source order with pattern index for precedence |
