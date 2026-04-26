# Unreal C++ Grammar Integration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `unreal-cpp` as a second tree-sitter grammar alongside `cpp`, opt-in via `[colorizer].cpp_grammar = "unreal"` in the colorizer TOML, with a build-time alias TU resolving the upstream `tree_sitter_cpp()` symbol mismatch.

**Architecture:** Extend the existing `add_grammar(...)` CMake helper with one optional `UPSTREAM_SYMBOL` keyword that generates a 4-line C alias TU forwarding `tree_sitter_<LANG>()` to the upstream symbol. The alias TU is added to the grammar DLL's source set, so the DLL exports both names. A new header-only pure function `apply_cpp_variant(...)` swaps `"cpp"` for `"unreal-cpp"` at the two language-resolution sites in the colorizer host adapter, gated by a `CppGrammar` enum on `ColorizerDisplayConfig`.

**Tech Stack:** CMake 3.20+, MSVC C++20, tree-sitter 0.x (Conan), doctest, toml++ (Conan), Win32 DLLs.

**Source spec:** `docs/superpowers/specs/2026-04-26-unreal-cpp-grammar-design.md` (commit `817c7ff`).

---

## File Structure

### Created

- `grammars/unreal-cpp/highlights.scm` — Hand-written query, `; inherits: cpp` plus Unreal reflection-macro and PROJECTNAME_API captures. Authored, not generated. Lives next to the eventual DLL.
- `src/colorizer/colorizer_routing.h` — Header-only. Single `inline` function `apply_cpp_variant(lang, variant, colorizer)`. No globals, no state — pure.
- `tests/test_colorizer_routing.cpp` — doctest TUs covering `apply_cpp_variant`. Wired into the `colorizer-tests` target.

### Modified

- `CMakeLists.txt` — Extend `add_grammar()` with `UPSTREAM_SYMBOL <name>` keyword that generates an alias TU; declare `ts-unreal-cpp` via FetchContent; build `unreal-cpp` grammar; add `tests/test_colorizer_routing.cpp` to `colorizer-tests`.
- `src/colorizer/colorizer_layout.h` — Add `enum class CppGrammar { Standard, Unreal }` and `CppGrammar cpp_grammar = CppGrammar::Standard;` field on `ColorizerDisplayConfig`.
- `src/colorizer/colorizer_host_adapter.cpp` — Parse `[colorizer].cpp_grammar` from TOML in `ensure_theme()`; wrap the two `ext_to_language` / `filename_to_language` results with `apply_cpp_variant(...)` (lines 358-360 and 1054-1055).
- `tests/test_colorizer_grammars.cpp` — Add `Grammar: unreal-cpp` test case (loadability + parse-smoke + query-loads).
- `config/wlx-listerine-colorizer.toml` — Document new `[colorizer]` section with `cpp_grammar` key.
- `docs/LANGUAGES.md` — Note the Unreal variant in extension table; new "Switching to Unreal C++" subsection.
- `docs/CONFIGURATION.md` — Document the new TOML key.
- `CLAUDE.md` — One-sentence note on the per-language variant pattern in the colorizer-core paragraph.

---

## Task 1: Extend `add_grammar` with `UPSTREAM_SYMBOL` parameter

**Files:**
- Modify: `CMakeLists.txt` — the `add_grammar` function (currently lines ~196-234).

- [ ] **Step 1: Open `CMakeLists.txt` and locate the `add_grammar` function.**

It begins with `function(add_grammar LANG SOURCE_DIR)` and ends with `endfunction()` after the highlights-copy logic.

- [ ] **Step 2: Replace the `cmake_parse_arguments` line and add alias-TU generation.**

The function's current opening is:

```cmake
function(add_grammar LANG SOURCE_DIR)
    cmake_parse_arguments(AG "" "QUERY_DIR" "" ${ARGN})
    if(NOT EXISTS "${SOURCE_DIR}/src/parser.c")
```

Change the `cmake_parse_arguments` call to accept the new keyword and add the alias-TU block right before `add_library(...)`. After the change, the head of the function looks like:

```cmake
function(add_grammar LANG SOURCE_DIR)
    cmake_parse_arguments(AG "" "QUERY_DIR;UPSTREAM_SYMBOL" "" ${ARGN})
    if(NOT EXISTS "${SOURCE_DIR}/src/parser.c")
        message(STATUS "Grammar ${LANG}: parser.c not found in ${SOURCE_DIR}/src — skipping")
        return()
    endif()
    set(GRAMMAR_SOURCES "${SOURCE_DIR}/src/parser.c")
    if(EXISTS "${SOURCE_DIR}/src/scanner.c")
        list(APPEND GRAMMAR_SOURCES "${SOURCE_DIR}/src/scanner.c")
    elseif(EXISTS "${SOURCE_DIR}/src/scanner.cc")
        list(APPEND GRAMMAR_SOURCES "${SOURCE_DIR}/src/scanner.cc")
    endif()

    # When upstream exports a different symbol than tree_sitter_<LANG>, generate
    # an alias TU that forwards tree_sitter_<LANG>() to the upstream symbol.
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

    add_library(tree-sitter-${LANG} SHARED ${GRAMMAR_SOURCES})
    target_include_directories(tree-sitter-${LANG} PRIVATE "${SOURCE_DIR}/src")
    target_link_libraries(tree-sitter-${LANG} PRIVATE tree-sitter::tree-sitter)
    set_target_properties(tree-sitter-${LANG} PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/grammars/${LANG}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/grammars/${LANG}"
    )
```

(The rest of the function — local-query check and POST_BUILD copy — stays unchanged.)

- [ ] **Step 3: Reconfigure CMake to verify no syntax errors.**

Run:

```bash
cmake --preset conan-default
```

Expected: configure completes successfully. No grammar uses `UPSTREAM_SYMBOL` yet, so build behavior is unchanged.

- [ ] **Step 4: Commit.**

```bash
git add CMakeLists.txt
git commit -m "build: add UPSTREAM_SYMBOL keyword to add_grammar() for forked grammars"
```

---

## Task 2: Fetch and build the `unreal-cpp` grammar

**Files:**
- Modify: `CMakeLists.txt` — append a `FetchContent_Declare`, `fetch_grammar`, and `add_grammar` call.

- [ ] **Step 1: Add the `FetchContent_Declare` block.**

In `CMakeLists.txt`, find the last `FetchContent_Declare(ts-...)` block (the one for `ts-gitignore` near line ~368). Immediately after that block (and before the `# Download all grammar sources` comment), append:

```cmake
FetchContent_Declare(ts-unreal-cpp
    GIT_REPOSITORY git@github.com:taku25/tree-sitter-unreal-cpp.git
    GIT_TAG        92eee7d        # 2026-04-10 — pinned SHA, no upstream tags
    GIT_SHALLOW    FALSE          # GIT_SHALLOW + arbitrary-SHA pin is unreliable
)
```

- [ ] **Step 2: Add the `fetch_grammar` call.**

In the block of `fetch_grammar(...)` calls (currently ending with `fetch_grammar(ts-gitignore)` near line ~400), append:

```cmake
fetch_grammar(ts-unreal-cpp)
```

- [ ] **Step 3: Add the `add_grammar` call.**

After the last special-grammar `add_grammar(...)` call (currently `add_grammar(gitignore "${ts-gitignore_SOURCE_DIR}")` near line ~438), append:

```cmake
# Unreal C++ — fork that exports tree_sitter_cpp; alias TU bridges the name.
add_grammar(unreal-cpp "${ts-unreal-cpp_SOURCE_DIR}" UPSTREAM_SYMBOL tree_sitter_cpp)
```

- [ ] **Step 4: Configure and build.**

```bash
cmake --preset conan-default
cmake --build --preset conan-release --target tree-sitter-unreal-cpp
```

Expected: `git clone` of the fork succeeds; alias TU is generated at `build/grammars/unreal-cpp_alias.c`; build produces `grammars/unreal-cpp/tree-sitter-unreal-cpp.dll`.

- [ ] **Step 5: Verify the DLL exports both symbols.**

Run:

```bash
dumpbin //EXPORTS grammars/unreal-cpp/tree-sitter-unreal-cpp.dll | grep -E "tree_sitter_(cpp|unreal_cpp)"
```

(If `dumpbin` is not on PATH, use the Visual Studio Developer Command Prompt or `llvm-readobj --coff-exports`.)

Expected: both `tree_sitter_cpp` and `tree_sitter_unreal_cpp` appear in the export table.

- [ ] **Step 6: Commit.**

```bash
git add CMakeLists.txt
git commit -m "build: add unreal-cpp grammar (taku25 fork, SHA-pinned)"
```

---

## Task 3: Author `grammars/unreal-cpp/highlights.scm`

**Files:**
- Create: `grammars/unreal-cpp/highlights.scm`

- [ ] **Step 1: Inspect the upstream `grammar.js` to confirm Unreal-specific node names.**

```bash
grep -nE "name:|unreal_|UCLASS|GENERATED_BODY|specifier" build/_deps/ts-unreal-cpp-src/grammar.js | head -40
```

Note any `unreal_specifier_*` or `unreal_*_macro` node names that the highlight queries can target directly. If none, fall back to `(identifier) (#match? ...)` text-based capture.

- [ ] **Step 2: Create the file with this content.**

`grammars/unreal-cpp/highlights.scm`:

```scheme
; inherits: cpp

; Unreal reflection macros — keyword.directive distinguishes them from regular
; function calls. The cpp/c inherited queries treat these as plain @function calls;
; this override re-tags them.
((identifier) @keyword.directive
 (#match? @keyword.directive
  "^(UCLASS|USTRUCT|UENUM|UINTERFACE|UFUNCTION|UPROPERTY|UPARAM|UDELEGATE|GENERATED_BODY|GENERATED_UCLASS_BODY|GENERATED_USTRUCT_BODY|DECLARE_LOG_CATEGORY_EXTERN|DECLARE_LOG_CATEGORY_CLASS|DEFINE_LOG_CATEGORY|DECLARE_DYNAMIC_MULTICAST_DELEGATE|DECLARE_DYNAMIC_DELEGATE|DECLARE_MULTICAST_DELEGATE|DECLARE_DELEGATE)$"))

; PROJECTNAME_API export macros (e.g. MYGAME_API, MYPLUGIN_API). Captured as
; @attribute so themes can render them like Rust attributes / C# annotations.
((identifier) @attribute
 (#match? @attribute "^[A-Z][A-Z0-9_]+_API$"))

; Common Unreal property/function specifiers found inside UPROPERTY()/UFUNCTION() args.
; If the upstream grammar provides dedicated unreal_specifier_* nodes (verified in Step 1),
; replace the identifier-match below with structural captures.
((identifier) @attribute.builtin
 (#match? @attribute.builtin
  "^(BlueprintCallable|BlueprintReadOnly|BlueprintReadWrite|BlueprintImplementableEvent|BlueprintNativeEvent|BlueprintPure|EditAnywhere|EditDefaultsOnly|EditInstanceOnly|VisibleAnywhere|VisibleDefaultsOnly|VisibleInstanceOnly|Category|Replicated|ReplicatedUsing|Transient|SaveGame|Config|GlobalConfig|Localized|SkipSerialization|Meta|ClassGroup|HideCategories|ShowCategories|Within|Blueprintable|NotBlueprintable|MinimalAPI|customConstructor|noexport|placeable|notplaceable|hidedropdown|abstract)$"))
```

- [ ] **Step 3: Rebuild — confirm the local highlights file is preferred over upstream.**

```bash
cmake --build --preset conan-release --target tree-sitter-unreal-cpp
```

Expected: build succeeds, no POST_BUILD copy of upstream highlights.scm (because `grammars/unreal-cpp/highlights.scm` exists; see `add_grammar` lines 219-220 in `CMakeLists.txt`).

Verify that the file in the source tree is the one we just wrote:

```bash
diff grammars/unreal-cpp/highlights.scm build/_deps/ts-unreal-cpp-src/queries/highlights.scm
```

Expected: differences (proves our local file wasn't overwritten).

- [ ] **Step 4: Commit.**

```bash
git add grammars/unreal-cpp/highlights.scm
git commit -m "colorizer: add unreal-cpp highlights (inherits cpp + Unreal scopes)"
```

---

## Task 4: Add `CppGrammar` enum to `ColorizerDisplayConfig`

**Files:**
- Modify: `src/colorizer/colorizer_layout.h`

- [ ] **Step 1: Open `src/colorizer/colorizer_layout.h` and add the enum.**

Just below the existing `enum class ShowWhitespace` (line 10), add:

```cpp
enum class CppGrammar { Standard, Unreal };
```

- [ ] **Step 2: Add the field to `ColorizerDisplayConfig`.**

Inside the struct (currently lines 12-20), append a new field:

```cpp
struct ColorizerDisplayConfig {
    bool line_numbers = true;
    bool word_wrap = false;
    int tab_width = 4;
    float line_height_factor = 1.4f;
    ShowWhitespace show_whitespace = ShowWhitespace::Boundary;
    bool show_indent_guides = true;
    bool highlight_trailing = true;
    CppGrammar cpp_grammar = CppGrammar::Standard;
};
```

- [ ] **Step 3: Build to confirm no breakage.**

```bash
cmake --build --preset conan-release
```

Expected: clean build. No call sites change yet.

- [ ] **Step 4: Commit.**

```bash
git add src/colorizer/colorizer_layout.h
git commit -m "colorizer: add CppGrammar enum to ColorizerDisplayConfig"
```

---

## Task 5: Create `colorizer_routing.h` with `apply_cpp_variant` (TDD)

**Files:**
- Create: `tests/test_colorizer_routing.cpp`
- Modify: `CMakeLists.txt` — add new test file to `colorizer-tests` target.
- Create: `src/colorizer/colorizer_routing.h`

- [ ] **Step 1: Write the failing test first.**

Create `tests/test_colorizer_routing.cpp`:

```cpp
#include <doctest/doctest.h>
#include <filesystem>
#include "colorizer.h"
#include "colorizer_layout.h"
#include "colorizer_routing.h"

namespace fs = std::filesystem;

static const bool grammars_present =
    fs::exists("grammars/cpp/tree-sitter-cpp.dll") &&
    fs::exists("grammars/unreal-cpp/tree-sitter-unreal-cpp.dll");

TEST_CASE("apply_cpp_variant: standard -> cpp"
    * doctest::skip(!grammars_present)) {
    Colorizer c(L"grammars", L"config/themes");
    CHECK(apply_cpp_variant("cpp", CppGrammar::Standard, &c) == "cpp");
}

TEST_CASE("apply_cpp_variant: unreal -> unreal-cpp"
    * doctest::skip(!grammars_present)) {
    Colorizer c(L"grammars", L"config/themes");
    CHECK(apply_cpp_variant("cpp", CppGrammar::Unreal, &c) == "unreal-cpp");
}

TEST_CASE("apply_cpp_variant: unreal but grammar missing -> falls back to cpp") {
    // Point at a non-existent grammar dir so the colorizer's registry has no grammars.
    Colorizer c(L"nonexistent_grammars_dir", L"config/themes");
    CHECK(apply_cpp_variant("cpp", CppGrammar::Unreal, &c) == "cpp");
}

TEST_CASE("apply_cpp_variant: non-cpp lang is untouched"
    * doctest::skip(!grammars_present)) {
    Colorizer c(L"grammars", L"config/themes");
    CHECK(apply_cpp_variant("python", CppGrammar::Unreal, &c) == "python");
    CHECK(apply_cpp_variant("rust", CppGrammar::Standard, &c) == "rust");
}

TEST_CASE("apply_cpp_variant: null colorizer is defensive") {
    CHECK(apply_cpp_variant("cpp", CppGrammar::Unreal, nullptr) == "cpp");
    CHECK(apply_cpp_variant("cpp", CppGrammar::Standard, nullptr) == "cpp");
}
```

- [ ] **Step 2: Wire the test file into `colorizer-tests`.**

In `CMakeLists.txt`, find the `add_executable(colorizer-tests ...)` block (currently around line 441) and add the new test file to its source list:

```cmake
add_executable(colorizer-tests
    tests/test_main.cpp
    tests/test_colorizer.cpp
    tests/test_colorizer_helix_theme.cpp
    tests/test_colorizer_grammar.cpp
    tests/test_colorizer_query_highlighter.cpp
    tests/test_colorizer_grammars.cpp
    tests/test_colorizer_routing.cpp
)
```

- [ ] **Step 3: Run the build to confirm the test fails to compile (header doesn't exist yet).**

```bash
cmake --build --preset conan-release --target colorizer-tests
```

Expected: build fails with `cannot open include file: 'colorizer_routing.h'` (or equivalent MSVC message). This is the failing-test red light.

- [ ] **Step 4: Create the header.**

`src/colorizer/colorizer_routing.h`:

```cpp
#pragma once

#include <string>
#include "colorizer.h"
#include "colorizer_layout.h"

// Pure routing primitive: when the user opts into the Unreal C++ grammar via
// [colorizer].cpp_grammar = "unreal" AND the unreal-cpp grammar is actually
// loadable, swap "cpp" for "unreal-cpp". Falls back to "cpp" if the grammar
// is missing. Non-cpp languages are passed through unchanged.
inline std::string apply_cpp_variant(const std::string& lang,
                                     CppGrammar variant,
                                     const Colorizer* colorizer) {
    if (lang == "cpp"
        && variant == CppGrammar::Unreal
        && colorizer != nullptr
        && colorizer->supports("unreal-cpp")) {
        return "unreal-cpp";
    }
    return lang;
}
```

- [ ] **Step 5: Rebuild and run the test.**

```bash
cmake --build --preset conan-release --target colorizer-tests
./build/Release/colorizer-tests.exe --test-case-exclude="*"  # smoke: confirm it builds
./build/Release/colorizer-tests.exe -tc="apply_cpp_variant*"
```

Expected: all five `apply_cpp_variant*` test cases pass (one is unconditional, four require grammars to be present and skip otherwise).

- [ ] **Step 6: Commit.**

```bash
git add src/colorizer/colorizer_routing.h tests/test_colorizer_routing.cpp CMakeLists.txt
git commit -m "colorizer: add apply_cpp_variant routing primitive (header-only, tested)"
```

---

## Task 6: Wire `apply_cpp_variant` into the colorizer host adapter

**Files:**
- Modify: `src/colorizer/colorizer_host_adapter.cpp`

- [ ] **Step 1: Add the include.**

Near the top of `colorizer_host_adapter.cpp` (alongside other colorizer/* includes — search for `#include "colorizer.h"`), add:

```cpp
#include "colorizer_routing.h"
```

- [ ] **Step 2: Parse the new TOML key in `ensure_theme()`.**

Find the `ensure_theme()` function (currently around line 252). After the existing `[display]` parse block (currently lines 263-282) but still inside the `try { ... }` block, append:

```cpp
            // [colorizer].cpp_grammar — "standard" (default) | "unreal"
            if (auto v = tbl["colorizer"]["cpp_grammar"].value<std::string>()) {
                if (*v == "unreal") {
                    g_display_cfg.cpp_grammar = CppGrammar::Unreal;
                } else if (*v == "standard") {
                    g_display_cfg.cpp_grammar = CppGrammar::Standard;
                } else {
                    WLX_TRACE(L"unknown cpp_grammar value '%hs', defaulting to standard",
                              v->c_str());
                    g_display_cfg.cpp_grammar = CppGrammar::Standard;
                }
            }
```

(`WLX_TRACE` is already in scope — `colorizer_host_adapter.cpp` includes `wlx_trace.h` near the top, see lines 37-38.)

- [ ] **Step 3: Update the first language-resolution call site (around line 358).**

Find this block:

```cpp
    std::string language = ext_to_language(vs->file_path);
    if (language.empty())
        language = filename_to_language(vs->file_path);
    vs->cached_colors = {};
```

Replace with:

```cpp
    std::string language = ext_to_language(vs->file_path);
    if (language.empty())
        language = filename_to_language(vs->file_path);
    language = apply_cpp_variant(language, g_display_cfg.cpp_grammar, g_colorizer.get());
    vs->cached_colors = {};
```

- [ ] **Step 4: Update the second language-resolution call site (around line 1054).**

Find this block:

```cpp
            // Re-colorize with new palette (no file re-read)
            std::string language = ext_to_language(vs->file_path);
            if (language.empty()) language = filename_to_language(vs->file_path);
            vs->cached_colors = {};
```

Replace with:

```cpp
            // Re-colorize with new palette (no file re-read)
            std::string language = ext_to_language(vs->file_path);
            if (language.empty()) language = filename_to_language(vs->file_path);
            language = apply_cpp_variant(language, g_display_cfg.cpp_grammar, g_colorizer.get());
            vs->cached_colors = {};
```

- [ ] **Step 5: Build and run all tests.**

```bash
cmake --build --preset conan-release
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

Expected: all tests pass. The host adapter wiring isn't directly unit-tested, but the routing primitive (Task 5) is.

- [ ] **Step 6: Commit.**

```bash
git add src/colorizer/colorizer_host_adapter.cpp
git commit -m "colorizer: route cpp -> unreal-cpp when [colorizer].cpp_grammar=unreal"
```

---

## Task 7: Add `unreal-cpp` grammar tests to `test_colorizer_grammars.cpp`

**Files:**
- Modify: `tests/test_colorizer_grammars.cpp`

- [ ] **Step 1: Append a new `TEST_CASE` for unreal-cpp.**

At the end of `tests/test_colorizer_grammars.cpp`, add:

```cpp
// --- Grammar: Unreal C++ ---
TEST_CASE("Grammar: unreal-cpp"
    * doctest::skip(!std::filesystem::exists("grammars/unreal-cpp/tree-sitter-unreal-cpp.dll"))) {
    Colorizer c(L"grammars", L"config/themes");

    SUBCASE("registry exposes unreal-cpp") {
        CHECK(c.supports("unreal-cpp"));
        auto langs = c.available_languages();
        CHECK(std::find(langs.begin(), langs.end(), std::string("unreal-cpp")) != langs.end());
    }

    SUBCASE("parses Unreal-flavored snippet without errors") {
        verify_colorize(c, R"(// Unreal-flavored sample
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyActor.generated.h"

UCLASS(Blueprintable)
class FOO_API AMyActor : public AActor {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
    float Health = 100.0f;

    UFUNCTION(BlueprintCallable, Category = "Combat")
    void TakeDamage(float Amount);
};
)", "unreal-cpp");
    }

    SUBCASE("highlights query loads (inherits cpp resolves)") {
        // colorize() exercises the full query path; if the inherits chain or
        // any inherited rule fails to compile, spans would be empty or
        // colorize() would return early. verify_colorize already asserts
        // non-empty + sorted + non-overlapping spans.
        auto result = c.colorize("class A {};", "unreal-cpp", false);
        CHECK_FALSE(result.spans.empty());
    }
}
```

(`<algorithm>` is needed for `std::find`. Add `#include <algorithm>` near the top of the file if not already present — check existing includes first.)

- [ ] **Step 2: Build and run the test.**

```bash
cmake --build --preset conan-release --target colorizer-tests
./build/Release/colorizer-tests.exe -tc="Grammar: unreal-cpp"
```

Expected: PASS, three subcases pass.

- [ ] **Step 3: Run the full colorizer test suite to confirm no regressions.**

```bash
./build/Release/colorizer-tests.exe
```

Expected: all tests pass.

- [ ] **Step 4: Commit.**

```bash
git add tests/test_colorizer_grammars.cpp
git commit -m "tests: add unreal-cpp grammar loadability + parse-smoke + query tests"
```

---

## Task 8: Document the new key in the default colorizer TOML

**Files:**
- Modify: `config/wlx-listerine-colorizer.toml`

- [ ] **Step 1: Inspect the current TOML.**

```bash
cat config/wlx-listerine-colorizer.toml
```

Note where the existing `[display]` section ends.

- [ ] **Step 2: Append a new `[colorizer]` section.**

At the end of the file, append:

```toml

# C++ grammar selection.
#
# "standard" (default) — use tree-sitter/tree-sitter-cpp for all .c/.h/.cpp/.cc/.cxx/.hpp/.hxx.
# "unreal"             — use the taku25/tree-sitter-unreal-cpp fork, which adds
#                        dedicated parsing and highlighting for UCLASS, UPROPERTY,
#                        UFUNCTION, GENERATED_BODY, PROJECTNAME_API export macros,
#                        and Blueprint property specifiers.
#
# The "unreal" variant is a strict superset of plain C++: non-Unreal files still
# parse correctly, they just have nothing extra to highlight.
[colorizer]
cpp_grammar = "standard"
```

- [ ] **Step 3: Smoke test by toggling it and reloading.**

This step is manual / optional in CI. To verify locally, set `cpp_grammar = "unreal"` and open a file containing `UCLASS()` with the colorizer plugin. Confirm the macro is highlighted distinctly.

- [ ] **Step 4: Reset the value to `"standard"` for the committed default.**

Verify the file ends with `cpp_grammar = "standard"` before committing.

- [ ] **Step 5: Commit.**

```bash
git add config/wlx-listerine-colorizer.toml
git commit -m "colorizer: document [colorizer].cpp_grammar key (default=standard)"
```

---

## Task 9: Update documentation

**Files:**
- Modify: `docs/LANGUAGES.md`
- Modify: `docs/CONFIGURATION.md`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update `docs/LANGUAGES.md` extension table.**

Find the row for C / C++ in the extension table (currently around line 10):

```markdown
| C / C++      | `.c`, `.h`, `.cpp`, `.cc`, `.cxx`, `.hpp`, `.hxx` (all parsed by tree-sitter-cpp) |
```

Change to:

```markdown
| C / C++      | `.c`, `.h`, `.cpp`, `.cc`, `.cxx`, `.hpp`, `.hxx` (default: tree-sitter-cpp; opt-in Unreal variant — see below) |
```

- [ ] **Step 2: Add a "Switching to Unreal C++" subsection to `docs/LANGUAGES.md`.**

Insert this section just before "## Adding a New Language":

```markdown
## Switching to Unreal C++

Unreal Engine's reflection macros (`UCLASS`, `UPROPERTY`, `UFUNCTION`, `GENERATED_BODY`, `PROJECTNAME_API`, Blueprint property specifiers) parse as ordinary identifiers under the standard C++ grammar. The colorizer ships a second, opt-in grammar from [`taku25/tree-sitter-unreal-cpp`](https://github.com/taku25/tree-sitter-unreal-cpp) that adds dedicated parsing for these.

Enable it in `config/wlx-listerine-colorizer.toml`:

\`\`\`toml
[colorizer]
cpp_grammar = "unreal"
\`\`\`

Restart Total Commander. All C/C++ files now route to the Unreal grammar. The Unreal grammar is a strict superset of plain C++, so non-Unreal files continue to parse correctly — they just have no extra macros to highlight.

The fork is **SHA-pinned** in `CMakeLists.txt` (no upstream releases yet). Bumping it is a manual edit of `GIT_TAG`. The build-side mechanism that resolves the fork's `tree_sitter_cpp()` symbol export to `tree_sitter_unreal_cpp` is the `add_grammar(..., UPSTREAM_SYMBOL <name>)` keyword — reusable for any future fork that retains its parent's exported name.
```

(Replace the `\`\`\`toml ... \`\`\`` literal backticks with real triple backticks when editing the file. The escaping above is just so this plan renders correctly.)

- [ ] **Step 3: Update `docs/CONFIGURATION.md`.**

Find the section that documents the colorizer TOML keys. Append a `[colorizer]` subsection:

```markdown
### `[colorizer]`

| Key            | Type     | Default      | Description |
|----------------|----------|--------------|-------------|
| `cpp_grammar`  | string   | `"standard"` | Which tree-sitter grammar handles `.c/.h/.cpp/.cc/.cxx/.hpp/.hxx`. `"standard"` uses upstream tree-sitter-cpp; `"unreal"` uses the taku25 Unreal-aware fork. See [LANGUAGES.md → Switching to Unreal C++](LANGUAGES.md#switching-to-unreal-c). |
```

(If `docs/CONFIGURATION.md` doesn't currently have a colorizer section, add a new top-level "## wlx-listerine-colorizer" heading containing this table.)

- [ ] **Step 4: Update `CLAUDE.md`.**

Find the colorizer-core paragraph (currently the "**colorizer-core (static lib)**" heading). At the end of the existing description, append one sentence:

```markdown
Some languages support multiple grammar variants selected at runtime via TOML config (e.g. `[colorizer].cpp_grammar = "standard" | "unreal"` swaps standard tree-sitter-cpp for taku25's Unreal-aware fork via a build-time alias TU; the routing primitive is `apply_cpp_variant(...)` in `src/colorizer/colorizer_routing.h`).
```

- [ ] **Step 5: Commit.**

```bash
git add docs/LANGUAGES.md docs/CONFIGURATION.md CLAUDE.md
git commit -m "docs: document Unreal C++ grammar opt-in and the variant routing pattern"
```

---

## Final verification

- [ ] **Step 1: Clean build from scratch.**

```bash
rm -rf build
conan install . --output-folder=build --build=missing -s build_type=Release -s compiler.cppstd=20
cmake --preset conan-default
cmake --build --preset conan-release
```

Expected: clean build succeeds; `grammars/unreal-cpp/tree-sitter-unreal-cpp.dll` is produced.

- [ ] **Step 2: Run the full test suites.**

```bash
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

Expected: both pass with the new tests included.

- [ ] **Step 3: Manual smoke (optional but recommended).**

Set `cpp_grammar = "unreal"` in `config/wlx-listerine-colorizer.toml`, copy the rebuilt `wlx-listerine-colorizer.wlx64` into TC's plugin dir, restart TC, open a file containing `UCLASS()`. Confirm the macro is highlighted distinctly from regular identifiers.

- [ ] **Step 4: Reset the default if you toggled it for the smoke test.**

```bash
git diff config/wlx-listerine-colorizer.toml
```

If the file shows `cpp_grammar = "unreal"`, change it back to `"standard"` and amend the relevant commit (Task 8) — the shipping default must be `"standard"`.

---

## Parallelism notes

For subagent-driven execution, tasks are mostly sequential but a few branches are genuinely independent:

- **Tasks 1–2** must run first (the build pipeline must produce the DLL).
- **After Task 2 lands**, Tasks 3 (highlights) and 4 (enum) are independent and can run in parallel.
- **After Task 4 lands**, Task 5 (routing primitive + tests) blocks Task 6 (host adapter wiring), but Task 5 is independent of Task 3.
- **After Task 6 lands**, Task 7 (grammar tests) and Task 8 (TOML default) are independent.
- **Task 9 (docs)** can be parallelized with Task 7 or Task 8, or batched at the end.

If running purely sequentially, the order 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → final-verification is correct. If dispatching parallel subagents:

- Wave A: Task 1
- Wave B: Task 2
- Wave C: Task 3 ‖ Task 4
- Wave D: Task 5 (must wait for Task 4)
- Wave E: Task 6 (must wait for Task 5)
- Wave F: Task 7 ‖ Task 8 ‖ Task 9
- Wave G: Final verification

Each wave's subagents touch disjoint files (or in the case of Wave F, mostly disjoint — only `git commit` is shared, and serial commits are fine).
