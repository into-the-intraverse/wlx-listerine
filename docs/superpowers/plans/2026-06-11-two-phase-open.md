**Superseded 2026-06-11:** wrap mode now uses the implicit grid (two-phase, tree under wrap, B3.4 retired) — see docs/superpowers/plans/2026-06-11-wrap-grid.md.

# Two-Phase Open Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The colorizer shows plain, fully interactive text as soon as the file read completes (milliseconds), and colors arrive when the background parse lands (~1 s on sqlite-class files) — without losing scroll, selection, or search state.

**Architecture:** One load worker, two ordered posts. After a successful read the worker posts a new `TextReady` message carrying a copy of the raw bytes (phase 1: the UI swaps the source, lays out the grid skeleton plain, goes `Ready`); the worker keeps its original, parses, and the existing `ParseDone` post arrives with a new `two_phase` flag (phase 2: set the tree, clear the materialized window so the next paint re-colors it, start the sweep — no relayout, because the bytes didn't change). Wrap mode and read failures stay single-phase exactly as today. All interleavings (reload, dark-flip, close mid-gap) resolve through the existing generation/token discipline.

**Tech Stack:** C++20/MSVC (/W4 /WX), Win32 message passing per `runtime/host/async_loader.h` discipline (which itself is NOT modified), the M2 implicit-grid machinery, doctest.

Spec: `docs/superpowers/specs/2026-06-11-two-phase-open-design.md`. Single file changes hands: `src/plugin_colorizer/window/colorizer_host_adapter.cpp` (+ docs). Build `cmake --build --preset conan-release`; suites `./build/Release/colorizer-tests.exe` (202) and `./build/Release/tests.exe` (280) from REPO ROOT; visual `bash scripts/visual-test.sh`.

Task ordering is handler-before-producer: Task 1 lands the message + adopt handler + drain (dormant — nothing posts yet, so no commit can leak a payload), Task 2 turns on the producer and the slimmed phase-2 path.

---

### Task 1: TextReady message, result struct, phase-1 adopt handler, close-drain (dormant)

**Files:**
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp` only

- [ ] **Step 1: Message + result struct.** Next to `wm_colorizer_sweep_done()` add:

```cpp
// Phase 1 of a two-phase open (spec 2026-06-11-two-phase-open): the worker
// posts the raw bytes as soon as the read completes; adoption shows plain,
// fully interactive text while the parse continues. Distinct registered
// message for the same reasons as ParseDone.
static UINT wm_colorizer_text_ready() {
    static const UINT m = RegisterWindowMessageW(L"WlxListerineColorizer.TextReady");
    return m;
}
```

Next to `SweepResultColor` add:

```cpp
// Worker -> UI phase-1 result. COM-free. Carries a COPY of the source (the
// worker keeps its original for the parse).
struct TextResultColor {
    uint64_t generation = 0;
    std::shared_ptr<wlx::runtime::host::ViewLiveToken> live;
    std::string raw_utf8;
};
```

In `ParseResultColor` add (after `wrap`):

```cpp
    // True when a TextReady post already delivered raw_utf8 (phase 1); the
    // adopt handler then takes the minimal tree + window-clear path and this
    // struct's raw_utf8 is empty. False on the single-phase paths (wrap mode,
    // read failure, or a failed phase-1 post) which still carry the source.
    bool two_phase = false;
```

- [ ] **Step 2: Phase-1 adopt handler.** In `ColorViewWndProc`, immediately after the `wm_colorizer_sweep_done()` block, add the sibling:

```cpp
    if (msg == wm_colorizer_text_ready()) {
        std::unique_ptr<TextResultColor> res(reinterpret_cast<TextResultColor*>(lp));
        auto it = g_views.find(hwnd);
        if (it == g_views.end()) return 0;            // closed/recycled -> drop
        ColorViewState* v = it->second;
        if (!wlx::runtime::host::should_adopt_result(res->live.get(), res->generation,
                                                     v->live.get(), v->current_gen))
            return 0;                                 // superseded/closed -> drop
        // Swap in the new source and show it plain. The OLD file's tree/table
        // describe the previous content — drop them now (the generation bump
        // already cancelled the old sweep; freeing here is on the UI thread,
        // off the loader lock). Colors arrive at ParseDone (two_phase path).
        v->cached_raw_utf8 = std::move(res->raw_utf8);
        v->tree.reset();
        v->span_table.clear();
        v->tree_language.clear();
        do_layout(v, v->cached_raw_utf8, /*colors=*/{});
        v->state = wlx::runtime::host::LoadState::Ready;
        WLX_TRACE(L"text ready: %zu bytes shown plain", v->cached_raw_utf8.size());
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
```

(Verify `LoadState::Ready` spelling and the `WLX_TRACE` style against neighbors. `do_layout` with empty colors on the no-wrap path builds the grid skeleton in milliseconds; phase 1 is gated to no-wrap by the PRODUCER in Task 2, but the handler itself is mode-agnostic — if it ever ran under wrap it would do a plain eager layout, correct just slower.)

- [ ] **Step 3: Close-drain.** In `ListCloseWindow`, after the existing `SweepResultColor` drain loop, add the third loop mirroring its exact structure/comments:

```cpp
        while (PeekMessageW(&msg, v->hwnd, wm_colorizer_text_ready(),
                            wm_colorizer_text_ready(), PM_REMOVE))
            delete reinterpret_cast<TextResultColor*>(msg.lParam);
```

(Adapt to the real drain pattern — read the two existing loops and copy their shape verbatim, including any comment.)

- [ ] **Step 4: Build + suites.**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe && ./build/Release/tests.exe`
Expected: clean /W4 /WX (the handler and struct are referenced — the drain uses the struct, the handler is reachable; if the compiler flags `two_phase` as unused, that's expected-dormant and fine since it's a data member, not a local). 202 + 280 green.

- [ ] **Step 5: Commit.**

```bash
git add src/plugin_colorizer/window/colorizer_host_adapter.cpp
git commit -m "feat(colorizer): TextReady message + phase-1 adopt handler + close drain (dormant)"
```

---

### Task 2: Worker two-post + slimmed phase-2 adopt

**Files:**
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp` only

- [ ] **Step 1: `color_parse_body` gains the two-phase contract.** Current signature: `color_parse_body(const ParseJob& j, std::string raw_utf8, std::string language, bool wrap_text, WlxCore* core)` — it stores `r->raw_utf8 = std::move(raw_utf8)`, prewarms, decides the tree path, parses from `r->raw_utf8`. Add a `bool two_phase` parameter (last) and, after the parse block, drop the payload when phase 1 already delivered it:

```cpp
    if (two_phase) {
        // Phase 1 already delivered the source to the UI; don't ship a second
        // copy through the message queue.
        r->raw_utf8.clear();
        r->raw_utf8.shrink_to_fit();
        r->two_phase = true;
    }
    return r;
```

Update the `begin_async_recolor` caller to pass `/*two_phase=*/false` (the recolor funnel stays single-phase: the UI already shows the text; its adopt must do the full swap because dark/wrap/lang changed the colors' meaning, not the bytes).

- [ ] **Step 2: The producer.** In `begin_async_load`'s worker lambda, after the successful read and before `color_parse_body`, post phase 1 (no-wrap only). The lambda already captures `language, wrap, core`; the full new body:

```cpp
        [language, wrap, core](const wlx::runtime::host::ParseJob& j)
            -> std::unique_ptr<ParseResultColor> {
            wlx::runtime::io::FileService fs;             // worker-LOCAL, stateless
            std::optional<wlx::runtime::io::FileContent> content;
            if (!file_too_large(j.path.c_str()))   // re-check: F2 reload may have grown the file
                content = fs.read(j.path.c_str());
            if (!content) {
                auto r = std::make_unique<ParseResultColor>();
                r->generation = j.generation;
                r->live = j.live;
                r->failed = true;
                return r;
            }
            // Phase 1 (no-wrap only): post a COPY of the source so the UI can
            // show plain text now; keep the original for the parse below.
            // Mirrors spawn_parse_worker's posting discipline: re-check the
            // shutdown/closed gates, free locally on any failure to deliver.
            bool posted_text = false;
            if (!wrap) {
                auto t = std::make_unique<TextResultColor>();
                t->generation = j.generation;
                t->live = j.live;
                t->raw_utf8 = content->raw_utf8;          // copy
                if (!wlx::runtime::host::g_shutting_down.load(std::memory_order_acquire) &&
                    !j.live->closed.load(std::memory_order_acquire)) {
                    TextResultColor* raw = t.release();
                    if (PostMessage(j.hwnd, wm_colorizer_text_ready(),
                                    static_cast<WPARAM>(j.generation),
                                    reinterpret_cast<LPARAM>(raw)))
                        posted_text = true;
                    else
                        t.reset(raw);   // post failed -> reclaim + free locally
                }
            }
            // Spec's measurement stamps: read->text-ready and text-ready->
            // parse-done, visible in DebugView.
            const auto t_text = _clk::now();
            WLX_TRACE(L"two-phase: read+post %.1f ms (posted=%d)",
                      std::chrono::duration<double, std::milli>(t_text - t_read0).count(),
                      posted_text ? 1 : 0);
            auto r = color_parse_body(j, std::move(content->raw_utf8),
                                      language, wrap, core, posted_text);
            WLX_TRACE(L"two-phase: parse %.1f ms",
                      std::chrono::duration<double, std::milli>(_clk::now() - t_text).count());
            return r;
        }
```

(with `using _clk = std::chrono::steady_clock;` and `const auto t_read0 = _clk::now();` stamped at the top of the lambda body, before the read. The file already includes `<chrono>`.)

Note the fallback correctness: if the phase-1 post FAILS (dead hwnd / full queue), `posted_text` stays false → `color_parse_body` keeps `raw_utf8` in the result and `two_phase` false → phase 2 performs today's full single-phase adopt. The UI can never end up with a `two_phase` result it has no bytes for.

- [ ] **Step 3: Slimmed phase-2 adopt.** In the `wm_colorizer_parse_done()` handler's SUCCESS path (after the `failed` branch), the current code swaps `cached_raw_utf8`/`tree_language`, clears the table, converts the tree, applies the wrap-mismatch guard, then branches tree/fallback. Restructure to branch on `res->two_phase` FIRST:

```cpp
        v->state = wlx::runtime::host::LoadState::Ready;
        ...existing failed branch unchanged...
        if (res->two_phase) {
            // Phase 1 already swapped the source + plain skeleton; the bytes
            // didn't change, so the skeleton, line index, search index, scroll
            // and any selection made meanwhile are all still exact. Just bring
            // the colors in.
            v->tree_language = std::move(res->language);
            if (WlxTree* raw_tree = res->tree.release())
                v->tree = std::shared_ptr<WlxTree>(raw_tree,
                                                   wlx_core::TreeDeleter{g_colorizer_handle});
            else
                v->tree.reset();
            if (v->tree && res->wrap != v->wrap_text)
                v->tree.reset();   // wrap flipped mid-gap (defensive; gen bump normally catches it)
            if (v->tree) {
                // Window blocks were built plain — drop them; the next paint's
                // ensure_grid_window rebuilds the visible window through
                // colors_for (tree highlight). Everything else stays put.
                if (v->layout) {
                    v->layout->blocks.clear();
                    v->layout->first_block_line = 0;
                }
                begin_sweep(v);
            } else {
                // Unsupported language -> already plain, done. Supported but
                // parse failed -> whole-doc colorize fallback (feeds the span
                // table through do_layout, identical to today's semantics).
                apply_whole_doc_fallback(v, v->tree_language);
            }
            WLX_TRACE(L"parse done (two-phase): tree=%d", v->tree ? 1 : 0);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        ...existing single-phase swap + branches, byte-for-byte unchanged...
```

CAREFUL with the existing code: today's success path begins with `v->cached_raw_utf8 = std::move(res->raw_utf8);` etc. — the `two_phase` branch must come BEFORE any of that. Read the handler at HEAD and place the branch precisely; the single-phase tail stays untouched (wrap mode, failed-post fallback). NOTE on `apply_whole_doc_fallback` in the two_phase branch: it reads `v->cached_raw_utf8` (already correct from phase 1) and calls `do_layout`, which rebuilds the skeleton + InteractionEngine and sets `index_dirty` — acceptable on this rare path (unsupported/parse-fail); the common path (tree) does NOT relayout.

- [ ] **Step 4: Phase-2 window-clear test.** Append to `tests/plugin_colorizer/layout/test_grid_window.cpp` (and stage it in this task's commit):

```cpp
TEST_CASE("phase-2 window clear: re-slide rebuilds colored at the same scroll") {
    // Simulates the two-phase ParseDone adopt: a window built PLAIN at a
    // scrolled position is cleared (blocks + first_block_line only) and the
    // next slide rebuilds it WITH colors — geometry/line index untouched.
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    ThemeService theme;
    ColorizerDisplayConfig display;
    const std::string raw = numbered_lines(100);
    std::vector<int> starts;
    std::shared_ptr<MaterializeCtx> ctx;
    GridGeometry geo;
    auto doc = layout_grid_skeleton(factory.Get(), raw, theme, false, 800.0f,
                                    display, &starts, &ctx, &geo);
    const auto tops_before = doc.line_tops;
    const float height_before = doc.total_height;

    slide_grid_window(doc, geo, *ctx, raw, starts, 40, 60, empty_colors());  // plain
    REQUIRE(doc.blocks.size() == 21);
    CHECK(doc.blocks[5].text_runs[0].color_ranges.empty());

    doc.blocks.clear();              // the phase-2 adopt's exact mutation
    doc.first_block_line = 0;

    ColorSpan s;                     // a crafted span on line 45's text
    s.start = static_cast<uint32_t>(starts[45]);
    s.length = 4;
    s.color = 0x123456;
    ColorizeResult crafted;
    crafted.spans = {s};
    slide_grid_window(doc, geo, *ctx, raw, starts, 40, 60,
                      [&](uint32_t, uint32_t) { return crafted; });
    REQUIRE(doc.blocks.size() == 21);
    CHECK(doc.first_block_line == 40);
    CHECK_FALSE(doc.blocks[5].text_runs[0].color_ranges.empty());  // line 45 colored
    CHECK(doc.line_tops == tops_before);                           // geometry untouched
    CHECK(doc.total_height == doctest::Approx(height_before));
}
```

(Reuses the file's existing helpers — `create_dwrite_factory`, `numbered_lines`, `empty_colors`; verify names at HEAD.)

- [ ] **Step 5: Build + suites + visual.**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe && ./build/Release/tests.exe && bash scripts/visual-test.sh`
Expected: 202 + 280 green; visual 3 stages 100% (the tool is untouched; the plugin's two-phase path has no automated pixel coverage — live behavior is the soak's job, consistent with the async-loader precedent).

- [ ] **Step 6: Bench no-regression check.**

Run: `uv run scripts/bench.py --only sqlite`
Expected: all three sqlite rows within noise of the README baseline (the tool path didn't change; this guards against accidental shared-code edits).

- [ ] **Step 7: Commit.**

```bash
git add src/plugin_colorizer/window/colorizer_host_adapter.cpp tests/plugin_colorizer/layout/test_grid_window.cpp
git commit -m "feat(colorizer): two-phase open — plain text at read time, colors at parse time"
```

---

### Task 3: Docs + soak addendum + final verification

**Files:**
- Modify: `CLAUDE.md` (the "Async loading (both plugins)" section)
- Modify: `README.md` (the TC-soak TODO bullet)

- [ ] **Step 1: CLAUDE.md.** In the async-loading section, after the sentence describing `ListLoadW` returning immediately, add:

```
The colorizer opens in two phases: the worker posts the raw bytes as soon as the
read completes (`TextReady` — the view shows plain, fully interactive text via
the grid skeleton and goes Ready), then parses and posts `ParseDone` with a
`two_phase` flag (the adopt sets the tree, clears the materialized window so the
next paint re-colors it, and starts the sweep — no relayout, since the bytes
didn't change). Wrap mode and read failures stay single-phase.
```

(Adapt placement/wording to the section's actual text at HEAD; keep its density.)

- [ ] **Step 2: README soak bullet.** Extend the "Manual TC soak" TODO item with one more check:

```
two-phase open (open sqlite3.c: text readable in well under 100 ms, colors
arrive ~1 s later with scroll/selection preserved; F2 and dark-flip inside the
gap behave)
```

- [ ] **Step 3: Final verification + commit.**

Run the full set: build, both suites, visual, `uv run scripts/bench.py` (all rows vs baseline — expect noise only).

```bash
git add CLAUDE.md README.md
git commit -m "docs: two-phase colorizer open — architecture note + soak addendum"
```

---

## Risk register (for the executor)

- **Message ordering is load-bearing:** phase 1 and phase 2 are posted from the SAME worker thread to the SAME window — Win32 guarantees queue order, so phase 2 can never adopt before phase 1 of its generation. Do not move the phase-1 post onto a different thread.
- **`two_phase` ⇒ the UI has the bytes:** the only path setting it is a successful phase-1 PostMessage; both posts carry the same generation, and `should_adopt_result` is deterministic in (token, gen, closed) — so if phase 2 adopts, phase 1 either adopted already or was dropped *with* phase 2 (same gate). The handler may still guard `v->cached_raw_utf8.empty()` defensively before `begin_sweep` if paranoid — `begin_sweep` already no-ops on empty raw.
- **Never touch the single-phase tail:** wrap mode, read failures, and failed-post fallbacks must keep byte-identical behavior; the visual suite + suites prove the dormant cases but the wrap path is only soak-verifiable.
- **No `async_loader.h` changes** — the mid-body post replicates the discipline locally; if you feel the urge to generalize it into the template, don't (YAGNI; one consumer).
