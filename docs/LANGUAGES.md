# Language Support

## Shipped Grammars

The colorizer plugin (`wlx-listerine-colorizer`) ships with 25+ tree-sitter grammars covering:

| Language     | Extensions                              |
|--------------|-----------------------------------------|
| Bash         | `.sh`, `.bash`, `.zsh`                  |
| C / C++      | `.c`, `.h`, `.cpp`, `.cc`, `.cxx`, `.hpp`, `.hxx` (all parsed by tree-sitter-cpp) |
| C#           | `.cs`                                   |
| CMake        | `.cmake`, `CMakeLists.txt`              |
| CSS          | `.css`                                  |
| Dockerfile   | `.dockerfile`, `Dockerfile`, `Containerfile`, `*dockerfile*` |
| Git          | `.gitconfig`, `.gitignore`, `.gitattributes`, `git-rebase-todo` |
| Go           | `.go`                                   |
| HTML         | `.html`, `.htm`                         |
| Java         | `.java`                                 |
| JavaScript   | `.js`, `.mjs`, `.cjs`, `.jsx`           |
| JSON         | `.json`, `.jsonc`                       |
| Lua          | `.lua`                                  |
| PHP          | `.php`                                  |
| PowerShell   | `.ps1`, `.psm1`, `.psd1`                |
| Python       | `.py`, `.pyi`                           |
| Rust         | `.rs`                                   |
| TOML         | `.toml`                                 |
| TypeScript   | `.ts`, `.tsx`, `.mts`                   |
| Vim          | `.vim`, `.vimrc`                        |
| YAML         | `.yaml`, `.yml`                         |

DLLs live at `grammars/<lang>/tree-sitter-<lang>.dll` next to the per-language `highlights.scm`.

Files with extensions outside this set are displayed as plain text with line numbers and whitespace markers, but without syntax highlighting.

### Plugin boundary

`.md` and `.markdown` files are **not** handled by the colorizer — they are owned by the sibling plugin `wlx-listerine-md`, which renders rich markdown rather than tokenized syntax.

## Adding a New Language

The grammar list is hardcoded in CMake (fetched at build time) and the extension map is hardcoded in C++. Adding a language requires three edits and a rebuild:

1. **Declare and fetch the grammar source in `CMakeLists.txt`:**
   ```cmake
   FetchContent_Declare(ts-foo
       GIT_REPOSITORY https://github.com/owner/tree-sitter-foo.git
       GIT_TAG        v1.0.0
       GIT_SHALLOW    TRUE
   )
   fetch_grammar(ts-foo)
   add_grammar(foo "${ts-foo_SOURCE_DIR}")
   ```
   `add_grammar(foo …)` builds `grammars/foo/tree-sitter-foo.dll` and copies upstream `queries/highlights.scm` if no local override exists at `grammars/foo/highlights.scm`.

2. **Map extensions to the language in `src/colorizer/colorizer_host_adapter.cpp`:**
   ```cpp
   static const struct { const wchar_t* ext; const char* lang; } kExtLangMap[] = {
       // ...
       { L"foo",  "foo" },
   };
   ```
   Add an entry to `kDefaultDetectString` so Total Commander knows to route `.foo` files to the colorizer:
   ```cpp
   L"... | EXT=\"FOO\" | ...";
   ```
   For language detection by filename rather than extension (e.g., `Dockerfile`), extend `filename_to_language()` instead.

3. **Rebuild and reload:**
   ```bash
   cmake --build --preset conan-release
   ```
   Restart Total Commander to reload the rebuilt `.wlx64`.

## Local Highlight Overrides

If an upstream grammar's `queries/highlights.scm` doesn't fit the Helix-compatible scope conventions used by `helix_theme.cpp`, drop a custom `highlights.scm` into `grammars/<lang>/` before building. CMake will skip the upstream copy step when a local file exists.

The query language supports `; inherits: <other-lang>` on the first line — see `grammars/cpp/highlights.scm` for an example (it consumes `grammars/c/highlights.scm`).

## Tree-sitter Grammar Repositories

Browse community grammars at the [tree-sitter GitHub org](https://github.com/tree-sitter) and the [tree-sitter-grammars org](https://github.com/tree-sitter-grammars).
