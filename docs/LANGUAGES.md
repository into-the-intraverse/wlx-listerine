# Language Support

## Shipped Grammars

The colorizer plugin (`wlx-listerine-colorizer`) ships 27 tree-sitter grammar DLLs covering 26 languages:

| Language     | Extensions                              |
|--------------|-----------------------------------------|
| Bash         | `.sh`, `.bash`, `.zsh`, shell dotfiles (`.bashrc`, `.bash_profile`, `.zshrc`, `.profile`, `.envrc`, …) |
| C / C++      | `.c`, `.h`, `.cpp`, `.cc`, `.cxx`, `.hpp`, `.hxx` (default: tree-sitter-cpp; opt-in Unreal variant — see below) |
| C#           | `.cs`                                   |
| CMake        | `.cmake`, `CMakeLists.txt`, `CMakeCache.txt` |
| CSS          | `.css`                                  |
| Dockerfile   | `.dockerfile`, `Dockerfile`, `Containerfile`, `*dockerfile*` |
| Git          | `.gitconfig`, `.gitmodules`, `.gitignore`, `.gitattributes`, `git-rebase-todo`; `.dockerignore`/`.npmignore` reuse the gitignore grammar |
| Go           | `.go`                                   |
| HTML         | `.html`, `.htm`                         |
| Java         | `.java`                                 |
| JavaScript   | `.js`, `.mjs`, `.cjs`, `.jsx`           |
| JSON         | `.json`, `.jsonc`, `Pipfile.lock`, `bun.lock` |
| Lua          | `.lua`                                  |
| PHP          | `.php`                                  |
| PowerShell   | `.ps1`, `.psm1`, `.psd1`                |
| Python       | `.py`, `.pyi`                           |
| Rust         | `.rs`                                   |
| TOML         | `.toml`, `Pipfile`, `uv.lock`, `poetry.lock` |
| TypeScript   | `.ts`, `.tsx`, `.mts`                   |
| Vim          | `.vim`, `.vimrc`, `.nvimrc`             |
| XML          | `.xml`, `.svg`, VS/MSBuild files (`.vcxproj`, `.csproj`, `.fsproj`, `.vbproj`, `.proj`, `.props`, `.targets`, `.filters`, `.slnx`, `.xaml`, `.resx`) |
| YAML         | `.yaml`, `.yml`                         |

The extension → grammar map lives in `src/plugin_colorizer/language/path_to_language.h`; filename special cases (`Dockerfile`, `CMakeLists.txt`, lockfiles, `git-rebase-todo`) in its `filename_to_language()`.

DLLs live at `grammars/<lang>/tree-sitter-<lang>.dll` next to the per-language `highlights.scm`.

At install time, drop new grammar subdirectories into `<TC plugin dir>/wlx-listerine/grammars/<lang>/`. Both plugins pick them up from this single shared location — there is no per-plugin grammar directory anymore.

Files with extensions outside this set (including `.txt` and `.sql`, which is routed but has no shipped grammar yet) are displayed as plain text with line numbers and whitespace markers, but without syntax highlighting.

### Per-file grammar override

Right-click → language submenu forces a specific grammar for the current view (session-only; "Auto-detect" restores extension-based routing). Useful for files with misleading or missing extensions.

### Plugin boundary

`.md` and `.markdown` files are owned by the sibling plugin `wlx-listerine-md`, which renders rich markdown rather than tokenized syntax. The colorizer claims the extensions only as a plain-text fallback — its grammar map intentionally has no markdown entry.

## Switching to Unreal C++

Unreal Engine's reflection macros (`UCLASS`, `UPROPERTY`, `UFUNCTION`, `GENERATED_BODY`, `PROJECTNAME_API`, Blueprint property specifiers) parse as ordinary identifiers under the standard C++ grammar. The colorizer ships a second, opt-in grammar from [`taku25/tree-sitter-unreal-cpp`](https://github.com/taku25/tree-sitter-unreal-cpp) that adds dedicated parsing for these.

Enable it in `config/wlx-listerine-colorizer.toml`:

```toml
[colorizer]
cpp_grammar = "unreal"
```

Restart Total Commander. All C/C++ files now route to the Unreal grammar. The Unreal grammar is a strict superset of plain C++, so non-Unreal files continue to parse correctly — they just have no extra macros to highlight.

The fork is **SHA-pinned** in `cmake/grammars.cmake` (no upstream releases yet) as a GitHub archive `URL` + `URL_HASH`. Bumping it means updating the commit SHA in the archive URL and the tarball's `URL_HASH` SHA256. The build-side mechanism that resolves the fork's `tree_sitter_cpp()` symbol export to `tree_sitter_unreal_cpp` is the `add_grammar(..., UPSTREAM_SYMBOL <name>)` keyword — reusable for any future fork that retains its parent's exported name.

## Adding a New Language

The grammar list is hardcoded in CMake (fetched at build time) and the extension map is hardcoded in C++. Adding a language requires three edits and a rebuild:

1. **Declare and fetch the grammar source in `cmake/grammars.cmake`:**
   ```cmake
   FetchContent_Declare(ts-foo
       URL      https://github.com/owner/tree-sitter-foo/archive/refs/tags/v1.0.0.tar.gz
       URL_HASH SHA256=<sha256 of the tarball>
   )
   fetch_grammar(ts-foo)
   add_grammar(foo "${ts-foo_SOURCE_DIR}")
   ```
   Grammars are fetched as hash-pinned GitHub source archives, not git clones (single fast HTTP download, reproducible, CI-cacheable). For untagged upstreams, pin a commit SHA: `https://github.com/owner/tree-sitter-foo/archive/<sha>.tar.gz`.
   `add_grammar(foo …)` builds `grammars/foo/tree-sitter-foo.dll` and copies upstream `queries/highlights.scm` if no local override exists at `grammars/foo/highlights.scm`.

2. **Map extensions to the language in `src/plugin_colorizer/language/path_to_language.h`:**
   ```cpp
   constexpr ExtEntry kExtTable[] = {
       // ...
       { L"foo",  "foo" },
   };
   ```
   Add an entry to `kDefaultDetectString` (in `src/plugin_colorizer/window/colorizer_host_adapter.cpp`) so Total Commander knows to route `.foo` files to the colorizer, and mirror it in the shipped `config/wlx-listerine-colorizer.toml` `extensions`/`detect_string`:
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
