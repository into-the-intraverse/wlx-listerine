# Language Support

## Supported Languages (Lexilla)

The colorizer plugin (`wlx-listerine-colorizer`) uses [Lexilla](https://www.scintilla.org/Lexilla.html) (Scintilla's GUI-independent lexer library), statically linked into `wlx-listerine-core.dll`. 19 languages are fully syntax-highlighted:

| Language     | Extensions                              | Notes |
|--------------|-----------------------------------------|-------|
| Bash         | `.sh`, `.bash`, `.zsh`, shell dotfiles (`.bashrc`, `.bash_profile`, `.zshrc`, `.profile`, `.envrc`, …) | |
| C / C++      | `.c`, `.h`, `.cpp`, `.cc`, `.cxx`, `.hpp`, `.hxx` | Shares Lexilla's `cpp` lexer |
| C#           | `.cs`                                   | Shares Lexilla's `cpp` lexer |
| CSS          | `.css`                                  | |
| HTML         | `.html`, `.htm`                         | |
| Java         | `.java`                                 | Shares Lexilla's `cpp` lexer |
| JavaScript   | `.js`, `.mjs`, `.cjs`, `.jsx`           | Shares Lexilla's `cpp` lexer |
| JSON         | `.json`, `.jsonc`, `Pipfile.lock`, `bun.lock` | |
| Lua          | `.lua`                                  | |
| PHP          | `.php`                                  | |
| PowerShell   | `.ps1`, `.psm1`, `.psd1`                | |
| Python       | `.py`, `.pyi`                           | |
| Rust         | `.rs`                                   | |
| SQL          | `.sql`                                  | |
| TOML         | `.toml`, `Pipfile`, `uv.lock`, `poetry.lock` | |
| TypeScript   | `.ts`, `.tsx`, `.mts`                   | Shares Lexilla's `cpp` lexer |
| XML          | `.xml`, `.svg`, VS/MSBuild files (`.vcxproj`, `.csproj`, `.fsproj`, `.vbproj`, `.proj`, `.props`, `.targets`, `.filters`, `.slnx`, `.xaml`, `.resx`) | |
| YAML         | `.yaml`, `.yml`                         | |

The extension → language map lives in `src/plugin_colorizer/language/path_to_language.h`; filename special cases (lockfiles) in its `filename_to_language()`.

## Plain-Text Fallback

7 file types are recognized and opened as fully interactive plain text (line numbers, whitespace markers, selection, search) but receive no syntax color highlighting — no Lexilla lexer covers them:

| File type      | Extensions / filenames |
|----------------|------------------------|
| Go             | `.go` |
| Dockerfile     | `.dockerfile`, `Dockerfile`, `Containerfile`, `*dockerfile*` |
| Vim            | `.vim`, `.vimrc`, `.nvimrc` |
| gitignore      | `.gitignore`, `.dockerignore`, `.npmignore` |
| git-config     | `.gitconfig`, `.gitmodules` |
| git-rebase     | `git-rebase-todo` |
| gitattributes  | `.gitattributes` |

Files with extensions not in either list are also displayed as plain text.

### Per-file language override

Right-click → language submenu forces a specific language for the current view (session-only; "Auto-detect" restores extension-based routing). Useful for files with misleading or missing extensions.

### Plugin boundary

`.md` and `.markdown` files are owned by the sibling plugin `wlx-listerine-md`, which renders rich markdown rather than tokenized syntax. The colorizer claims the extensions only as a plain-text fallback — its language map intentionally has no markdown entry.
