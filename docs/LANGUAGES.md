# Language Support

## Shipped Grammars

The colorizer plugin ships with grammars for:

| Language | Grammar DLL | Extensions |
|----------|------------|------------|
| C | `tree-sitter-c.dll` | `.c`, `.h` |
| JSON | `tree-sitter-json.dll` | `.json` |
| Python | `tree-sitter-python.dll` | `.py` |

Files with other extensions are displayed as plain text with line numbers and whitespace markers, but without syntax highlighting.

## Adding More Languages

The colorizer uses [tree-sitter](https://tree-sitter.github.io/) grammars. To add support for a new language:

1. **Get the grammar DLL.** Either:
   - Build it from a tree-sitter grammar repository (see below)
   - Download a pre-built DLL if available

2. **Drop it in `grammars/`.** Name it `tree-sitter-<language>.dll` (e.g., `tree-sitter-rust.dll`).

3. **Add the extension mapping** in `wlx-listerine-colorizer.toml`:
   ```toml
   [general]
   extensions = ["c", "cpp", "h", "hpp", "py", "json", "rs"]  # add "rs" for Rust
   ```

4. **Restart Total Commander.**

## Building a Grammar DLL

Requires CMake, MSVC, and the tree-sitter library (already available if you build the project from source).

1. Clone the grammar repository:
   ```bash
   git clone --depth 1 https://github.com/tree-sitter/tree-sitter-rust build/grammars/rust
   ```

2. Add it to `CMakeLists.txt` (or use the `add_grammar` function already defined there):
   ```cmake
   add_grammar(rust "${CMAKE_SOURCE_DIR}/build/grammars/rust")
   ```

3. Rebuild:
   ```bash
   cmake --preset conan-default
   cmake --build --preset conan-release
   ```

The DLL appears in `grammars/tree-sitter-rust.dll`.

## Tree-sitter Grammar Repositories

Common grammars available at:
- [tree-sitter-rust](https://github.com/tree-sitter/tree-sitter-rust)
- [tree-sitter-go](https://github.com/tree-sitter/tree-sitter-go)
- [tree-sitter-javascript](https://github.com/tree-sitter/tree-sitter-javascript)
- [tree-sitter-typescript](https://github.com/tree-sitter/tree-sitter-typescript)
- [tree-sitter-cpp](https://github.com/tree-sitter/tree-sitter-cpp)
- [tree-sitter-java](https://github.com/tree-sitter/tree-sitter-java)
- [tree-sitter-bash](https://github.com/tree-sitter/tree-sitter-bash)

Full list: [tree-sitter GitHub org](https://github.com/tree-sitter)
