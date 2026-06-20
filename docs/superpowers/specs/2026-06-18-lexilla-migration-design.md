# Design: replace tree-sitter with Lexilla for the colorizer engine

**Date:** 2026-06-18
**Decision (user):** Lexilla as the highlighting engine; **drop the Unreal-C++ variant.**
**Status:** design — awaiting sign-off on the two architecture decisions in §9.

Background research: `docs/research/2026-06-17-lightweight-highlighting.md`.

---

## 1. Why

tree-sitter's transient CST is ~15× the source (~140 MB for sqlite's 8.85 MB) and exists
during parse+sweep; it also drags in the tree-sitter runtime + 27 fetched/compiled grammar
DLLs + the FetchContent/CI-tarball machinery. Lexilla is a streaming, GUI-independent C++
lexer library (HPND license) whose retained state is ~0 (no tree), bundles 100+ lexers in
one lib, and — being line-based — also fixes the long-standing inability to color *code
fragments* (md code fences; `project_treesitter_fragment_highlighting`).

We only need **static coloring, no editing** — so tree-sitter's one unique advantage
(incremental re-parse) is exactly what we don't use.

## 2. The seam we're cutting at

The engine is already isolated behind the C ABI (`include/wlx_core/abi.h`). Plugins only see
`WlxColorSpan { start, length, color, bg_color, modifiers }`. **Output shape is unchanged**,
so the md plugin's code-fence path (`runtime/layout/code_fence_layout.cpp`) and the
colorizer's `SpanTable` keep working as-is. The swap is entirely inside `core_dll`.

## 3. Component map (inside `core_dll`)

| Today (tree-sitter) | After (Lexilla) |
|---|---|
| `grammar/GrammarRegistry` — loads `tree-sitter-<lang>.dll` + `highlights.scm`, parses | `lexilla/LexerRegistry` — `CreateLexer(name)` → `ILexer5*`; owns keyword lists + properties per language |
| `highlighting/QueryHighlighter` — runs TS queries → `ColorSpan` | `lexilla/StyleMapper` — runs `ILexer5::Lex` over a byte buffer via an `IDocument`, reads back style bytes, coalesces runs, maps style# → Helix scope → color |
| `colorizer/Colorizer` (parse/highlight_tree_range/free_tree + colorize) | `colorizer/Colorizer` — keeps `colorize(range)`; **tree methods removed** |
| `theme/HelixTheme` | **unchanged** — still scope→color/modifiers |
| `cmake/grammars.cmake` + 27 DLLs + tree-sitter runtime | **deleted**; one vendored Lexilla static lib |

## 4. Lexilla standalone integration (the concrete API)

1. Link `liblexilla`; `CreateLexer("cpp")` → `Scintilla::ILexer5*` (Lexilla exports it).
2. Implement a minimal `Scintilla::IDocument` (from Scintilla's `ILexer.h`) over a flat
   `string_view` + a parallel `std::vector<char>` style buffer. Essential methods:
   `Version`, `Length`, `GetCharRange`, `StyleAt`, `LineFromPosition`, `LineStart`,
   `LineEnd`, `GetLineState`/`SetLineState`, `StartStyling`, `SetStyles`. The rest (folding,
   decorations) stub out.
3. **`WordListSet(n, keywords)` per language** — without keyword lists, lexers don't color
   keywords (e.g. `SCE_C_WORD`). This is required config, not optional.
4. Optional `PropertySet(...)` for lexer tuning (e.g. `styling.within.preprocessor`).
5. `lexer->Lex(start, len, initStyle, &doc)` — supports lexing a **byte range**, matching
   the existing viewport-scoped `range_start/range_end`.
6. Read style bytes, coalesce equal-style runs, map via §5.

## 5. The new per-language data (the real work)

Each supported language needs a small table, replacing its `highlights.scm`:

- **Keyword lists** — one or more word lists. Source: SciTE's bundled `*.properties`
  (`keywords=...`) is a good starting corpus; curate per language.
- **Style# → scope map** — e.g. for `SCLEX_CPP`: `SCE_C_COMMENT→"comment"`,
  `SCE_C_STRING→"string"`, `SCE_C_WORD→"keyword"`, `SCE_C_NUMBER→"constant.numeric"`,
  `SCE_C_PREPROCESSOR→"keyword.directive"`, … Scopes are the **existing Helix names**, so
  `HelixTheme` resolution (incl. modifiers) is reused unchanged.
- Optional **properties**.

Scope: ~26 languages. Style enums are stable (`SciLexer.h`). This is bounded, mechanical,
and the bulk of the effort. Bundle these as data (TOML next to the DLL, mirroring themes)
rather than hard-coding, so they ship/version with the bundle.

## 6. Architecture simplification (no tree ⇒ no sweep)

The colorizer's two-phase open + background **sweep exists to free the tree-sitter tree**.
With Lexilla there is no tree. Proposed (Decision A, §9):

- Worker reads bytes → posts `TextReady` (unchanged, plain interactive text).
- Worker lexes the **whole file once** (cheap, streaming, low-mem) into the plugin-side
  `SpanTable`, posts the result. No tree, no `free_tree`, no adaptive mutex-held chunk loop
  *for memory reasons* (we may still chunk to keep the core mutex responsive, but there's no
  tree coexisting with the sweep, so the json.hpp peak problem disappears).
- Post-settle scroll still slices the `SpanTable`. Dark-flip/re-language = re-lex (fast).

This **removes**: `wlx_core_parse` / `wlx_core_highlight_range` / `wlx_core_free_tree`,
`WlxTree`, the grammar pin/unpin, the free-tree generation dance. ABI bumps to **v6**
(Decision B) with `colorize(range)` as the sole highlight entry point.

## 7. Drop Unreal-C++

- `cmake/grammars.cmake`: gone entirely (all grammars go); the `unreal-cpp` fetch + alias-TU
  in particular is removed.
- `src/plugin_colorizer/language/routing.h`: delete `apply_cpp_variant` (both overloads).
- Config: remove `[colorizer].cpp_grammar` from `wlx-listerine-colorizer.toml`,
  `core_config`, and `docs/CONFIGURATION.md`.
- `layout/colorizer_layout.h`: remove `CppGrammar`.
- Tests: remove unreal-routing tests; the grammar-menu still lists languages via
  `wlx_core_list_languages` (now Lexilla-backed).

## 8. Impact checklist

- **md plugin:** unaffected by ABI output; gains fragment coloring for code fences. Verify
  `code_fence_layout.cpp` still propagates `modifiers`.
- **Grammar context menu:** `wlx_core_list_languages` now enumerates Lexilla lexers we have
  maps for (not DLLs on disk).
- **Bundle:** drop `grammars/`; add `lexilla.dll` (or static-link into core). Ship the
  per-language keyword/style TOMLs.
- **Tests:** `colorizer-tests.exe` (~200 cases) — grammar-registry/tokenization/query tests
  get rewritten against Lexilla; Helix-theme + ABI + path-to-language + layout tests largely
  survive. **Visual regression (29 cases) is the real acceptance gate** — colors will shift
  vs tree-sitter goldens, so goldens get re-baselined intentionally after review.
- **Perf/bench:** re-baseline; expect transient peak ↓, open time likely ↓ (no query
  compile, no parse), held memory ≈ (SpanTable-dominated, engine-independent).
- **CI:** the grammar-tarball caching work becomes moot; configure time drops further.

## 9. Decisions needed before implementation

- **A — sweep/tree removal:** remove the cached-tree path + sweep and lex whole-file into the
  `SpanTable` (recommended), vs. keep the current sweep scaffolding and only swap internals.
- **B — ABI:** bump to v6 and delete the tree functions (recommended, follows from A), vs.
  keep ABI v5 shape with the tree functions as thin no-op shims (smaller blast radius).

## 10. Staged plan (with verify checks) — drafted, pending §9

1. **Vendor Lexilla** (FetchContent Lexilla + Scintilla headers; custom CMake static lib).
   *Verify:* `CreateLexer("cpp")` returns non-null in a scratch test; build green.
2. **`IDocument` + `StyleMapper` for one language (cpp)** behind `colorize()`; hard-code the
   cpp keyword list + style map. *Verify:* a cpp snippet produces sane spans; new unit test.
3. **Per-language data as TOML** + `LexerRegistry` loading it; port the other 25 maps.
   *Verify:* every shipped language returns non-empty spans on a sample; map-coverage test.
4. **Wire `CoreRegistry`/ABI to Lexilla**, apply Decisions A/B (remove tree path + sweep).
   *Verify:* colorizer-tests green; manual open of several files in TC.
5. **Drop Unreal + tree-sitter + grammars.cmake**; clean config/docs. *Verify:* no
   `tree_sitter`/`unreal`/`cpp_grammar` references remain; build green.
6. **Re-baseline** visual goldens (after visual review) + perf bench + docs. *Verify:* 29
   visual cases pass against new goldens; bench recorded; ~470 tests green; TC soak.

## Sources

- Lexilla — https://www.scintilla.org/Lexilla.html , https://www.scintilla.org/LexillaDoc.html
- Lexilla/Scintilla license (HPND) — https://www.scintilla.org/License.txt
- Lexer API (`ILexer5`, `IDocument`, `Lex`, `WordListSet`) — Scintilla `include/ILexer.h`
