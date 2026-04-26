# Unreal C++ Grammar Integration

## Problem

The colorizer plugin ships 25+ tree-sitter grammars covering C, C++, and most mainstream languages. Unreal Engine C++ files are highlighted by the standard `cpp` grammar today, which leaves Unreal-specific reflection macros (`UCLASS`, `UPROPERTY`, `UFUNCTION`, `GENERATED_BODY`, `MYPROJECT_API`, property specifiers like `Blueprintable`/`EditAnywhere`) parsed as ordinary identifiers — no semantic distinction in the rendered output.

The natural candidate is [`taku25/tree-sitter-unreal-cpp`](https://github.com/taku25/tree-sitter-unreal-cpp), an active fork of `tree-sitter/tree-sitter-cpp` that adds dedicated grammar nodes for Unreal macros. The fork is maintained (last upstream sync 2026-04-10) and a true superset of plain C++. However, its `grammar.js` declares `name: 'cpp'`, so the compiled DLL exports `tree_sitter_cpp()` — colliding with the standard cpp grammar's exported symbol and incompatible with this project's "directory name == exported symbol name" convention in `grammar_registry.cpp:91-94`.

A second wrinkle: the fork has zero releases or tags, so it must be SHA-pinned rather than version-tagged like every other grammar in `CMakeLists.txt`.

## Goals

1. Ship `unreal-cpp` as a second grammar alongside `cpp`, loadable by `GrammarRegistry` under the symbol name the registry expects (`tree_sitter_unreal_cpp`).
2. Resolve the symbol-name mismatch at build time without modifying upstream sources.
3. Provide a config opt-in (`[colorizer].cpp_grammar = "standard" | "unreal"`) that re-routes `.c/.h/.cpp/.cc/.cxx/.hpp/.hxx` to the Unreal grammar when set. Default behavior unchanged for non-Unreal users.
4. Ship a hand-written `grammars/unreal-cpp/highlights.scm` that inherits from `cpp` and adds Unreal-specific captures, since the upstream `queries/highlights.scm` covers only seven scopes.
5. Generalize the build-pipeline change so the next grammar fork that retains its parent's exported symbol is a one-liner.

## Non-goals

- Per-file content sniffing (`#include "*.generated.h"` detection, `UCLASS()` macro scan) or filesystem heuristics (`.uproject` parent-walk). Deferred until config-flip ergonomics prove insufficient in practice.
- Per-extension override map. The toggle applies to every C/C++ extension or none.
- Replacing `cpp` with `unreal-cpp` as the universal default. Reproducibility (`v0.23.4` tag) and bus-factor concerns make the toggle the right v1 surface.
- Upstream-version-tracking automation. SHA pin documented in `LANGUAGES.md`; manual bumps when the user wants newer Unreal coverage.
- Forking taku25's repo into the project's own GitHub org. Reserved as a fallback if upstream goes silent.

## Approach summary

**Build-time alias TU** (chosen over compile-flag rename and registry-side manifest):

When a grammar's upstream-exported symbol differs from the directory-derived symbol the registry will look up, generate a tiny C translation unit that exports a forwarder. The generated TU is the single mechanism; no runtime knowledge needed.

```c
// Generated as: build/.../grammars/unreal-cpp_alias.c
#include "tree_sitter/api.h"
extern const TSLanguage *tree_sitter_cpp(void);
__declspec(dllexport) const TSLanguage *tree_sitter_unreal_cpp(void) {
    return tree_sitter_cpp();
}
```

Both symbols are exported from the same DLL; the registry resolves `tree_sitter_unreal_cpp` exactly as it does for every other grammar. Upstream `parser.c` is unmodified.

**Runtime routing** (chosen over filesystem or content heuristics):

A single new function in the colorizer host adapter, called immediately after `ext_to_language` / `filename_to_language`, swaps `"cpp"` for `"unreal-cpp"` when the user has set `cpp_grammar = "unreal"` in their config and the Unreal grammar is actually present. If the grammar is missing or fails to load, the call falls through to `"cpp"` — no error popup, one trace line.

## Architecture

### New / modified files

```
CMakeLists.txt                              CHG: extend add_grammar() with UPSTREAM_SYMBOL,
                                                 declare ts-unreal-cpp, build unreal-cpp grammar
grammars/unreal-cpp/highlights.scm          NEW: ~50-line query, `; inherits: cpp` + Unreal captures

src/colorizer/colorizer_layout.h            CHG: add CppGrammar enum + cpp_grammar field
                                                 to ColorizerDisplayConfig
src/colorizer/colorizer_routing.h           NEW: header-only pure function apply_cpp_variant(...)
src/colorizer/colorizer_host_adapter.cpp    CHG: parse cpp_grammar from `[colorizer].cpp_grammar`
                                                 in ensure_theme(); call apply_cpp_variant() at
                                                 the two language-resolution sites

config/wlx-listerine-colorizer.toml         CHG: document new [colorizer] section + default value

tests/test_colorizer_grammars.cpp           CHG: add unreal-cpp loadability + parse-smoke + query tests
tests/test_colorizer_routing.cpp            NEW: apply_cpp_variant() unit tests
CMakeLists.txt                              CHG: register test_colorizer_routing.cpp in colorizer-tests

docs/LANGUAGES.md                           CHG: row in extension table, "Switching to Unreal C++" section
docs/CONFIGURATION.md                       CHG: cpp_grammar key documentation
CLAUDE.md                                   CHG: one-sentence note on the per-language variant pattern
```

### Build pipeline — `add_grammar` extension

Signature gains one optional keyword:

```cmake
# add_grammar(LANG SOURCE_DIR
#             [QUERY_DIR path]
#             [UPSTREAM_SYMBOL name])
#
# UPSTREAM_SYMBOL <name>: when set, the upstream grammar exports `name` rather than
# `tree_sitter_<LANG>` (with `-` -> `_`). Generates an alias translation unit that
# forwards `tree_sitter_<LANG>(...)` to `name(...)`.
```

Implementation outline inside `add_grammar` (before `add_library`):

```cmake
if(AG_UPSTREAM_SYMBOL)
    string(REPLACE "-" "_" _lang_underscored "${LANG}")
    set(_alias_path "${CMAKE_CURRENT_BINARY_DIR}/grammars/${LANG}_alias.c")
    file(WRITE "${_alias_path}"
"#include \"tree_sitter/api.h\"\n"
"extern const TSLanguage *${AG_UPSTREAM_SYMBOL}(void);\n"
"__declspec(dllexport) const TSLanguage *tree_sitter_${_lang_underscored}(void) {\n"
"    return ${AG_UPSTREAM_SYMBOL}();\n"
"}\n"
    )
    list(APPEND GRAMMAR_SOURCES "${_alias_path}")
endif()
```

The `tree_sitter::tree-sitter` Conan target supplies `tree_sitter/api.h` for the alias TU; the existing `target_include_directories(... PRIVATE "${SOURCE_DIR}/src")` is irrelevant here since we don't include parser-internal headers.

### FetchContent declaration

```cmake
FetchContent_Declare(ts-unreal-cpp
    GIT_REPOSITORY git@github.com:taku25/tree-sitter-unreal-cpp.git
    GIT_TAG        92eee7d        # 2026-04-10 — pinned SHA, no upstream tags
    GIT_SHALLOW    FALSE          # GIT_SHALLOW + arbitrary-SHA pin is unreliable
)
fetch_grammar(ts-unreal-cpp)
add_grammar(unreal-cpp "${ts-unreal-cpp_SOURCE_DIR}" UPSTREAM_SYMBOL tree_sitter_cpp)
```

### Local highlights override

`grammars/unreal-cpp/highlights.scm` (skeleton — exact node names verified against `taku25` `grammar.js` during implementation):

```scheme
; inherits: cpp

; Unreal reflection macros — recognized as keyword.directive
((identifier) @keyword.directive
 (#match? @keyword.directive
  "^(UCLASS|USTRUCT|UENUM|UINTERFACE|UFUNCTION|UPROPERTY|UPARAM|UDELEGATE|GENERATED_BODY|GENERATED_UCLASS_BODY|GENERATED_USTRUCT_BODY|DECLARE_LOG_CATEGORY_EXTERN|DEFINE_LOG_CATEGORY)$"))

; PROJECTNAME_API export macros
((identifier) @attribute
 (#match? @attribute "^[A-Z][A-Z0-9_]*_API$"))

; Property specifiers (Blueprintable, EditAnywhere, Category, ...)
; Captured against the grammar's dedicated unreal_specifier_* nodes when available;
; falls back to identifier-match otherwise.
```

`add_grammar` already prefers a local `grammars/<lang>/highlights.scm` over the upstream copy, so no copy step needs to be suppressed.

### Config schema

Adds one key to `config/wlx-listerine-colorizer.toml`:

```toml
[colorizer]
cpp_grammar = "unreal"   # "standard" (default) | "unreal"
```

Backed by an enum added to `ColorizerDisplayConfig` in `src/colorizer/colorizer_layout.h`:

```cpp
enum class CppGrammar { Standard, Unreal };

struct ColorizerDisplayConfig {
    // ... existing fields ...
    CppGrammar cpp_grammar = CppGrammar::Standard;
};
```

Parsed in `ensure_theme()` (`colorizer_host_adapter.cpp`) alongside the existing `[display]` keys, but under its own `[colorizer]` table to keep the section semantically clean (parsing/grammar selection isn't a "display" concern). Invalid value -> trace warning, defaults to `Standard`.

### Routing

A new pure function in `src/colorizer/colorizer_routing.h` (header-only, no globals — testable from a separate TU):

```cpp
// colorizer_routing.h
inline std::string apply_cpp_variant(const std::string& lang,
                                     CppGrammar variant,
                                     const Colorizer* colorizer) {
    if (lang == "cpp"
        && variant == CppGrammar::Unreal
        && colorizer && colorizer->supports("unreal-cpp"))
        return "unreal-cpp";
    return lang;
}
```

Call sites in `colorizer_host_adapter.cpp` (lines `360` and `1055`) wrap their existing language lookups:

```cpp
language = apply_cpp_variant(ext_to_language(vs->file_path),
                             g_display_cfg.cpp_grammar,
                             g_colorizer.get());
```

Single chokepoint, two call sites updated, no global access from the routing primitive.

## Error handling

| Failure mode | Behavior |
|---|---|
| `cpp_grammar = "unreal"` but DLL not built or fails to load | `supports("unreal-cpp")` returns false -> falls through to `"cpp"`. One `wlx_trace` line on first miss. No popup. |
| `unreal-cpp/highlights.scm` fails to compile | Registry returns `nullptr` for the query; colorizer renders plain text. Same as every other grammar. |
| `cpp_grammar = "klingon"` (invalid value) | TOML parser emits warning to trace, defaults to `Standard`. |
| Upstream `tree_sitter_cpp` symbol missing from forked sources (only possible if upstream renames its grammar) | Caught at link time when building the alias TU; build fails with a clear undefined-symbol error. Not a runtime concern. |

## Testing

### `colorizer-tests` additions (`tests/test_colorizer_grammars.cpp`)

1. **Loadability** — `GrammarRegistry::available_languages()` includes `"unreal-cpp"`; `get_grammar("unreal-cpp")` returns non-null. Validates the alias TU compiled into the DLL and exports the expected forwarder.
2. **Parse smoke** — parse a tiny snippet:
   ```cpp
   UCLASS()
   class FOO_API AMyActor : public AActor {
       GENERATED_BODY()
   };
   ```
   Assert the parsed tree's root has no `ERROR` nodes.
3. **Query loads** — `get_query("unreal-cpp")` returns non-null. Validates `; inherits: cpp` resolves and the scheme parses without error.

### Routing test (new file: `tests/test_colorizer_routing.cpp`)

Direct unit tests on `apply_cpp_variant(lang, variant, colorizer)`:

- `lang = "cpp"`, variant = Standard, colorizer with both grammars -> `"cpp"`.
- `lang = "cpp"`, variant = Unreal, colorizer with both grammars -> `"unreal-cpp"`.
- `lang = "cpp"`, variant = Unreal, colorizer without `unreal-cpp` -> `"cpp"` (fallback).
- `lang = "python"`, variant = Unreal -> `"python"` (variant is C++-only).
- `colorizer = nullptr`, variant = Unreal -> `"cpp"` (defensive null check).

The function takes the `Colorizer` as a pointer parameter so the test feeds either a real registry pointed at the project's `grammars/` dir or `nullptr` for the defensive case. No global state to mock.

### Visual regression

Optional. A new test case `28_unreal_cpp.<src>` could exercise the rendered output. Out of scope for v1; add later if Unreal coverage warrants it.

## Documentation

- **`docs/LANGUAGES.md`** — add a row to the extension table marked "(Unreal variant available, opt-in)"; new subsection "Switching to Unreal C++" pointing to the config key. Document the SHA-pin rationale next to the standard "Adding a New Language" instructions.
- **`docs/CONFIGURATION.md`** — entry for `cpp_grammar` under `[colorizer]`.
- **`CLAUDE.md`** — one sentence in the colorizer-core paragraph noting that some languages have multiple grammar variants selected by config (so future contributors recognize the pattern).

## Open questions / risks

- **R1.** taku25's grammar may rename or restructure nodes between the pinned SHA and any future bump. Mitigation: the highlights.scm uses defensive `(identifier) @scope (#match? ...)` patterns where node-naming is uncertain, falling back to identifier-text matching. Exact node-name captures verified during implementation against the pinned SHA's `grammar.js`.
- **R2.** SHA pinning means the Conan/CMake reproducibility story for this one grammar is weaker than the rest. Documented in `LANGUAGES.md`. Acceptable trade-off until upstream cuts a tag.
- **R3.** If taku25 abandons the fork, the SHA still works indefinitely (FetchContent caches), but plain-C++ parity drifts behind upstream. Detection: occasional manual diff against `tree-sitter/tree-sitter-cpp@HEAD`. Mitigation if it happens: fork-our-fork, sync ourselves, retarget GIT_REPOSITORY.
- **R4.** A user with `cpp_grammar = "unreal"` opening a non-Unreal C++ file gets the Unreal grammar applied. The grammar is a true superset, so parsing succeeds; the only visible effect is that no Unreal macros match. Acceptable. If users complain, content-sniff (deferred) is the answer.

## Acceptance criteria

- `cmake --build --preset conan-release` succeeds on a clean build directory.
- `grammars/unreal-cpp/tree-sitter-unreal-cpp.dll` is produced.
- `tests.exe` and `colorizer-tests.exe` pass with the new tests in place.
- Setting `[colorizer].cpp_grammar = "unreal"` and opening a `.cpp` file with `UCLASS()` shows the macro highlighted distinctly from regular identifiers.
- Default config (no `cpp_grammar` key, or set to `"standard"`) produces visually identical output to today.

## References

- Upstream fork: <https://github.com/taku25/tree-sitter-unreal-cpp>
- Symbol-collision diagnosis: `src/colorizer/grammar_registry.cpp:91-94`
- Existing language-routing chokepoints: `src/colorizer/colorizer_host_adapter.cpp:107-173, 360, 1055`
- Existing local-highlights pattern: `grammars/cpp/highlights.scm` (`; inherits: c`)
