# Lexilla Round 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the Lexilla migration — ABI v6 (delete cached-tree path + sweep), Lexilla coverage for cmake/html/xml/php, and the M6 finish (docs, bench, goldens).

**Architecture:** Lexilla is already the live engine behind a v5-shaped ABI (round 1). This round removes the now-vestigial cached-tree ABI + the colorizer's chunked sweep (worker lexes whole file once via `wlx_core_colorize`), adds the last 4 languages, and cleans docs/baselines.

**Tech Stack:** C++20, MSVC, CMake, Conan, Lexilla 5.5.0, doctest. Build: `cmake --build --preset conan-release`. Tests: `build/Release/colorizer-tests.exe`, `build/Release/tests.exe`.

**Spec:** `docs/superpowers/specs/2026-06-19-lexilla-round2-design.md` (+ round 1 spec). Project state + exact enums/tag-vocab in memory `project_lexilla_migration`.

**Dependency order:** Phase A (sequential, shared core/plugin) → Phase B (independent of A; may run parallel) → Phase C (needs final build). Every task ends green (full build + both suites).

---

## Phase A — ABI v6: remove cached-tree path + sweep

### Task A1: Migrate the colorizer worker to whole-file colorize

Replace the plugin worker's `parse_tree → ParseDone → chunked sweep → free_tree` with a single
`wlx_core_colorize(whole file)` → feed `SpanTable` → seal. Keep the ABI functions defined for now
(removed in A3) so the build stays green between tasks. Keep the two-phase open, `ViewLiveToken`,
grid, and `SpanTable` intact.

**Files:**
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp` (the async worker: the
  begin_async_load / begin_async_recolor flow, the `ParseDone`/`SweepDone` message handlers,
  `ColorViewState`'s `WlxTree*`/sweep fields).
- Likely delete usage of: `src/plugin_colorizer/colorize/sweep_chunk.h`.

- [ ] **Step 1:** Read `colorizer_host_adapter.cpp` end-to-end; map every use of `wlx_core_parse`,
  `wlx_core_highlight_range`, `wlx_core_free_tree`, the `SweepDone` message, the sweep worker
  thread, and the `WlxTree*` / generation fields in `ColorViewState`.
- [ ] **Step 2:** Replace the worker body: after `TextReady`, call
  `wlx_core_colorize(core, bytes, len, lang, dark, 0, 0, &spans, &count)` once on the worker
  thread, convert via `wlx_core::abi_spans_to_result`, post a single colored-result message.
  In the adopt handler: `vs->span_table.clear(); append_chunk(result, 0, len); seal();` then
  trigger repaint. Preserve the `ViewLiveToken`/`should_adopt_result` race guards verbatim.
- [ ] **Step 3:** Delete the `SweepDone` message, the sweep worker, the `WlxTree*`/sweep
  generation fields, and any `#include "plugin_colorizer/colorize/sweep_chunk.h"`.
- [ ] **Step 4:** Build colorizer plugin + colorizer-tests:
  `cmake --build --preset conan-release`. Expected: green (ABI funcs still exist; `test_sweep_parity`
  still passes — it exercises the ABI directly, not the plugin).
- [ ] **Step 5:** Run `build/Release/colorizer-tests.exe`. Expected: SUCCESS.

### Task A2: Remove the screenshot tool's --cached-tree path

The tool's `--cached-tree` / `--scroll-screens` bench path uses `wlx_core_parse` +
`wlx_core_highlight_range`. Remove it so A3 can delete those ABI functions.

**Files:**
- Modify: `src/tools/screenshot/colorizer_pipeline.cpp` (the cached-tree branch),
  `src/tools/screenshot/options.h` (`cached_tree`, `scroll_screens` fields),
  `src/tools/screenshot/main.cpp` (`--cached-tree`, `--scroll-screens` parsing + usage text).

- [ ] **Step 1:** In `colorizer_pipeline.cpp`, delete the `opts.cached_tree` branch (the parse +
  per-viewport highlight_range + post-settle scroll loop). Keep only the eager whole-doc colorize
  path (already Lexilla-backed via `wlx_core_colorize`).
- [ ] **Step 2:** Remove `cached_tree` / `scroll_screens` from `options.h` and their arg parsing +
  usage lines in `main.cpp`.
- [ ] **Step 3:** Build screenshot_tool: `cmake --build --preset conan-release --target screenshot_tool`.
  Expected: green.
- [ ] **Step 4:** Smoke: copy a `.cpp` to `%TEMP%`, run `screenshot_tool <tmp> --colorizer --lang cpp`,
  confirm a sibling `.png` is written.

### Task A3: Delete the cached-tree ABI + bump to v6

Now nothing calls `parse`/`highlight_range`/`free_tree`/`prewarm`. Remove them.

**Files:**
- Modify: `include/wlx_core/abi.h` (remove `WlxTree` typedef, `wlx_core_parse`,
  `wlx_core_highlight_range`, `wlx_core_free_tree`, `wlx_core_prewarm`, `TreeDeleter`/`TreePtr`;
  `WLX_CORE_ABI_VERSION` 5 → 6), `src/core_dll/abi/wlx_core_abi.cpp` (drop those exports),
  `src/core_dll/colorizer/colorizer.{h,cpp}` (drop `WlxTree`, `parse_tree`,
  `highlight_tree_range`, `free_tree`, `prewarm`), `src/core_dll/registry/core_registry.{h,cpp}`
  (same).
- Delete: `tests/plugin_colorizer/colorize/test_sweep_parity.cpp`.
- Modify: `tests/core_dll/abi/test_wlx_core_abi.cpp` (delete parse/highlight_range/free_tree/prewarm
  cases; change "ABI version is 5" → 6).
- Modify: `tests/CMakeLists.txt` (drop `test_sweep_parity.cpp`).

- [ ] **Step 1:** Remove the four functions + `WlxTree` + the RAII shim from `abi.h`; bump the
  version macro to 6.
- [ ] **Step 2:** Remove the matching exports from `wlx_core_abi.cpp` and the methods + `WlxTree`
  struct from `colorizer.{h,cpp}` and `core_registry.{h,cpp}`.
- [ ] **Step 3:** Delete `test_sweep_parity.cpp`, drop it from `tests/CMakeLists.txt`, and trim
  the removed cases from `test_wlx_core_abi.cpp` (keep colorize/supports/list/version/threading;
  version assertion → 6).
- [ ] **Step 4:** Build all: `cmake --build --preset conan-release`. Expected: green.
- [ ] **Step 5:** Run both suites. Expected: SUCCESS. Commit nothing (left for user review).

---

## Phase B — the 4 remaining languages

### Task B1: html / xml / php via suffix-matched tags

`LexHTML` tags are HTML-specific and prefix embedded scripts (`client javascript keyword`,
`server php literal string`). Switch `tag_to_scope` to match the generic token as a SUFFIX, and
add the base HTML tokens. **`tag_to_scope` is shared by all 15 working languages — re-run the
full suite.**

**Files:**
- Modify: `src/core_dll/lexilla/lexilla_highlighter.cpp` (`tag_to_scope`).
- Modify: `src/core_dll/lexilla/lexer_registry.cpp` (add `hypertext`/`xml`/`phpscript` specs,
  keyed by our ids `html`/`xml`/`php`).
- Modify: `tests/core_dll/lexilla/test_lexer_registry.cpp` (coverage cases).

- [ ] **Step 1:** Add failing coverage cases in `test_lexer_registry.cpp`: `{"html", "<div class=\"x\">hi</div><!-- c -->\n"}`, `{"xml", "<a x=\"1\">t</a>\n"}`, `{"php", "<?php $x = 1; // c\n?>\n"}` — assert non-empty spans + a comment/tag span. Run; expect FAIL (no spec).
- [ ] **Step 2:** Rewrite `tag_to_scope` to test tokens as a trailing word / contains (so
  `"client javascript keyword"`→keyword, `"server php literal string"`→string,
  `"client python literal string character"`→constant.character). Add base HTML tokens:
  `tag`→"keyword", `attribute`→"variable", `tag operator`→"operator", `literal`→"constant".
  Keep prior mappings (comment/keyword/operator/numeric/string/preprocessor/identifier-skip/error-skip).
  Order: check string-character before string; check `error` and `identifier` → skip.
- [ ] **Step 3:** Add registry specs: `html`→lexer "hypertext", `xml`→"xml", `php`→"phpscript"
  (word_lists optional; for php put PHP keywords in HTML word-list index 4 per `phpscriptWordListDesc`).
- [ ] **Step 4:** Run `colorizer-tests.exe`. Expected: ALL pass (the 15 prior langs unchanged + 3 new).
  If any prior-lang assertion shifted, inspect `tag_to_scope` ordering.
- [ ] **Step 5:** Full build + both suites green.

### Task B2: cmake via explicit map + word lists

`LexCMake` has no `lexicalClasses`; map `SCE_CMAKE_*` explicitly and supply command word-lists.

**Files:**
- Modify: `src/core_dll/lexilla/lexer_registry.cpp` (cmake spec), `test_lexer_registry.cpp`.

- [ ] **Step 1:** Add failing case `{"cmake", "# c\nadd_library(foo STATIC a.c)\nif(X)\nendif()\n"}`
  → assert non-empty (comment + command). Run; expect FAIL.
- [ ] **Step 2:** Add `cmake_spec()`: lexer "cmake"; word_lists = {commands list, parameters list}
  (commands: `add_executable add_library target_link_libraries include find_package set if elseif
  else endif foreach endforeach while endwhile function endfunction macro endmacro project
  option message install add_subdirectory target_include_directories ...`); style_scopes map
  `SCE_CMAKE_COMMENT→comment`, `STRINGDQ/STRINGLQ/STRINGRQ/STRINGVAR→string`,
  `COMMANDS→keyword`, `PARAMETERS→keyword`, `VARIABLE→variable`, `NUMBER→constant.numeric`,
  `WHILEDEF/FOREACHDEF/IFDEFINEDEF/MACRODEF→keyword`, `USERDEFINED→function`. Register `"cmake"`.
- [ ] **Step 3:** Run `colorizer-tests.exe`. Expected: PASS.
- [ ] **Step 4:** Full build + both suites green.

---

## Phase C — M6 finish

### Task C1: Docs + dead-script cleanup

**Files:**
- Modify: `CLAUDE.md`, `README.md`, `docs/CONFIGURATION.md`, `docs/LANGUAGES.md`, `docs/BUILDING.md`
  (replace tree-sitter/grammar narrative with Lexilla; note 7 languages are plain-text;
  remove `[colorizer].cpp_grammar` + `[grammar_cache]` from config docs).
- Delete: `scripts/fetch-grammars.sh`, `scripts/build-grammars.sh`, `scripts/package-grammars.sh`.
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp` (stale `apply_cpp_variant`
  comment in the `force_grammar_id` doc).

- [ ] **Step 1:** Grep `tree-sitter|grammar|tree_sitter|cpp_grammar|grammar_cache` across the docs
  files; rewrite each hit to describe the Lexilla engine (one static lib, no grammar DLLs,
  ~19 well-covered langs + 7 plain-text). Keep the architecture sections accurate.
- [ ] **Step 2:** Delete the three dead grammar scripts; grep for references to them (CI yaml,
  README) and remove.
- [ ] **Step 3:** Fix the stale comment in `colorizer_host_adapter.cpp`.
- [ ] **Step 4:** `git status` review; build still green (docs/scripts don't affect build).

### Task C2: Perf bench re-baseline

**Files:** Modify `README.md` (Performance section), via `uv run scripts/bench.py --update`.

- [ ] **Step 1:** Ensure a Release `screenshot_tool.exe` exists.
- [ ] **Step 2:** `uv run scripts/bench.py` — record current vs old baseline; note open-time /
  held-memory deltas vs tree-sitter.
- [ ] **Step 3:** `uv run scripts/bench.py --update` to rewrite the README baseline. Report the
  before/after table to the user.

### Task C3: Visual goldens — regenerate + report (user review)

**Files:** `test_data/cases/*` (do NOT blind-rebaseline).

- [ ] **Step 1:** Run `./scripts/visual-test.sh` (generate current + compare to Chrome goldens).
- [ ] **Step 2:** Collect per-case similarity %; list PASS (≥95%) vs BELOW-threshold.
- [ ] **Step 3:** Report the pass/fail table to the user. Do NOT run `update-goldens` — the user
  decides which (if any) shifts are acceptable, since Lexilla's palette differs from Chrome's.

---

## Self-review notes
- Spec coverage: A→Phase A; B→Phase B; C (docs/bench/goldens)→Phase C. Complete.
- A1 keeps ABI funcs so the build stays green until A3 removes their last callers (plugin in A1,
  tool in A2). Order verified.
- `tag_to_scope` change is the one shared-surface risk — B1 Step 4 gates on the full suite.
