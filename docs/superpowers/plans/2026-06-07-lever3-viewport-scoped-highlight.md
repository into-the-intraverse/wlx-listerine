# Lever 3 — Viewport-Scoped Tree-sitter Highlight Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **Independent of Lever 1** — may proceed in parallel.

**Goal:** Stop running the tree-sitter highlight query over the whole file at open. Phase A makes the highlight range-limitable (safe primitive, no behavior change at default). Phase B caches the parsed tree and highlights only the visible byte range on demand, so the colorizer's first-open colorize drops from ~78 ms (12k file: parse 43 + highlight 35) toward parse-once + viewport-highlight.

**Architecture:** Today `wlx_core_colorize` does, under one mutex, `parse(whole file)` → `highlight(whole tree)` → `delete tree`. **Phase A:** add a byte-range to `QueryHighlighter::highlight` via `ts_query_cursor_set_byte_range`, threaded through `Colorizer::colorize` → `CoreRegistry::colorize` → the C ABI (bump v3→v4; default range = whole doc = identical output). **Phase B:** add a *cached-tree* ABI — `wlx_core_parse` (parse once, return an opaque `WlxTree*` that **pins its grammar against eviction**), `wlx_core_highlight_range` (highlight a byte range against the cached tree), `wlx_core_free_tree` (unpin + delete). The colorizer plugin parses once at open, highlights the visible range, re-highlights newly-visible ranges on scroll, and merges spans into its (already viewport-lazy) layout.

**Tech Stack:** C, C++20, tree-sitter (`TSTree`, `TSQueryCursor`, `ts_query_cursor_set_byte_range`), the `wlx-listerine-core.dll` C ABI, doctest.

---

## Invariants (memory safety & correctness — priority #1; do not violate)

1. **A cached `TSTree` must keep its grammar alive.** A `TSTree` holds an internal pointer to the `TSLanguage` it was parsed with. The `GrammarCache` (LRU+TTL) may evict and **free the grammar DLL**. Today this is safe only because trees never outlive the locked colorize call. **Phase B caches trees across calls, so it MUST pin the tree's grammar entry** (refcount/non-evictable flag) for the tree's whole lifetime, and unpin on `wlx_core_free_tree`. A dangling `TSLanguage` is a hard crash. This is the single most important task in Phase B.
2. **Range correctness across construct boundaries.** Range-limiting the *query cursor* is safe because the parse is whole-file: a multi-line construct (block comment, raw string) whose node starts outside the range still resolves — you are scoping query *iteration*, not the parse tree. Verify with the Phase A multi-line test.
3. **Default range = whole document = byte-identical output.** `range_start == range_end == 0` (or `range_end == UINT32_MAX`) must reproduce today's spans exactly. The Stage-2 token-diff visual harness must stay byte-identical at default. The markdown code-fence caller always uses the default (fences are small).
4. **Span ownership unchanged.** `wlx_core_free_spans` still frees span arrays; `wlx_core_free_tree` frees only the tree (+ unpins grammar), never the span arrays.
5. **Mutex discipline.** All new ABI entry points take the `CoreRegistry` mutex for their whole body, exactly like `colorize`. `wlx_core_highlight_range` reads the cached tree under the lock; since the grammar is pinned, eviction of *other* entries during the call cannot free this tree's language.

---

## File Structure

| File | Create/Modify | Responsibility |
|------|---------------|----------------|
| `src/core_dll/highlighting/query_highlighter.h` / `.cpp` | Modify | `highlight(...)` gains `uint32_t range_start, uint32_t range_end`; calls `ts_query_cursor_set_byte_range`. |
| `src/core_dll/colorizer/colorizer.h` / `.cpp` | Modify | `colorize(...)` gains range params (default 0,0); Phase B adds `parse_tree`/`highlight_tree_range`. |
| `src/core_dll/registry/core_registry.h` / `.cpp` | Modify | `colorize(...)` gains range; Phase B adds tree-cache + `parse`/`highlight_range`/`free_tree`. |
| `include/wlx_core/abi.h` | Modify | Bump `WLX_CORE_ABI_VERSION` 3→4 (Phase A), 4→5 (Phase B); add range params + the tree ABI; add `WlxTree` opaque + RAII shim. |
| `src/core_dll/abi/wlx_core_abi.cpp` | Modify | Implement the new/changed exports. |
| `src/core_dll/grammar/grammar_cache.h` / `.cpp` | Modify | **Phase B:** grammar pinning (`pin(lang)`/`unpin(lang)`; eviction skips pinned). |
| `src/runtime/layout/code_fence_layout.cpp` (or `layout_engine.cpp`) | Modify | Pass default range (0,0) to the changed `wlx_core_colorize`. |
| `src/plugin_colorizer/window/colorizer_host_adapter.cpp` | Modify | **Phase B:** parse-once at open, viewport-highlight, re-highlight on scroll, free tree on close/reload. |
| `src/plugin_colorizer/layout/colorizer_layout.{h,cpp}` | Modify | **Phase B:** accept incremental spans for a byte-range and update affected lines. |
| `tests/.../test_query_highlighter*.cpp` (existing colorizer tests) | Modify/Create | Range tests + default-equivalence. |

> **Before Task 1:** locate the ABI version-pin test (`Grep -r "WLX_CORE_ABI_VERSION\|abi_version" tests`) — it asserts the version equals 3. Every version bump below must update it. Also `Grep` for all `wlx_core_colorize(` call sites (markdown + colorizer + screenshot pipeline) — each must be updated when the signature changes.

---

# PHASE A — range-limitable highlight primitive (safe, shippable, no behavior change)

## Task A1: `QueryHighlighter::highlight` accepts a byte range

**Files:**
- Modify: `src/core_dll/highlighting/query_highlighter.h`, `src/core_dll/highlighting/query_highlighter.cpp:171-245`
- Test: the existing colorizer highlighter test file (find via `Grep -rl "QueryHighlighter" tests`); create `tests/core_dll/highlighting/test_highlight_range.cpp` if none focuses on highlight.

- [ ] **Step 1: Write the failing test**

Create/append a test (adjust the include + fixture to the existing colorizer-test harness, which builds a `TSTree`+`TSQuery` via the grammar registry — mirror an existing highlighter test's setup):

```cpp
TEST_CASE("highlight with a byte range only returns spans inside the range") {
    // (Set up tslang/tree/query/theme via the same helpers the existing
    //  colorizer highlighter tests use; `src` is a multi-line C snippet.)
    std::string src = "int a = 1;\nint b = 2;\nint c = 3;\n";
    // ... obtain tree, query, theme for "c" ...

    auto full  = QueryHighlighter::highlight(tree, query, theme, src);          // default
    auto line2 = QueryHighlighter::highlight(tree, query, theme, src,
                    /*default_color=*/0xD4D4D4,
                    /*range_start=*/11, /*range_end=*/21);                       // "int b = 2;"

    REQUIRE(!full.empty());
    // Every returned span must start within [11, 21).
    for (auto& s : line2) {
        CHECK(s.start >= 11);
        CHECK(s.start < 21);
    }
    // And the range result is a subset (fewer or equal) of the full result.
    CHECK(line2.size() <= full.size());
}

TEST_CASE("highlight default range reproduces whole-document spans") {
    // ... same setup ...
    auto a = QueryHighlighter::highlight(tree, query, theme, src);
    auto b = QueryHighlighter::highlight(tree, query, theme, src, 0xD4D4D4, 0, 0);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].start == b[i].start);
        CHECK(a[i].length == b[i].length);
        CHECK(a[i].color == b[i].color);
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset conan-release`
Expected: compile error — `highlight` has no `range_start`/`range_end` parameters.

- [ ] **Step 3: Implement**

`query_highlighter.h` — extend the signature (keep defaults so existing callers compile):

```cpp
    static std::vector<colorizer::ColorSpan> highlight(
        const TSTree* tree,
        const TSQuery* query,
        const theme::HelixTheme& theme,
        std::string_view source,
        uint32_t default_color = 0xD4D4D4,
        uint32_t range_start = 0,
        uint32_t range_end = 0);   // 0,0 (or end<=start) => whole document
```

`query_highlighter.cpp` — in `highlight`, after `ts_query_cursor_exec(cursor, query, ts_tree_root_node(tree));` (line 206) add:

```cpp
    // Viewport scoping: when a non-empty range is given, limit query iteration
    // to captures intersecting [range_start, range_end). The parse is whole-file,
    // so multi-line constructs that begin outside the range still resolve
    // (Invariant 2). Default (end<=start) leaves the cursor full-document.
    if (range_end > range_start)
        ts_query_cursor_set_byte_range(cursor, range_start, range_end);
```

> `ts_query_cursor_set_byte_range` is declared in `<tree_sitter/api.h>` (already included). It returns `bool` (false if the range is invalid) — ignore the return; an invalid range simply leaves full iteration.

- [ ] **Step 4: Run tests**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe -tc="highlight*range*"` and `-tc="*default range*"`
Expected: PASS. Also run full `./build/Release/colorizer-tests.exe` — unchanged cases green.

- [ ] **Step 5: Commit**

```bash
git add src/core_dll/highlighting/query_highlighter.h src/core_dll/highlighting/query_highlighter.cpp tests/
git commit -m "feat(core): QueryHighlighter::highlight accepts an optional byte range"
```

---

## Task A2: Thread the range through `Colorizer` and `CoreRegistry`

**Files:**
- Modify: `src/core_dll/colorizer/colorizer.h`, `src/core_dll/colorizer/colorizer.cpp:76-126`
- Modify: `src/core_dll/registry/core_registry.h:31-33`, `src/core_dll/registry/core_registry.cpp:50-56`

- [ ] **Step 1: Write the failing test**

Append to the colorizer-level test (mirror an existing `Colorizer::colorize` test):

```cpp
TEST_CASE("Colorizer::colorize range-limits the highlight") {
    // ... construct a Colorizer over the test grammar dir ...
    std::string src = "int a = 1;\nint b = 2;\n";
    auto full = c.colorize(src, "c", true);
    auto ranged = c.colorize(src, "c", true, /*range_start=*/0, /*range_end=*/10);
    for (auto& s : ranged.spans) CHECK(s.start < 10);
    CHECK(ranged.spans.size() <= full.spans.size());
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset conan-release`
Expected: compile error — no range overload.

- [ ] **Step 3: Implement**

`colorizer.h` — add range params (defaults 0,0) to both `colorize` overloads:

```cpp
    ColorizeResult colorize(std::string_view source, const std::string& language,
                            bool dark_mode,
                            uint32_t range_start = 0, uint32_t range_end = 0);
    ColorizeResult colorize(std::string_view source, const std::string& language,
                            bool dark_mode, ColorizeTimings* timings,
                            uint32_t range_start = 0, uint32_t range_end = 0);
```

`colorizer.cpp` — the timings overload forwards range to highlight; the convenience overload forwards to it:

```cpp
ColorizeResult Colorizer::colorize(std::string_view source, const std::string& language,
                                   bool dark_mode, uint32_t range_start, uint32_t range_end) {
    return colorize(source, language, dark_mode, nullptr, range_start, range_end);
}
```

In the timings overload, change the highlight call (line 114):

```cpp
    result.spans = QueryHighlighter::highlight(tree, query, t, source, default_color,
                                               range_start, range_end);
```

`core_registry.h` / `.cpp` — add range params to `CoreRegistry::colorize` (default 0,0) and forward to `colorizer_->colorize(source, language, dark_mode, range_start, range_end)`.

- [ ] **Step 4: Run tests**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe`
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add src/core_dll/colorizer/colorizer.h src/core_dll/colorizer/colorizer.cpp \
        src/core_dll/registry/core_registry.h src/core_dll/registry/core_registry.cpp tests/
git commit -m "feat(core): thread highlight byte-range through Colorizer + CoreRegistry"
```

---

## Task A3: Expose the range on the C ABI (bump v3→v4)

**Files:**
- Modify: `include/wlx_core/abi.h`, `src/core_dll/abi/wlx_core_abi.cpp:38-75`
- Modify: all `wlx_core_colorize(` call sites; the ABI version-pin test.

- [ ] **Step 1: Write the failing test**

Update the version-pin test to expect `4`, and add an ABI range smoke (mirror an existing ABI colorize test):

```cpp
TEST_CASE("ABI version is 4") {
    CHECK(wlx_core_abi_version() == 4);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe -tc="ABI version*"`
Expected: FAIL (still 3) — and the old `wlx_core_colorize` callers still compile (you haven't changed the signature yet), so do Step 3 atomically.

- [ ] **Step 3: Implement (signature + version + all callers in one commit)**

`abi.h`:
- `#define WLX_CORE_ABI_VERSION 4`.
- Add two params to the `wlx_core_colorize` declaration:

```c
WLX_CORE_API int wlx_core_colorize(WlxCore*,
                                   const char* source, uint32_t len,
                                   const char* language, int dark_mode,
                                   uint32_t range_start, uint32_t range_end,
                                   WlxColorSpan** out_spans, uint32_t* out_count);
```

`wlx_core_abi.cpp` — accept the params and forward:

```c
    auto result = reg.colorize(std::string_view(source, len), language,
                               dark_mode != 0, range_start, range_end);
```

Update **every** caller (from the pre-task `Grep`):
- markdown code fence (`code_fence_layout.cpp` from Lever-1 Task 2, else `layout_engine.cpp`): pass `0, 0` for range (whole fence).
- colorizer pipeline / host: pass `0, 0` for now (Phase B changes this).
- screenshot tool colorizer pipeline if it calls the ABI (it calls `Colorizer::colorize` directly — check; if so, no ABI change needed there).

- [ ] **Step 4: Run tests + visual**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe && ./build/Release/colorizer-tests.exe`
Expected: green (incl. `ABI version is 4`).
Run: `./scripts/visual-test.sh`
Expected: all cases ≥ 95% AND Stage-2 token diff byte-identical (Invariant 3 — callers all pass default range).

- [ ] **Step 5: Commit**

```bash
git add include/wlx_core/abi.h src/core_dll/abi/wlx_core_abi.cpp \
        src/runtime/layout/ src/plugin_colorizer/ src/tools/ tests/
git commit -m @'
feat(core-abi): wlx_core_colorize takes a byte range (ABI v3->v4)

Default range (0,0) is whole-document; all current callers pass it, so
output is byte-identical. Enables viewport-scoped highlight in Phase B.
'@
```

> **End of Phase A.** Shippable on its own (primitive + no behavior change). The actual open-cost win lands in Phase B.

---

# PHASE B — cached-tree viewport colorize (the win)

> Phase B touches the core's grammar-cache lifetime model and the colorizer's span pipeline. **Read these before starting:** `src/core_dll/grammar/grammar_cache.{h,cpp}` (eviction + how grammars are freed — for pinning), `src/plugin_colorizer/window/colorizer_host_adapter.cpp` (the colorize call site, `ViewState`, scroll handlers), `src/plugin_colorizer/layout/colorizer_layout.{h,cpp}` (the span→line distribution that must become incremental). The task granularity below is deliberately one level coarser than Phase A because exact code depends on those files; expand each into TDD steps at execution time following the Phase A pattern.

## Task B1: Grammar pinning in `GrammarCache` (Invariant 1 — do first)

**Files:** Modify `src/core_dll/grammar/grammar_cache.h` / `.cpp`. Test: extend the existing grammar-cache eviction tests.

- [ ] Add a per-entry `uint32_t pin_count` and `void pin(const std::string& lang)` / `void unpin(const std::string& lang)`. Eviction (LRU/TTL sweep) must **skip** entries with `pin_count > 0`.
- [ ] **Failing test first:** pin a language, force eviction pressure (exceed `cap` + advance the TTL clock via the injectable `Clock`), assert the pinned grammar's `TSLanguage*` is unchanged/valid and the entry was not freed; unpin, then assert it becomes evictable again.
- [ ] Commit: `feat(core): GrammarCache entry pinning (evictor skips pinned grammars)`.

## Task B2: Cached-tree ABI — `wlx_core_parse` / `wlx_core_highlight_range` / `wlx_core_free_tree` (bump v4→v5)

**Files:** Modify `abi.h`, `wlx_core_abi.cpp`, `core_registry.h/.cpp`, `colorizer.h/.cpp`. Test: ABI tree round-trip.

- [ ] `abi.h`: bump to `5`; add opaque `typedef struct WlxTree WlxTree;` and:

```c
// Parse `source` once and cache the tree; pins the grammar so the tree's
// TSLanguage stays valid until wlx_core_free_tree. Returns NULL on bad args /
// unknown language / parse failure.
WLX_CORE_API WlxTree* wlx_core_parse(WlxCore*, const char* source, uint32_t len,
                                     const char* language);

// Highlight [range_start, range_end) against a previously parsed tree. Spans are
// heap-owned (free with wlx_core_free_spans). range_end<=range_start => whole doc.
WLX_CORE_API int wlx_core_highlight_range(WlxCore*, WlxTree*, int dark_mode,
                                          uint32_t range_start, uint32_t range_end,
                                          WlxColorSpan** out_spans, uint32_t* out_count);

// Delete the tree and unpin its grammar. Safe on NULL.
WLX_CORE_API void wlx_core_free_tree(WlxCore*, WlxTree*);
```

Add an RAII shim (mirror `SpansPtr`): `struct TreeDeleter { WlxCore* core; void operator()(WlxTree*) const noexcept; }; using TreePtr = std::unique_ptr<WlxTree, TreeDeleter>;` (the deleter needs the core handle — store it in the deleter).

- [ ] Core side: `WlxTree` wraps `{ TSTree* tree; std::string language; std::string source_copy; }`. **`wlx_core_parse` must own a copy of the source** (the tree references byte offsets, and `highlight_range` re-reads the source text for predicate evaluation — `capture_text` reads `source`). Store `source_copy` in the `WlxTree`. Then `parse`: take mutex → `grammar_registry_->parse(lang, source_copy)` → on success `grammar_cache.pin(lang)` → return handle. `highlight_range`: take mutex → `QueryHighlighter::highlight(tree->tree, query, theme, tree->source_copy, default, start, end)`. `free_tree`: take mutex → `ts_tree_delete` → `unpin(lang)` → delete handle.
- [ ] `CoreRegistry` gains `parse_tree`/`highlight_tree_range`/`free_tree` methods (mutex-held) and owns nothing else (the handle is caller-held). Expose `Colorizer::parse_tree`/`highlight_tree_range` so the registry delegates (keeps tree-sitter inside the colorizer/grammar layer).
- [ ] **Failing test first:** `wlx_core_parse` a C snippet → non-null; `wlx_core_highlight_range(full)` equals `wlx_core_colorize(...,0,0,...)` spans byte-identical (Invariant 3); a sub-range returns only in-range spans; `wlx_core_free_tree` then no leak (run under the existing leak-style checks). Bump the version-pin test to 5.
- [ ] Commit: `feat(core-abi): cached-tree parse + range-highlight (ABI v4->v5)`.

## Task B3: Colorizer plugin — parse once, highlight viewport, re-highlight on scroll

**Files:** Modify `src/plugin_colorizer/window/colorizer_host_adapter.cpp`, `src/plugin_colorizer/layout/colorizer_layout.{h,cpp}`. Test: manual + visual smokes (host wiring is not unit-tested).

Design (mirrors Lever 1's host-driven viewport materialization, but for colors):
- [ ] `ViewState` gains `wlx_core::TreePtr tree;` and a `std::vector<bool> line_colored;` (or a coarser "colored byte ranges" set). On open: `wlx_core_parse(core, raw_utf8, len, lang)` once; store in `vs->tree`. **Free on close/reload** (`TreePtr` destructor handles it; ensure `ListCloseWindow`/reload resets it — and that DLL_PROCESS_DETACH leaks it like other COM/handles per the repo's detach rule, since `wlx_core_free_tree` calls into the core under a mutex).
- [ ] Replace the open-time whole-file `colorize` with: compute the visible byte range from the first viewport (the layout already maps lines→`utf8_byte_start`; expose a `line_byte_range(first_visible_line, last_visible_line)` helper from `colorizer_layout`), call `wlx_core_highlight_range(tree, range)` , feed those spans to the layout for the visible lines, mark them colored.
- [ ] On scroll (the colorizer host's scroll/paint path — it already does lazy *layout* materialization): before paint, for any visible line not yet colored, `wlx_core_highlight_range` the new byte range against the cached tree (cheap — no re-parse), distribute spans to those lines (`colorizer_layout` gains an incremental `apply_spans_to_range(doc, spans, byte_start, byte_end)` that updates only the affected blocks' `color_ranges` and clears their cached `run.layout` so the next materialize re-applies colors), mark colored.
- [ ] `colorizer_layout.cpp`: refactor the span-distribution block (lines 383-467) into a reusable `distribute_spans(...)` callable both at build time (whole doc, current behavior when called with the full set) and incrementally for a byte range. Keep the existing eager whole-doc path for `word_wrap` on (Invariant: word-wrap stays fully eager, like today).
- [ ] **Document the no-silent-cap:** if any path falls back to whole-file colorize (e.g., parse failed, or word-wrap), `WLX_TRACE` it so the behavior is observable.
- [ ] Manual smoke: open a large C++ file in TC — instant colored first paint; scrolling colors new regions with no parse hitch; correct colors across a block comment / raw string that starts above the viewport (Invariant 2).
- [ ] Commit: `feat(plugin-colorizer): viewport colorize via cached tree + range highlight`.

## Task B4: Benchmark validation (gate)

- [ ] Bench BEFORE/AFTER on a ≥ 1 MB C++ file: `./build/Release/screenshot_tool.exe --bench <big.cpp> --width 1000 --height 1200 --dark`. The screenshot colorizer pipeline calls `Colorizer::colorize` directly — to exercise the win there, the bench/tool path must use parse-once + range-highlight too (check `colorizer_pipeline.cpp`; if it bypasses the cached-tree path, add a `--bench` variant or measure via the real plugin). Record `colorize` first-open before/after.
- [ ] Update perf memory (`project_perf_baselines`) with the cached-tree viewport colorize result.

---

## Self-Review checklist (run before handing off)

- **Spec coverage:** range primitive (A1-A3) ✓, default-equivalence (A1/A3 tests, Invariant 3) ✓, grammar pinning (B1, Invariant 1) ✓, cached-tree ABI (B2) ✓, plugin integration + on-scroll (B3) ✓, no-silent-cap trace (B3) ✓, bench (B4) ✓.
- **Type consistency:** `highlight(..., range_start, range_end)` signature identical across A1 test/impl, A2 (`Colorizer`/`CoreRegistry`), B2 (`highlight_tree_range`); `WlxTree`/`TreePtr`/`wlx_core_parse`/`wlx_core_highlight_range`/`wlx_core_free_tree` used identically in B2/B3; ABI version bumped exactly twice (4 then 5) with the pin-test updated each time.
- **Phase boundary:** Phase A is independently shippable and reversible; Phase B is gated on B1 (pinning) landing first — **never cache a tree before pinning works** (Invariant 1).
- **Open risk:** `wlx_core_parse` must copy the source (tree + predicate highlight both read it); a view into a plugin buffer that later changes/frees would dangle. The B2 design stores `source_copy` — keep it.
