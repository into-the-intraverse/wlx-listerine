# Memory Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut per-open-file memory from ~40–60× file size to ~"raw bytes + span table + a materialized window" (sqlite3.c 349.5 → ≤ 70 MB held, json.hpp 53.7 → ≤ 35 MB), and make scroll memory bounded — per the approved spec `docs/superpowers/specs/2026-06-11-memory-optimization-design.md`.

**Architecture:** Four milestones. **M1** adds a background *sweep* that chunk-highlights the whole file via the existing ABI-v5 `wlx_core_highlight_range` into a compact plugin-side `SpanTable`, then frees the tree-sitter tree (the single biggest retained item). **M2** replaces the colorizer's eager one-`LayoutBlock`-per-line skeleton with an *implicit grid*: `LayoutDocument.blocks` holds only a materialized viewport±overscan window; geometry is arithmetic; text decodes on demand from the one retained UTF-8 copy. **M3** instruments the markdown bench per phase, adds md layout eviction and a CacheService byte budget. **M4** re-baselines the bench/README and runs the manual TC soak.

**Tech Stack:** C++20/MSVC, Direct2D/DirectWrite, tree-sitter via `wlx-listerine-core.dll` C ABI v5 (unchanged), doctest, CMake+Conan, `scripts/bench.py` (Python stdlib), `scripts/visual-test.sh`.

**Build & test commands used throughout:**

```bash
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe          # ~200 cases
./build/Release/tests.exe                    # ~270 cases
./build/Release/colorizer-tests.exe -tc="*span table*"   # doctest filter syntax
./scripts/visual-test.sh                     # 29 goldens + token snapshots + smokes
uv run scripts/bench.py                      # perf/memory vs README baseline
```

**One deliberate spec refinement (flag to reviewer):** the spec says "fixed byte chunks (~256 KB)". Measured highlight cost is ~7 ms/KB on pathological inputs (json.hpp): a fixed 256 KB chunk would hold the core mutex ~2 s per chunk and stall any concurrent viewport highlight. The sweep therefore uses **adaptive chunk sizing targeting ~25 ms per chunk** (start 64 KB, clamp [16 KB, 1 MB]). Same design, bounded mutex holds.

---

## Milestone 1 — Sweep-highlight + free tree (keeps today's skeleton)

Expected effect on its own (sqlite3.c): tree (~100–150 MB) + `cached_text` (~17.6 MB) gone → held ~349 → **~180–210 MB**. Verified at the M1 checkpoint; final targets apply after M2.

### Task 1: `SpanTable` — compact whole-file span store

**Files:**
- Create: `src/plugin_colorizer/colorize/span_table.h`
- Create: `src/plugin_colorizer/colorize/span_table.cpp`
- Create: `src/plugin_colorizer/colorize/sweep_chunk.h`
- Test: `tests/plugin_colorizer/colorize/test_span_table.cpp`
- Modify: `src/plugin_colorizer/CMakeLists.txt` (add `colorize/span_table.cpp` to the plugin sources)
- Modify: `tests/CMakeLists.txt` (colorizer-tests: add the test file and `${CMAKE_SOURCE_DIR}/src/plugin_colorizer/colorize/span_table.cpp`)
- Modify: `src/tools/screenshot/CMakeLists.txt` (add `${CMAKE_SOURCE_DIR}/src/plugin_colorizer/colorize/span_table.cpp` — the tool drives the sweep synchronously in Task 2)

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/plugin_colorizer/colorize/test_span_table.cpp
#include <doctest/doctest.h>

#include "plugin_colorizer/colorize/span_table.h"
#include "plugin_colorizer/colorize/sweep_chunk.h"

using wlx::core::colorizer::ColorSpan;
using wlx::core::colorizer::ColorizeResult;
using wlx::plugin_colorizer::colorize::SpanTable;
using wlx::plugin_colorizer::colorize::next_chunk_bytes;

static ColorSpan span(uint32_t start, uint32_t len, uint32_t color = 0xABCDEF) {
    ColorSpan s;
    s.start = start;
    s.length = len;
    s.color = color;
    return s;
}

TEST_CASE("span table: append + slice round-trips a single chunk") {
    SpanTable t;
    ColorizeResult chunk;
    chunk.spans = {span(0, 5), span(10, 4), span(20, 6)};
    t.append_chunk(chunk, 0, 30);
    CHECK(t.swept_hi() == 30);
    CHECK(t.size() == 3);
    auto all = t.slice(0, 30);
    REQUIRE(all.spans.size() == 3);
    CHECK(all.spans[1].start == 10);
}

TEST_CASE("span table: boundary-crossing span is not duplicated across chunks") {
    // highlight_range returns every span OVERLAPPING the window, so a span
    // crossing a chunk edge comes back from BOTH chunks. append_chunk keeps
    // only spans whose START is inside the chunk window.
    SpanTable t;
    ColorizeResult c1;
    c1.spans = {span(0, 5), span(28, 10)};   // 28..38 crosses the 0..30 edge
    t.append_chunk(c1, 0, 30);
    ColorizeResult c2;
    c2.spans = {span(28, 10), span(40, 3)};  // 28-starter repeats; must be dropped
    t.append_chunk(c2, 30, 60);
    CHECK(t.size() == 3);
    auto s = t.slice(0, 60);
    REQUIRE(s.spans.size() == 3);
    CHECK(s.spans[1].start == 28);
    CHECK(s.spans[2].start == 40);
}

TEST_CASE("span table: slice includes the predecessor span overlapping lo") {
    SpanTable t;
    ColorizeResult chunk;
    chunk.spans = {span(0, 5), span(8, 10), span(30, 2)};  // 8..18 overlaps lo=12
    t.append_chunk(chunk, 0, 40);
    auto s = t.slice(12, 31);
    REQUIRE(s.spans.size() == 2);
    CHECK(s.spans[0].start == 8);
    CHECK(s.spans[1].start == 30);
}

TEST_CASE("span table: slice excludes spans entirely outside [lo,hi)") {
    SpanTable t;
    ColorizeResult chunk;
    chunk.spans = {span(0, 5), span(10, 5), span(20, 5)};
    t.append_chunk(chunk, 0, 30);
    CHECK(t.slice(5, 10).spans.empty());     // gap between spans
    CHECK(t.slice(25, 30).spans.empty());    // past the last span end
    auto s = t.slice(10, 11);
    REQUIRE(s.spans.size() == 1);
    CHECK(s.spans[0].start == 10);
}

TEST_CASE("span table: completeness watermark") {
    SpanTable t;
    CHECK(t.complete(0));          // empty file is trivially complete
    CHECK(!t.complete(100));
    t.append_chunk({}, 0, 60);     // chunk with no spans still advances the sweep
    CHECK(t.swept_hi() == 60);
    CHECK(!t.complete(100));
    t.append_chunk({}, 60, 100);
    CHECK(t.complete(100));
    t.clear();
    CHECK(!t.complete(100));
    CHECK(t.size() == 0);
}

TEST_CASE("adaptive sweep chunk targets ~25ms of highlight per chunk") {
    // pathological language: 64 KB took 480 ms -> shrink hard, clamped at 16 KB
    CHECK(next_chunk_bytes(64 * 1024, 480.0) == 16 * 1024);
    // fast language: 64 KB took 2 ms -> grow, clamped at 1 MB
    CHECK(next_chunk_bytes(64 * 1024, 2.0) == 1024 * 1024);
    // on-target stays put (within clamps)
    CHECK(next_chunk_bytes(256 * 1024, 25.0) == 256 * 1024);
    // degenerate timing -> max growth, no div-by-zero
    CHECK(next_chunk_bytes(64 * 1024, 0.0) == 1024 * 1024);
}
```

- [ ] **Step 2: Run to verify the tests fail to compile**

Run: `cmake --build --preset conan-release` (after adding the files to `tests/CMakeLists.txt`)
Expected: FAIL — `span_table.h: No such file or directory`.

- [ ] **Step 3: Implement `SpanTable` + `next_chunk_bytes`**

```cpp
// src/plugin_colorizer/colorize/span_table.h
#pragma once

#include "core_dll/colorizer/colorize_result.h"

#include <cstdint>
#include <vector>

namespace wlx::plugin_colorizer::colorize {

// Whole-file syntax colors in compact flat form: ColorSpans sorted by byte
// start, non-overlapping (QueryHighlighter's output contract), plus a
// watermark of how far the linear sweep has progressed. Once complete() the
// table replaces the retained tree-sitter tree: scroll coloring becomes a
// binary-searched slice instead of a tree query.
//
// Single writer: the sweep appends in file order (chunk_lo == swept_hi()), so
// the vector is sorted by construction and never needs a merge.
class SpanTable {
public:
    // Append one sweep chunk's highlight result for the byte window
    // [chunk_lo, chunk_hi). Keeps only spans whose START lies inside the
    // window: highlight_range returns every span OVERLAPPING the window, so a
    // span crossing the previous chunk's end arrives twice — its start
    // attributes it to exactly one chunk. Requires chunk_lo == swept_hi().
    void append_chunk(const wlx::core::colorizer::ColorizeResult& chunk,
                      uint32_t chunk_lo, uint32_t chunk_hi);

    // All spans overlapping [lo, hi), in start order. Because spans are
    // non-overlapping and sorted, at most ONE span starting before lo can
    // overlap it (the predecessor) — step back one after the lower_bound.
    wlx::core::colorizer::ColorizeResult slice(uint32_t lo, uint32_t hi) const;

    bool complete(uint32_t file_size) const { return swept_hi_ >= file_size; }
    uint32_t swept_hi() const { return swept_hi_; }
    size_t size() const { return spans_.size(); }
    size_t approx_bytes() const {
        return spans_.capacity() * sizeof(wlx::core::colorizer::ColorSpan);
    }
    void clear() { spans_.clear(); spans_.shrink_to_fit(); swept_hi_ = 0; }

private:
    std::vector<wlx::core::colorizer::ColorSpan> spans_;
    uint32_t swept_hi_ = 0;
};

}  // namespace wlx::plugin_colorizer::colorize
```

```cpp
// src/plugin_colorizer/colorize/span_table.cpp
#include "plugin_colorizer/colorize/span_table.h"

#include <algorithm>

namespace wlx::plugin_colorizer::colorize {

using wlx::core::colorizer::ColorizeResult;
using wlx::core::colorizer::ColorSpan;

void SpanTable::append_chunk(const ColorizeResult& chunk,
                             uint32_t chunk_lo, uint32_t chunk_hi) {
    for (const ColorSpan& s : chunk.spans) {
        if (s.start < chunk_lo || s.start >= chunk_hi) continue;  // owned by a neighbor chunk
        spans_.push_back(s);
    }
    swept_hi_ = chunk_hi;
}

ColorizeResult SpanTable::slice(uint32_t lo, uint32_t hi) const {
    ColorizeResult out;
    if (lo >= hi || spans_.empty()) return out;
    auto first = std::lower_bound(
        spans_.begin(), spans_.end(), lo,
        [](const ColorSpan& s, uint32_t v) { return s.start < v; });
    // The predecessor may still overlap lo (non-overlapping => at most one).
    if (first != spans_.begin()) {
        auto prev = std::prev(first);
        if (prev->start + prev->length > lo) first = prev;
    }
    for (auto it = first; it != spans_.end() && it->start < hi; ++it) {
        if (it->start + it->length <= lo) continue;  // ends before the window
        out.spans.push_back(*it);
    }
    return out;
}

}  // namespace wlx::plugin_colorizer::colorize
```

```cpp
// src/plugin_colorizer/colorize/sweep_chunk.h
#pragma once

#include <algorithm>
#include <cstdint>

namespace wlx::plugin_colorizer::colorize {

// Adaptive sweep chunk sizing. Each chunk's highlight holds the process-wide
// core mutex; a fixed 256 KB chunk would hold it ~2 s on pathological inputs
// (template-heavy C++ runs ~7 ms/KB), stalling concurrent viewport
// highlights. Target ~25 ms per chunk instead, learned from the previous
// chunk's measured cost.
inline constexpr uint32_t kSweepFirstChunkBytes = 64 * 1024;
inline constexpr uint32_t kSweepMinChunkBytes   = 16 * 1024;
inline constexpr uint32_t kSweepMaxChunkBytes   = 1024 * 1024;

inline uint32_t next_chunk_bytes(uint32_t prev_bytes, double prev_ms) {
    constexpr double kTargetMs = 25.0;
    if (prev_ms <= 0.01) return kSweepMaxChunkBytes;
    const double scaled = static_cast<double>(prev_bytes) * (kTargetMs / prev_ms);
    return static_cast<uint32_t>(std::clamp(
        scaled, static_cast<double>(kSweepMinChunkBytes),
        static_cast<double>(kSweepMaxChunkBytes)));
}

}  // namespace wlx::plugin_colorizer::colorize
```

Also add the pure abort predicate the sweep worker loop will use (spec's
"sweep abort-on-generation" test target), in the same header:

```cpp
#include "runtime/host/async_loader.h"

namespace wlx::plugin_colorizer::colorize {

// True when an in-flight sweep must vanish at the next chunk edge: the view
// was superseded (generation bumped by reload/relang/dark-flip), closed, or
// the module is detaching.
inline bool sweep_superseded(const wlx::runtime::host::ViewLiveToken& live,
                             uint64_t my_generation) {
    return wlx::runtime::host::g_shutting_down.load(std::memory_order_acquire) ||
           live.closed.load(std::memory_order_acquire) ||
           live.generation.load(std::memory_order_acquire) != my_generation;
}

}  // namespace wlx::plugin_colorizer::colorize
```

with this test appended to `test_span_table.cpp`:

```cpp
TEST_CASE("sweep abort: generation bump / close flag cancel between chunks") {
    using wlx::runtime::host::ViewLiveToken;
    using wlx::plugin_colorizer::colorize::sweep_superseded;
    ViewLiveToken live;
    live.generation.store(7);
    CHECK(!sweep_superseded(live, 7));
    live.generation.store(8);                 // reload/relang/dark-flip bumped it
    CHECK(sweep_superseded(live, 7));
    live.generation.store(7);
    live.closed.store(true);                  // ListCloseWindow
    CHECK(sweep_superseded(live, 7));
}
```

- [ ] **Step 4: Run the tests, verify they pass**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe -tc="*span table*,*sweep chunk*"`
Expected: all new cases PASS; full `colorizer-tests.exe` still green.

- [ ] **Step 5: Commit**

```bash
git add src/plugin_colorizer/colorize tests/plugin_colorizer/colorize tests/CMakeLists.txt src/plugin_colorizer/CMakeLists.txt src/tools/screenshot/CMakeLists.txt
git commit -m "feat(colorizer): SpanTable + adaptive sweep chunk sizing (memory M1 groundwork)"
```

### Task 2: Chunked sweep == whole-file highlight (parity proof) + synchronous sweep in the screenshot tool

**Files:**
- Test: `tests/plugin_colorizer/colorize/test_sweep_parity.cpp` (add to colorizer-tests in `tests/CMakeLists.txt`)
- Modify: `src/tools/screenshot/colorizer_pipeline.cpp` (cached-tree route, after first paint ~line 482)
- Modify: `src/tools/screenshot/options.h` + `src/tools/screenshot/main.cpp` (new `--scroll-screens N` flag)

- [ ] **Step 1: Write the failing parity test (uses the real core DLL — colorizer-tests links it)**

```cpp
// tests/plugin_colorizer/colorize/test_sweep_parity.cpp
#include <doctest/doctest.h>

#include "plugin_colorizer/colorize/span_table.h"
#include "plugin_colorizer/colorize/sweep_chunk.h"
#include "wlx_core/abi.h"
#include "wlx_core/abi_spans_to_result.h"

#include <string>

using namespace wlx::plugin_colorizer::colorize;

// A chunked sweep through the table must reproduce the exact span sequence of
// one whole-file highlight_range call — this is the "table-served colors are
// byte-identical to tree-served colors" guarantee from the spec.
TEST_CASE("sweep parity: chunked table == single whole-file highlight") {
    WlxCore* core = wlx_core::acquire_compatible();
    REQUIRE(core != nullptr);
    std::string src;
    for (int i = 0; i < 400; ++i)
        src += "static int value_" + std::to_string(i) +
               " = " + std::to_string(i) + "; /* trailing comment */\n";
    wlx_core::TreePtr tree(wlx_core_parse(core, src.c_str(),
                                          static_cast<uint32_t>(src.size()), "cpp"),
                           wlx_core::TreeDeleter{core});
    REQUIRE(tree);

    WlxColorSpan* spans = nullptr;
    uint32_t count = 0;
    REQUIRE(wlx_core_highlight_range(core, tree.get(), 1, 0, 0, &spans, &count) == 0);
    auto whole = wlx_core::abi_spans_to_result(spans, count);  // whole-doc reference

    SpanTable table;
    const auto fsize = static_cast<uint32_t>(src.size());
    uint32_t chunk = 1024;                       // tiny chunks: maximal boundary stress
    while (!table.complete(fsize)) {
        const uint32_t lo = table.swept_hi();
        const uint32_t hi = std::min(fsize, lo + chunk);
        WlxColorSpan* cs = nullptr;
        uint32_t cc = 0;
        REQUIRE(wlx_core_highlight_range(core, tree.get(), 1, lo, hi, &cs, &cc) == 0);
        table.append_chunk(wlx_core::abi_spans_to_result(cs, cc), lo, hi);
    }

    auto swept = table.slice(0, fsize);
    REQUIRE(swept.spans.size() == whole.spans.size());
    for (size_t i = 0; i < whole.spans.size(); ++i) {
        CHECK(swept.spans[i].start == whole.spans[i].start);
        CHECK(swept.spans[i].length == whole.spans[i].length);
        CHECK(swept.spans[i].color == whole.spans[i].color);
        CHECK(swept.spans[i].modifiers == whole.spans[i].modifiers);
    }
}
```

- [ ] **Step 2: Run to verify it fails (file not yet in CMake) then passes once added**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe -tc="*sweep parity*"`
Expected: PASS (the implementation already exists from Task 1; this step pins the ABI contract — if highlight_range clipped spans at range edges instead of returning full extents, this test is what catches it).
Note: colorizer-tests must run from the repo root (grammars/ + config/themes/ resolve relative to cwd) — same as the existing ABI tests.

- [ ] **Step 3: Add the synchronous sweep + settle sampling to the tool's cached-tree route**

In `src/tools/screenshot/colorizer_pipeline.cpp`, after the first paint of the cached-tree route (after `_tpaint_ct`, ~line 482) and BEFORE the bench print block:

```cpp
    // ---- sweep: chunk-highlight the whole file into the span table, then
    // free the tree (memory M1). Bench "process delta" must be sampled AFTER
    // this settle point, otherwise the freed tree never shows up.
    wlx::plugin_colorizer::colorize::SpanTable sweep_table;
    auto _tsweep0 = _clk::now();
    {
        const auto fsize = static_cast<uint32_t>(content->raw_utf8.size());
        uint32_t chunk = wlx::plugin_colorizer::colorize::kSweepFirstChunkBytes;
        while (!sweep_table.complete(fsize)) {
            const uint32_t lo = sweep_table.swept_hi();
            const uint32_t hi = std::min(fsize, lo + chunk);
            WlxColorSpan* cs = nullptr;
            uint32_t cc = 0;
            auto c0 = _clk::now();
            if (wlx_core_highlight_range(core, tree.get(), opts.dark ? 1 : 0,
                                         lo, hi, &cs, &cc) != 0)
                break;   // sweep failure: keep the tree (spec fallback)
            sweep_table.append_chunk(wlx_core::abi_spans_to_result(cs, cc), lo, hi);
            chunk = wlx::plugin_colorizer::colorize::next_chunk_bytes(
                hi - lo, ms(c0, _clk::now()));
        }
        if (sweep_table.complete(fsize))
            tree.reset();   // free tree + unpin grammar — the settle point
    }
    auto _tsweep1 = _clk::now();

    // ---- post-scroll pass (--scroll-screens N): slide the viewport down N
    // screens, recoloring + repainting per step, so the FINAL working-set
    // sample measures post-scroll retention (the new bench row).
    for (int step = 0; step < opts.scroll_screens; ++step) {
        scroll_y_ct += viewport_h;
        auto vr2 = wlx::plugin_colorizer::layout::viewport_byte_range(
            layout_ct.blocks, line_byte_starts,
            static_cast<int>(content->raw_utf8.size()),
            scroll_y_ct, viewport_h, viewport_h);
        if (!vr2.empty) {
            auto step_spans = sweep_table.slice(vr2.lo, vr2.hi);
            apply_spans_to_range(layout_ct, content->raw_utf8, line_byte_starts,
                                 step_spans, vr2.lo, vr2.hi, display.tab_width);
        }
        renderer_ct.paint(layout_ct, scroll_y_ct);
    }
```

Then move the existing cached-tree working-set sample (currently ~line 525) to AFTER this block, and extend the bench print block with:

```cpp
        std::fprintf(stderr, "  sweep      %6.2f ms  (%zu spans, %.1f MB table)\n",
                     ms(_tsweep0, _tsweep1), sweep_table.size(),
                     sweep_table.approx_bytes() / (1024.0 * 1024.0));
```

The existing `hot total`, `peak workingset`, and `process delta` labels stay byte-identical (bench.py regexes untouched); only the *sample moment* of `process delta` moves to post-settle (post-scroll when `--scroll-screens` is given).

- [ ] **Step 4: Add the `--scroll-screens` flag**

In `src/tools/screenshot/options.h` add `int scroll_screens = 0;`. In `src/tools/screenshot/main.cpp` next to the existing `--scroll` parsing (~line 91): `else if (arg == L"--scroll-screens") opts.scroll_screens = parse_int(next());` (mirroring the existing parse_int error handling).

- [ ] **Step 5: Build + manual smoke**

Run:
```bash
cmake --build --preset conan-release
./build/Release/screenshot_tool.exe test_data/bench/fetched/json.hpp --colorizer --cached-tree --bench --dark --width 1000 --height 1200
```
Expected: stderr shows the new `sweep` row; `process delta` is several MB LOWER than the README's 53.7 (tree freed before sampling). PNG output unchanged → `./scripts/visual-test.sh` stays green.

- [ ] **Step 6: Commit**

```bash
git add src/tools/screenshot tests/plugin_colorizer/colorize/test_sweep_parity.cpp tests/CMakeLists.txt
git commit -m "feat(bench): synchronous sweep + settle-point memory sampling + --scroll-screens"
```

### Task 3: Host-side sweep worker — sweep, adopt table, free tree

**Files:**
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp`

- [ ] **Step 1: Make the cached tree shareable with the sweep worker**

The sweep worker and the UI thread use the tree concurrently (the core mutex serializes the actual ABI calls); close/reload must not free it under a running chunk. Change the ViewState field (line 116):

```cpp
    // OLD:  wlx_core::TreePtr tree;
    // Shared with at most one in-flight sweep worker; whichever side drops the
    // last reference frees it via wlx_core_free_tree (worker-side frees take
    // the core mutex on the worker thread — never under the loader lock, since
    // ViewStates leak on DLL_PROCESS_DETACH and workers are detached).
    std::shared_ptr<WlxTree> tree;
```

All existing `vs->tree.get()` / `vs->tree.reset()` / truthiness sites compile unchanged. The parse-adopt site (line 726) converts the worker's move-only `TreePtr`:

```cpp
        // OLD: v->tree = std::move(res->tree);
        v->tree = std::shared_ptr<WlxTree>(res->tree.release(),
                                           wlx_core::TreeDeleter{g_colorizer_handle});
```

(`TreeDeleter{core}` is callable as `void(WlxTree*)` — it is the shared_ptr deleter directly. A null release() yields an empty shared_ptr, preserving the "may be null" path.)

- [ ] **Step 2: Add the sweep message, result struct, and ViewState fields**

Next to `wm_colorizer_parse_done()` (line 174):

```cpp
// Sweep completion (memory M1): a worker chunk-highlighted the whole file into
// a SpanTable; adoption frees the tree. Distinct registered message for the
// same reasons as ParseDone.
static UINT wm_colorizer_sweep_done() {
    static const UINT m = RegisterWindowMessageW(L"WlxListerineColorizer.SweepDone");
    return m;
}

// Worker -> UI sweep result. COM-free. `aborted` => keep the tree (fallback).
struct SweepResultColor {
    uint64_t generation = 0;
    std::shared_ptr<wlx::runtime::host::ViewLiveToken> live;
    bool aborted = false;
    wlx::plugin_colorizer::colorize::SpanTable table;
};
```

In `ColorViewState` (next to `colored_lo/colored_hi`, line 126):

```cpp
    // Whole-file span table (sweep output). Once complete, scroll coloring
    // reads this instead of the tree, and the tree is freed.
    wlx::plugin_colorizer::colorize::SpanTable span_table;
```

Include `"plugin_colorizer/colorize/span_table.h"` and `"plugin_colorizer/colorize/sweep_chunk.h"` at the top of the file.

- [ ] **Step 3: Spawn the sweep after a successful tree adoption**

Add the spawn funnel (after `begin_async_recolor`, ~line 624):

```cpp
// Sweep funnel (memory M1). Spawned after a tree adoption; walks the file in
// adaptive chunks against the SHARED tree (core mutex serializes with viewport
// highlights), accumulating a SpanTable worker-side. Re-checks the live token
// between chunks: close / reload / relang / dark-flip bumps the generation and
// the sweep self-cancels at the next chunk edge.
static void begin_sweep(ColorViewState* vs) {
    if (!vs->tree || vs->cached_raw_utf8.empty()) return;
    std::shared_ptr<WlxTree> tree = vs->tree;          // shared keep-alive copy
    WlxCore* core = g_colorizer_handle;
    const bool dark = vs->dark_mode;
    const auto fsize = static_cast<uint32_t>(vs->cached_raw_utf8.size());

    auto job = std::make_unique<wlx::runtime::host::ParseJob>();
    job->path = vs->file_path;                          // tracing only
    job->generation = vs->current_gen;
    job->live = vs->live;
    job->hwnd = vs->hwnd;
    job->done_msg = wm_colorizer_sweep_done();
    wlx::runtime::host::spawn_parse_worker<SweepResultColor>(std::move(job),
        [tree, core, dark, fsize](const wlx::runtime::host::ParseJob& j)
            -> std::unique_ptr<SweepResultColor> {
            using namespace wlx::plugin_colorizer::colorize;
            using _clk = std::chrono::steady_clock;
            auto r = std::make_unique<SweepResultColor>();
            r->generation = j.generation;
            r->live = j.live;
            uint32_t chunk = kSweepFirstChunkBytes;
            while (!r->table.complete(fsize)) {
                // Superseded / closed / detaching: vanish between chunks. The
                // shared tree ref drops on return; a worker-side last-ref free
                // takes the core mutex on this thread (never the loader lock).
                if (sweep_superseded(*j.live, j.generation))
                    return nullptr;
                const uint32_t lo = r->table.swept_hi();
                const uint32_t hi = std::min(fsize, lo + chunk);
                WlxColorSpan* spans = nullptr;
                uint32_t count = 0;
                auto c0 = _clk::now();
                if (wlx_core_highlight_range(core, tree.get(), dark ? 1 : 0,
                                             lo, hi, &spans, &count) != 0) {
                    r->aborted = true;   // keep the tree; today's behavior is the fallback
                    return r;
                }
                r->table.append_chunk(wlx_core::abi_spans_to_result(spans, count), lo, hi);
                const double chunk_ms = std::chrono::duration<double, std::milli>(
                    _clk::now() - c0).count();
                chunk = next_chunk_bytes(hi - lo, chunk_ms);
            }
            return r;
        });
}
```

Call it from the parse-adopt handler, right after the tree branch's first viewport colorize (line 738):

```cpp
        if (v->tree) {
            do_layout(v, v->cached_raw_utf8, /*colors=*/{});
            colorize_viewport(v);
            begin_sweep(v);          // memory M1: sweep -> span table -> free tree
        } else {
```

- [ ] **Step 4: Adopt the sweep result**

In `ColorViewWndProc`, after the `wm_colorizer_parse_done()` block (line 745), add the sibling handler:

```cpp
    if (msg == wm_colorizer_sweep_done()) {
        std::unique_ptr<SweepResultColor> res(reinterpret_cast<SweepResultColor*>(lp));
        auto it = g_views.find(hwnd);
        if (it == g_views.end()) return 0;
        ColorViewState* v = it->second;
        if (!wlx::runtime::host::should_adopt_result(res->live.get(), res->generation,
                                                     v->live.get(), v->current_gen))
            return 0;                                  // superseded/closed -> drop
        if (res->aborted) {
            WLX_TRACE(L"sweep aborted (highlight_range failed) — keeping tree");
            return 0;                                  // memory-heavy but correct
        }
        v->span_table = std::move(res->table);
        v->tree.reset();                               // free tree + unpin grammar
        return 0;
    }
```

- [ ] **Step 5: Serve post-settle scroll colors from the table**

In `colorize_viewport` (line 411) replace the tree-only gate and the highlight call:

```cpp
    // OLD: if (!vs || !vs->tree || !vs->layout) return;
    if (!vs || !vs->layout) return;
    const bool table_ready =
        vs->span_table.complete(static_cast<uint32_t>(vs->cached_raw_utf8.size()));
    if (!vs->tree && !table_ready) return;   // wrap/unsupported fallback already colored
```

and where the result is produced (lines 429-435):

```cpp
    ColorizeResult result;
    if (table_ready) {
        result = vs->span_table.slice(vlo, vhi);       // tree already freed
    } else {
        WlxColorSpan* spans = nullptr;
        uint32_t count = 0;
        if (wlx_core_highlight_range(g_colorizer_handle, vs->tree.get(),
                                     vs->dark_mode ? 1 : 0, vlo, vhi,
                                     &spans, &count) != 0)
            return;
        result = wlx_core::abi_spans_to_result(spans, count);
    }
```

The `colored_lo/hi` interval logic above/below this stays exactly as is (it now also prevents re-applying table slices per paint).

- [ ] **Step 6: Reset the table wherever the tree is reset**

Add `vs->span_table.clear();` at each of: `begin_async_recolor` (next to `vs->tree.reset()`, line 598), the failed-adopt branch (next to `v->tree.reset()`, line 710), and the parse-adopt success path (before the `if (v->tree)` branch, since a new file's table must not survive — next to `v->colored_lo = v->colored_hi = 0;`, line 727).

- [ ] **Step 7: Build, run both suites, commit**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe && ./build/Release/tests.exe`
Expected: all green (the host changes have no unit coverage yet — live behavior is soak-tested in M4; the pure pieces were tested in Task 1/2).

```bash
git add src/plugin_colorizer/window/colorizer_host_adapter.cpp
git commit -m "feat(colorizer): background sweep worker — span table adopted, tree freed after settle"
```

### Task 4: Delete `cached_text` (the dead UTF-16 whole-file copy)

**Files:**
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp`

- [ ] **Step 1: Verify deadness once more (cheap insurance)**

Run: `rg -n "cached_text" src/plugin_colorizer src/tools src/runtime`
Expected: hits ONLY inside `colorizer_host_adapter.cpp` (field, the recolor capture, the relayout emptiness check, adopt assignment, failed-adopt clear). If anything else reads it, STOP and re-evaluate.

- [ ] **Step 2: Remove it**

- Delete the field `std::wstring cached_text;` (line 104) and the `std::wstring text; // -> cached_text` member of `ParseResultColor` (line 189).
- `color_parse_body`: drop the `std::wstring text` parameter and the `r->text = std::move(text);` line; update both callers (`begin_async_load` passes only `std::move(content->raw_utf8)`; `begin_async_recolor` drops `text_copy` entirely — only `raw_copy` remains captured).
- Adopt handler: delete `v->cached_text = std::move(res->text);` (line 723) and `v->cached_text.clear();` (line 707).
- `relayout` guard (line 627): `if (vs->cached_text.empty() && vs->file_path.empty()) return;` → `if (vs->cached_raw_utf8.empty() && vs->file_path.empty()) return;`
- `begin_async_recolor` no-op guard already checks `cached_raw_utf8` — unchanged.

- [ ] **Step 3: Build, full suites, visual suite, commit**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe && ./build/Release/tests.exe && ./scripts/visual-test.sh`
Expected: all green, goldens 100%.

```bash
git add src/plugin_colorizer/window/colorizer_host_adapter.cpp
git commit -m "refactor(colorizer): drop cached_text — layout/selection/search consume raw_utf8 only"
```

### Task 5: bench.py post-scroll scenario

**Files:**
- Modify: `scripts/bench.py` (SCENARIOS, line 55-62)

- [ ] **Step 1: Add the scenario**

```python
SCENARIOS = [
    ("markdown",                                   BENCH_DIR / "big.md",      ["--lazy"]),
    ("C++ header json.hpp",                        FETCHED_DIR / "json.hpp",  ["--colorizer", "--cached-tree"]),
    ("C file sqlite3.c",                           FETCHED_DIR / "sqlite3.c", ["--colorizer", "--cached-tree"]),
    ("post-scroll sqlite3.c (20 screens)",         FETCHED_DIR / "sqlite3.c", ["--colorizer", "--cached-tree", "--scroll-screens", "20"]),
    ("worst case: markdown full layout",           BENCH_DIR / "big.md",      []),
    ("worst case: whole-file highlight json.hpp",  FETCHED_DIR / "json.hpp",  ["--colorizer"]),
    ("worst case: whole-file highlight sqlite3.c", FETCHED_DIR / "sqlite3.c", ["--colorizer"]),
]
```

No regex/render changes needed: the tool samples `process delta` at the end of the run, which for this scenario IS the post-scroll held number; the row appears in the README table via the generic machinery. The compare run will print "(new)" for the row until `--update`.

- [ ] **Step 2: Run the bench, record M1 interim numbers, commit**

Run: `uv run scripts/bench.py`
Expected (M1, machine-dependent): sqlite cached delta drops from ~349 to **~180–210 MB**; json.hpp from ~53.7 to **~40–45 MB**; post-scroll row prints a (large — eviction comes in M2) number; open times within +10% of baseline. Record actual numbers in the commit message. Do NOT `--update` yet (final baseline lands in M4).

```bash
git add scripts/bench.py
git commit -m "feat(bench): post-scroll memory scenario (M1 interim: sqlite held 349->NNN MB)"
```

### Task 6: M1 checkpoint

- [ ] **Step 1: Full verification**

Run all of:
```bash
cmake --build --preset conan-release
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
./scripts/visual-test.sh
uv run scripts/bench.py
```
Expected: ~470 tests green, goldens 100% (token snapshots byte-identical — the sweep changes WHEN colors are computed, never WHAT they are), bench shows the M1 memory drop with flat open times.

- [ ] **Step 2: Quick manual TC sanity (not the full soak)** — open sqlite3.c in TC lister, scroll during the first 2 s (mid-sweep), flip dark mode mid-sweep, close mid-sweep. No crashes/wrong colors. (Full soak checklist runs in M4.)

---

## Milestone 2 — Implicit grid (window-only blocks, colorizer no-wrap path)

After M2 the steady state per view is: `cached_raw_utf8` + `line_byte_starts` + `span_table` + `line_tops` + ~200 window blocks. Selection/search/hit-test switch from "block index == vector index" to **"block index == source line index"** with `LayoutDocument.first_block_line` mapping line → window slot.

### Task 7: Grid fields on LayoutDocument + pure geometry helpers

**Files:**
- Modify: `src/runtime/layout/layout_document.h`
- Create: `src/plugin_colorizer/layout/grid_geometry.h` (header-only)
- Test: `tests/plugin_colorizer/layout/test_grid_geometry.cpp` (add to colorizer-tests CMake list)

- [ ] **Step 1: Failing tests**

```cpp
// tests/plugin_colorizer/layout/test_grid_geometry.cpp
#include <doctest/doctest.h>

#include "plugin_colorizer/layout/grid_geometry.h"

using namespace wlx::plugin_colorizer::layout;

TEST_CASE("grid geometry: line tops are arithmetic and clamped") {
    GridGeometry g{/*top_pad=*/4.0f, /*line_height=*/16.0f, /*line_count=*/100};
    CHECK(grid_line_top(g, 0) == doctest::Approx(4.0f));
    CHECK(grid_line_top(g, 10) == doctest::Approx(4.0f + 160.0f));
    CHECK(grid_line_at_y(g, 0.0f) == 0);           // above top pad clamps to 0
    CHECK(grid_line_at_y(g, 4.0f + 16.0f * 3 + 0.5f) == 3);
    CHECK(grid_line_at_y(g, 1e9f) == 99);          // clamps to last line
    CHECK(grid_total_height(g) == doctest::Approx(4.0f + 1600.0f + 4.0f));
}

TEST_CASE("grid geometry: window covers viewport plus overscan, clamped") {
    GridGeometry g{4.0f, 16.0f, 100};
    auto [first, last] = grid_window_lines(g, /*scroll_y=*/0.0f,
                                           /*viewport_h=*/160.0f, /*overscan=*/160.0f);
    CHECK(first == 0);
    CHECK(last == 20);                             // 2 screens from the top, inclusive
    auto [f2, l2] = grid_window_lines(g, 16.0f * 95, 160.0f, 160.0f);
    CHECK(l2 == 99);                               // clamps at the end
    CHECK(f2 < 95);
    auto [f3, l3] = grid_window_lines(GridGeometry{4.0f, 16.0f, 0}, 0, 160, 160);
    CHECK(f3 == 0);
    CHECK(l3 == -1);                               // empty doc -> empty window
}
```

- [ ] **Step 2: Run to verify failure** — `colorizer-tests.exe -tc="*grid geometry*"` fails to compile.

- [ ] **Step 3: Implement**

`src/runtime/layout/layout_document.h` — add to the struct:

```cpp
    // Implicit-grid mode (colorizer no-wrap): `blocks` holds only a
    // materialized viewport±overscan window; blocks[i] represents source line
    // first_block_line + i, every line is exactly line_height tall, and all
    // public block indices (TextPosition, SearchMatch, HitResult) are SOURCE
    // LINE indices. grid_line_count == 0 => classic whole-file blocks (md,
    // colorizer wrap mode) and first_block_line stays 0.
    int first_block_line = 0;
    int grid_line_count = 0;
    bool is_grid() const { return grid_line_count > 0; }
```

```cpp
// src/plugin_colorizer/layout/grid_geometry.h
#pragma once

#include <algorithm>
#include <utility>

namespace wlx::plugin_colorizer::layout {

// Uniform no-wrap grid: line i spans
// [top_pad + i*line_height, top_pad + (i+1)*line_height). Mirrors the
// arithmetic the eager build loop used; single source of truth for grid mode.
struct GridGeometry {
    float top_pad = 4.0f;
    float line_height = 0.0f;
    int line_count = 0;
};

inline float grid_line_top(const GridGeometry& g, int line) {
    return g.top_pad + g.line_height * static_cast<float>(line);
}

inline float grid_total_height(const GridGeometry& g) {
    return g.top_pad + g.line_height * static_cast<float>(g.line_count) + 4.0f;
}

inline int grid_line_at_y(const GridGeometry& g, float y) {
    if (g.line_count <= 0 || g.line_height <= 0.0f) return 0;
    const int line = static_cast<int>((y - g.top_pad) / g.line_height);
    return std::clamp(line, 0, g.line_count - 1);
}

// Inclusive [first, last] line window for viewport+overscan; {0, -1} if empty.
inline std::pair<int, int> grid_window_lines(const GridGeometry& g, float scroll_y,
                                             float viewport_h, float overscan) {
    if (g.line_count <= 0) return {0, -1};
    const int first = grid_line_at_y(g, scroll_y - overscan);
    const int last = grid_line_at_y(g, scroll_y + viewport_h + overscan);
    return {first, last};
}

}  // namespace wlx::plugin_colorizer::layout
```

- [ ] **Step 4: Run tests green, full suites green** — `colorizer-tests.exe` + `tests.exe`.

- [ ] **Step 5: Commit** — `git commit -m "feat(layout): grid fields on LayoutDocument + pure grid geometry helpers"`

### Task 8: Grid skeleton in `layout_source` + `slide_grid_window`

**Files:**
- Modify: `src/plugin_colorizer/layout/colorizer_layout.h` / `.cpp`
- Create: `src/plugin_colorizer/layout/grid_window.h` / `.cpp` (add to plugin, tool, and colorizer-tests CMake lists)
- Test: `tests/plugin_colorizer/layout/test_grid_window.cpp`

- [ ] **Step 1: Expose the shared per-line primitives**

In `colorizer_layout.h`, declare (moving from file-static in the .cpp — bodies unchanged):

```cpp
// Shared per-line build primitives (also used by grid_window.cpp).
std::wstring decode_line(const std::string& raw_utf8,
                         int line_byte_start, int line_content_end_byte);
std::wstring expand_tabs(const std::wstring& line, int tab_width,
                         std::vector<int>* source_to_expanded);
struct PerLineSpan { /* keep the existing definition, moved from the .cpp */ };
void distribute_spans_to_lines(const std::string& raw_utf8,
                               const std::vector<int>& line_byte_starts,
                               int raw_utf8_size,
                               const wlx::core::colorizer::ColorizeResult& spans,
                               int line_first, int line_last,
                               std::vector<std::vector<PerLineSpan>>& out_line_spans);
std::vector<wlx::runtime::layout::ColorRange> build_color_ranges(
    const std::vector<PerLineSpan>& spans,
    const std::vector<int>& source_to_expanded, const std::wstring& expanded);
// MaterializeCtx moves to the header (minus orig_lines — deleted in Task 14);
// create_line_layout and apply_line_decorations move with it, declarations here,
// definitions stay in colorizer_layout.cpp.
```

(Exact bodies already exist at `colorizer_layout.cpp:71-105` (decode_line), `:146-258` (distribute), `:268-282` (MaterializeCtx), `:289-318` (create_line_layout), `:325-461` (apply_line_decorations). This step is mechanical de-static-ing; signatures must match the existing definitions verbatim.)

- [ ] **Step 2: Grid skeleton path in `layout_source`**

In `layout_source` (colorizer_layout.cpp:465), when `lazy` (i.e. `!display.word_wrap`), replace the per-line block build loop (lines 637-711) with a byte-scan that builds NO blocks and decodes NO text:

```cpp
    if (lazy) {
        // ---- implicit grid: no per-line blocks, no per-line decode ----
        // line_byte_starts is the only per-line state (4 B/line); blocks are
        // materialized per viewport by slide_grid_window.
        doc.grid_line_count = static_cast<int>(lines.size());
        doc.first_block_line = 0;
        double y = 4.0;
        doc.line_tops.reserve(lines.size());
        for (size_t i = 0; i < lines.size(); ++i) {
            doc.line_tops.push_back(static_cast<float>(y));
            y += line_height;
        }
        doc.total_height = static_cast<float>(y + 4.0);
        // gutter sizing below runs as today (uses lines.size() only)
    } else {
        // ---- eager path (word-wrap): existing loop, unchanged ----
        ...existing per-line build loop...
    }
```

Also change the line-split loop (lines 503-552) so the **lazy path skips `MultiByteToWideChar` entirely** (the `LineInfo::text` field is only needed by the eager path): guard the decode with `if (!display.word_wrap) { li.text stays empty }` — byte offsets are computed either way. The `doc.materialize_block` hook is NOT set in grid mode (the window is pre-built before paint); delete the lazy-hook lambda (lines 728-745) in this step's lazy branch (the eager path never set it).

- [ ] **Step 3: Failing tests for the window builder**

```cpp
// tests/plugin_colorizer/layout/test_grid_window.cpp  (key cases; follow
// test_colorizer_layout.cpp conventions: real IDWriteFactory fixture, repo-root cwd)
#include <doctest/doctest.h>
#include "plugin_colorizer/layout/colorizer_layout.h"
#include "plugin_colorizer/layout/grid_window.h"

TEST_CASE("grid window: slide builds entering lines and drops leaving ones") {
    // raw source of 100 numbered lines; grid layout_source skeleton; ctx from helper.
    // slide to lines [0,20] -> 21 blocks, first_block_line==0, every block has
    // run.layout != null, run.text == expanded source line, rect.top ==
    // grid_line_top(geo, line).
    // slide to [40,60] -> blocks for [0,20] destroyed (capacity reclaimed),
    // 21 new blocks, first_block_line==40.
    // slide to [50,70] (overlap) -> blocks [50,60] are the SAME IDWriteTextLayout
    // pointers as before (reuse, no rebuild); [40,49] gone; [61,70] new.
}

TEST_CASE("grid window: parity with the eager whole-file build") {
    // Small input WITH tabs + a UTF-8 multibyte char + an URL. Build once with
    // word_wrap=false OLD-STYLE (call slide over the full range [0, n-1]) and
    // once with the eager path (word_wrap=true forces eager but changes
    // wrapping; instead compare against per-line expected: decode+expand text
    // equality, rect arithmetic vs grid_line_top, color_ranges from a crafted
    // span set distributed via distribute_spans_to_lines).
}

TEST_CASE("grid window: empty file, single huge line, last-line window clamp") {
    // empty raw -> 1 empty line, slide [0,0] builds 1 placeholder block;
    // one 50k-char line -> slide [0,0] builds it (no crash, layout non-null);
    // window past EOF clamps to last line.
}
```

(Write these as real code against the API of Step 4 — the case skeletons above define the assertions; the executor fills the ~30 lines of setup per case using the existing `test_colorizer_layout.cpp` fixtures.)

- [ ] **Step 4: Implement `slide_grid_window`**

```cpp
// src/plugin_colorizer/layout/grid_window.h
#pragma once

#include "plugin_colorizer/layout/colorizer_layout.h"
#include "plugin_colorizer/layout/grid_geometry.h"
#include "runtime/layout/layout_document.h"

#include <functional>
#include <string>
#include <vector>

namespace wlx::plugin_colorizer::layout {

// Colors for a byte range: post-settle this slices the SpanTable; mid-sweep it
// calls wlx_core_highlight_range against the live tree; plain-text mode
// returns an empty result.
using ColorsForRange =
    std::function<wlx::core::colorizer::ColorizeResult(uint32_t lo, uint32_t hi)>;

// Slide doc.blocks (grid mode) to cover source lines [first, last] inclusive.
// Blocks already inside the new window are MOVED (their IDWriteTextLayouts
// survive untouched); entering lines are built (decode -> expand -> colors ->
// CreateTextLayout -> decorations); leaving lines are destroyed — this IS the
// colorizer's layout eviction. Colors for entering lines are fetched with ONE
// ColorsForRange call per contiguous entering range.
void slide_grid_window(wlx::runtime::layout::LayoutDocument& doc,
                       const GridGeometry& geo,
                       MaterializeCtx& ctx,
                       const std::string& raw_utf8,
                       const std::vector<int>& line_byte_starts,
                       int first, int last,
                       const ColorsForRange& colors_for);

// Build one line's block (exposed for tests). spans_for_line is window-relative
// output of distribute_spans_to_lines for this line.
wlx::runtime::layout::LayoutBlock build_grid_line(
    int line, const GridGeometry& geo, MaterializeCtx& ctx,
    const std::string& raw_utf8, const std::vector<int>& line_byte_starts,
    const std::vector<PerLineSpan>& spans_for_line);

}  // namespace wlx::plugin_colorizer::layout
```

```cpp
// src/plugin_colorizer/layout/grid_window.cpp
#include "plugin_colorizer/layout/grid_window.h"

#include <algorithm>

namespace wlx::plugin_colorizer::layout {

using wlx::runtime::layout::LayoutBlock;
using wlx::runtime::layout::LayoutDocument;
using wlx::runtime::layout::TextRun;

LayoutBlock build_grid_line(int line, const GridGeometry& geo, MaterializeCtx& ctx,
                            const std::string& raw_utf8,
                            const std::vector<int>& line_byte_starts,
                            const std::vector<PerLineSpan>& spans_for_line) {
    const int raw_size = static_cast<int>(raw_utf8.size());
    const int line_count = static_cast<int>(line_byte_starts.size());
    const int byte_start = line_byte_starts[line];
    const int content_end = (line + 1 < line_count)
                                ? line_byte_starts[line + 1] - 1   // strip '\n'
                                : raw_size;
    std::wstring orig = decode_line(raw_utf8, byte_start,
                                    std::max(byte_start, content_end));
    std::vector<int> source_to_expanded;
    std::wstring expanded = expand_tabs(orig, ctx.tab_width, &source_to_expanded);

    LayoutBlock lb;
    lb.type = wlx::runtime::parser::BlockType::Paragraph;
    const float top = grid_line_top(geo, line);
    lb.rect = D2D1::RectF(ctx.code_left, top,
                          ctx.code_left + ctx.max_code_width, top + geo.line_height);

    TextRun run;
    run.rect = lb.rect;
    run.is_code = true;
    run.color_ranges = build_color_ranges(spans_for_line, source_to_expanded, expanded);
    float h = geo.line_height;
    run.layout = create_line_layout(expanded, run.color_ranges, ctx, h);
    run.text = std::move(expanded);
    lb.text_runs.push_back(std::move(run));
    apply_line_decorations(lb, lb.text_runs[0].text, source_to_expanded, orig, ctx);
    return lb;
}

void slide_grid_window(LayoutDocument& doc, const GridGeometry& geo,
                       MaterializeCtx& ctx, const std::string& raw_utf8,
                       const std::vector<int>& line_byte_starts,
                       int first, int last, const ColorsForRange& colors_for) {
    if (last < first) { doc.blocks.clear(); doc.first_block_line = first; return; }
    const int old_first = doc.first_block_line;
    const int old_last = old_first + static_cast<int>(doc.blocks.size()) - 1;

    std::vector<LayoutBlock> next;
    next.reserve(static_cast<size_t>(last - first + 1));

    auto colors_into = [&](int lo_line, int hi_line,
                           std::vector<std::vector<PerLineSpan>>& out) {
        const int raw_size = static_cast<int>(raw_utf8.size());
        const uint32_t blo = static_cast<uint32_t>(line_byte_starts[lo_line]);
        const uint32_t bhi = (hi_line + 1 < static_cast<int>(line_byte_starts.size()))
                                 ? static_cast<uint32_t>(line_byte_starts[hi_line + 1])
                                 : static_cast<uint32_t>(raw_size);
        auto spans = colors_for(blo, bhi);
        out.assign(static_cast<size_t>(hi_line - lo_line + 1), {});
        distribute_spans_to_lines(raw_utf8, line_byte_starts, raw_size, spans,
                                  lo_line, hi_line, out);
    };

    // Walk the new window; reuse blocks that overlap the old one, batch-build
    // the contiguous entering ranges around them.
    int line = first;
    while (line <= last) {
        if (line >= old_first && line <= old_last && !doc.blocks.empty()) {
            next.push_back(std::move(doc.blocks[static_cast<size_t>(line - old_first)]));
            ++line;
            continue;
        }
        int run_end = line;
        while (run_end + 1 <= last && !(run_end + 1 >= old_first && run_end + 1 <= old_last))
            ++run_end;
        std::vector<std::vector<PerLineSpan>> line_spans;
        colors_into(line, run_end, line_spans);
        for (int l = line; l <= run_end; ++l)
            next.push_back(build_grid_line(l, geo, ctx, raw_utf8, line_byte_starts,
                                           line_spans[static_cast<size_t>(l - line)]));
        line = run_end + 1;
    }
    doc.blocks = std::move(next);          // leaving blocks destroyed here = eviction
    doc.first_block_line = first;
}

}  // namespace wlx::plugin_colorizer::layout
```

- [ ] **Step 5: Tests green** — `colorizer-tests.exe -tc="*grid window*"`, then full suite.

- [ ] **Step 6: Commit** — `git commit -m "feat(colorizer): implicit-grid skeleton + slide_grid_window (build/reuse/evict)"`

### Task 9: Host integration — grid windowing drives WM_PAINT

**Files:**
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp`

- [ ] **Step 1: ViewState + do_layout wiring**

Add fields to `ColorViewState`:

```cpp
    // Grid mode (no-wrap): per-document build context + geometry for
    // slide_grid_window. Null/zeroed in wrap mode (eager whole-file blocks).
    std::shared_ptr<wlx::plugin_colorizer::layout::MaterializeCtx> grid_ctx;
    wlx::plugin_colorizer::layout::GridGeometry grid_geo;
```

`layout_source` gains an out-param `std::shared_ptr<MaterializeCtx>* out_grid_ctx = nullptr` (it already heap-allocates `mctx` — in grid mode hand it out instead of capturing it in the deleted hook). `do_layout` (line 362) captures it and fills the geometry:

```cpp
    vs->line_byte_starts.clear();
    std::shared_ptr<MaterializeCtx> grid_ctx;
    auto layout = std::make_shared<LayoutDocument>(
        layout_source(dwrite_factory(), raw_utf8, colors, g_theme, vs->dark_mode,
                      viewport_width, cfg, /*timings=*/nullptr,
                      &vs->line_byte_starts, &grid_ctx));
    vs->grid_ctx = std::move(grid_ctx);
    vs->grid_geo = vs->grid_ctx
        ? GridGeometry{4.0f, vs->grid_ctx->line_height, layout->grid_line_count}
        : GridGeometry{};   // wrap mode: no ctx, line_count 0, is_grid() false
```

- [ ] **Step 2: `ensure_grid_window` replaces `colorize_viewport` in the paint path**

```cpp
// Pre-paint: slide the materialized window over the viewport. Colors come from
// the span table once the sweep settled, else from the live tree, else plain.
static void ensure_grid_window(ColorViewState* vs) {
    if (!vs || !vs->layout) return;
    if (!vs->layout->is_grid()) { colorize_viewport(vs); return; }   // wrap/eager fallback
    if (!vs->grid_ctx) return;
    const float viewport_h = vs->renderer ? vs->renderer->dip_height() : 100.0f;
    auto [first, last] = grid_window_lines(vs->grid_geo, vs->scroll_y,
                                           viewport_h, viewport_h);
    const auto raw_size = static_cast<uint32_t>(vs->cached_raw_utf8.size());
    ColorsForRange colors_for = [vs, raw_size](uint32_t lo, uint32_t hi)
        -> wlx::core::colorizer::ColorizeResult {
        if (vs->span_table.complete(raw_size)) return vs->span_table.slice(lo, hi);
        if (vs->tree) {
            WlxColorSpan* spans = nullptr;
            uint32_t count = 0;
            if (wlx_core_highlight_range(g_colorizer_handle, vs->tree.get(),
                                         vs->dark_mode ? 1 : 0, lo, hi,
                                         &spans, &count) == 0)
                return wlx_core::abi_spans_to_result(spans, count);
        }
        return {};   // unsupported language / sweep-aborted-and-tree-lost: plain
    };
    slide_grid_window(*vs->layout, vs->grid_geo, *vs->grid_ctx,
                      vs->cached_raw_utf8, vs->line_byte_starts,
                      first, last, colors_for);
}
```

In the WM_PAINT handler (line ~750) replace the `colorize_viewport(vs);` call with `ensure_grid_window(vs);`. The parse-adopt success path replaces its `colorize_viewport(v);` with `ensure_grid_window(v);` too (and keeps `begin_sweep(v);`).

- [ ] **Step 3: Resize + DPI paths**

`resize_widths_nowrap` (line 636): after the existing rect-stretch loop over the (now window-only) blocks, also refresh the build context so FUTURE window builds use the new width:

```cpp
    if (vs->grid_ctx) {
        const float code_right = new_vw - 8.0f;                 // right_margin
        vs->grid_ctx->max_code_width =
            std::max(1.0f, code_right - vs->grid_ctx->code_left);
    }
```

The `doc.blocks.empty()` guard already handles a pre-first-paint resize. WM_DPICHANGED keeps calling `relayout` → full `do_layout` rebuild (grid ctx/geo refreshed) — no change.

- [ ] **Step 4: Build + run; commit**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe && ./build/Release/tests.exe`
Expected: green. (Pixel parity is asserted in Task 13 when the tool routes through the same path.)

```bash
git commit -am "feat(colorizer): grid windowing drives paint — window slide replaces whole-file blocks"
```

### Task 10: Line-index awareness — renderer, hit-test, interaction

**Files:**
- Modify: `src/runtime/render/render_engine.cpp` (paint loop ~246-297; selection ~315-372; search ~374-434)
- Modify: `src/runtime/host/hit_test.cpp` (~13-64)
- Modify: `src/runtime/interaction/interaction_engine.cpp` (~12-41)
- Test: extend `tests/plugin_colorizer/layout/test_grid_window.cpp` + `tests/runtime/interaction/test_interaction_engine.cpp`

- [ ] **Step 1: Renderer — translate vector index → line index at the comparison points**

In `RenderEngine::paint`'s block loop, compute `const int line_base = layout.first_block_line;` once, and pass `line_base + block_idx` (instead of `block_idx`) into `paint_selection_highlight(...)` and `paint_search_highlights(...)`. Those two helpers compare the incoming index against `TextPosition::block_index` / `SearchMatch::block_index` — which are line indices in grid mode and unchanged vector indices in classic mode (`line_base == 0`), so md behavior is identical by construction. The `materialize_block` hook call keeps the raw vector index (its only md consumer indexes recipes by vector position; grid mode has no hook).

- [ ] **Step 2: Hit-test returns line indices**

`hit_test_position` (hit_test.cpp): both the exact loop and the nearest-snap fallback return `TextPosition{layout.first_block_line + i, offset}` instead of `{i, offset}`.
`InteractionEngine::hit_test`: `HitResult.block_index = layout_.first_block_line + bi;`.
Audit the colorizer host's HitResult consumers (link open/copy at the context-menu and click handlers): where they re-access `vs->layout->blocks[hit.block_index]`, convert with `hit.block_index - vs->layout->first_block_line` and bounds-check against `blocks.size()` (a scroll between hover and click can slide the window — out-of-window means "span no longer visible", treat as no-hit). The md plugin consumers are untouched (`first_block_line == 0`).

- [ ] **Step 3: Triple-click + double-click word select in the colorizer host**

These read `vs->layout->blocks[pos.block_index]` — convert to window slot the same way; if the line is outside the window (can't normally happen for a click, which is inside the viewport ⊂ window), no-op.

- [ ] **Step 4: Tests**

- Grid: build a 100-line grid window over [40,60], `hit_test_position` at y of line 45 → `block_index == 45` (not 5).
- Classic: existing `test_interaction_engine.cpp` + `test_text_selection.cpp` suites green unchanged (md regression gate — `first_block_line` defaults to 0).

- [ ] **Step 5: Run both suites + visual suite; commit**

```bash
git commit -am "feat(layout): block indices become source-line indices in grid mode (renderer/hit-test/interaction)"
```

### Task 11: Selection text + select-all from raw bytes

**Files:**
- Modify: `src/plugin_colorizer/layout/grid_window.h` / `.cpp`
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp` (lc_copy / lc_selectall / Ctrl+C sites)
- Test: extend `tests/plugin_colorizer/layout/test_grid_window.cpp`

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("grid selection text: decodes+expands from raw, multi-line, tabs, CRLF, UTF-8") {
    const std::string raw = "alpha\tbeta\r\nsecond line\nd\xC3\xA9j\xC3\xA0 vu\n";
    std::vector<int> starts = {0, 12, 24};   // computed as layout_source would
    // char offsets are into EXPANDED text (tab_width 4: "alpha   beta")
    auto s = extract_selected_text_grid(raw, starts, 4,
                                        {0, 6}, {2, 4});   // mid line 0 .. mid line 2
    CHECK(s == L"  beta\nsecond line\ndéjà");
    // single-line slice
    CHECK(extract_selected_text_grid(raw, starts, 4, {1, 0}, {1, 6}) == L"second");
    // whole-doc == select_all bounds
    auto all = extract_selected_text_grid(raw, starts, 4, {0, 0}, {2, 999});
    CHECK(all == L"alpha   beta\nsecond line\ndéjà vu");
}
```

- [ ] **Step 2: Implement**

```cpp
// grid_window.h
std::wstring extract_selected_text_grid(const std::string& raw_utf8,
                                        const std::vector<int>& line_byte_starts,
                                        int tab_width,
                                        wlx::runtime::layout::TextPosition lo,
                                        wlx::runtime::layout::TextPosition hi);

// grid_window.cpp — mirrors runtime extract_selected_text's clamping, but
// sources each line from raw bytes (decode + expand) instead of run.text.
std::wstring extract_selected_text_grid(const std::string& raw_utf8,
                                        const std::vector<int>& line_byte_starts,
                                        int tab_width, TextPosition lo, TextPosition hi) {
    std::wstring out;
    const int line_count = static_cast<int>(line_byte_starts.size());
    const int raw_size = static_cast<int>(raw_utf8.size());
    for (int line = std::max(0, lo.block_index);
         line <= hi.block_index && line < line_count; ++line) {
        const int bs = line_byte_starts[line];
        const int be = (line + 1 < line_count) ? line_byte_starts[line + 1] - 1 : raw_size;
        std::wstring expanded =
            expand_tabs(decode_line(raw_utf8, bs, std::max(bs, be)), tab_width, nullptr);
        int from = (line == lo.block_index) ? lo.char_offset : 0;
        int to = (line == hi.block_index) ? hi.char_offset
                                          : static_cast<int>(expanded.size());
        from = std::clamp(from, 0, static_cast<int>(expanded.size()));
        to = std::clamp(to, from, static_cast<int>(expanded.size()));
        if (line != std::max(0, lo.block_index)) out.push_back(L'\n');
        out.append(expanded, static_cast<size_t>(from), static_cast<size_t>(to - from));
    }
    return out;
}
```

- [ ] **Step 3: Host switches by mode**

Where the colorizer host copies the selection (the `lc_copy`/Ctrl+C path currently calling the shared `wlx::runtime::host::copy_selection`), branch:

```cpp
static bool copy_selection_color(ColorViewState* vs, HWND hwnd) {
    if (!vs->layout || !vs->layout->is_grid())
        return wlx::runtime::host::copy_selection(*vs, hwnd);   // wrap/eager path
    auto [lo, hi] = wlx::runtime::host::ordered_selection(*vs); // existing normalize helper
    std::wstring text = extract_selected_text_grid(
        vs->cached_raw_utf8, vs->line_byte_starts, g_display_cfg.tab_width, lo, hi);
    return wlx::runtime::host::put_clipboard_text(hwnd, text);  // existing clipboard helper
}
```

(Adapt the two helper names to the actual ones in `view_actions.h`/`clipboard.h` — they exist; the Selectable template body shows them.) `select_all` in grid mode sets `sel_anchor = {0, 0}` and `sel_active = {grid_line_count - 1, <expanded length of last line>}` (one decode of the last line).

- [ ] **Step 4: Tests green, suites green, commit**

```bash
git commit -am "feat(colorizer): selection/copy/select-all decode from raw bytes in grid mode"
```

### Task 12: Search over raw bytes + grid-aware scroll-to-match

**Files:**
- Modify: `src/runtime/search/search_index.h` / `.cpp` (new `build_lines` overload)
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp` (index build site + scroll_to_match)
- Test: `tests/runtime/search/test_search_engine.cpp` (extend), `tests/plugin_colorizer/layout/test_grid_window.cpp` (offsets-with-tabs case)

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("search index: build_lines matches block-based build") {
    // Construct a LayoutDocument with 3 blocks of known run.text, build()
    // normally; then build_lines(3, callback returning the same 3 strings).
    // find_all over both must return identical SearchMatch lists, and
    // block_index in the line-built index must equal the line number.
}
```

- [ ] **Step 2: Implement `build_lines`**

```cpp
// search_index.h
    void build_lines(int line_count,
                     const std::function<std::wstring(int)>& line_text);

// search_index.cpp — same flattening contract as build(): lines joined by
// '\n', block_starts_[i] = flat offset of line i. Grid mode feeds expanded
// line text so char offsets stay consistent with hit-testing.
void SearchIndex::build_lines(int line_count,
                              const std::function<std::wstring(int)>& line_text) {
    flat_.clear();
    flat_lower_.clear();
    block_starts_.clear();
    block_starts_.reserve(static_cast<size_t>(std::max(0, line_count)));
    for (int i = 0; i < line_count; ++i) {
        if (i > 0) flat_.push_back(L'\n');
        block_starts_.push_back(static_cast<int>(flat_.size()));
        flat_ += line_text(i);
    }
    flat_lower_ = to_lower(flat_);
}
```

(`<functional>` include added to the header.)

- [ ] **Step 3: Host index build + scroll-to-match**

At the colorizer's `index_dirty` rebuild site:

```cpp
    if (vs->index_dirty) {
        if (vs->layout && vs->layout->is_grid()) {
            const auto& raw = vs->cached_raw_utf8;
            const auto& starts = vs->line_byte_starts;
            const int tab = g_display_cfg.tab_width;
            vs->search_index.build_lines(
                vs->layout->grid_line_count, [&raw, &starts, tab](int line) {
                    const int raw_size = static_cast<int>(raw.size());
                    const int bs = starts[static_cast<size_t>(line)];
                    const int be = (line + 1 < static_cast<int>(starts.size()))
                                       ? starts[static_cast<size_t>(line) + 1] - 1
                                       : raw_size;
                    return expand_tabs(decode_line(raw, bs, std::max(bs, be)), tab,
                                       nullptr);
                });
        } else {
            vs->search_index.build(*vs->layout);
        }
        vs->index_dirty = false;
    }
```

`scroll_to_match` wrapper (line 677): in grid mode center arithmetically instead of hit-testing a possibly-unmaterialized block:

```cpp
static void scroll_to_match(ColorViewState* vs, const SearchMatch& m) {
    if (vs->layout && vs->layout->is_grid()) {
        if (m.block_index < 0 || m.block_index >= vs->layout->grid_line_count) return;
        const float line_top = grid_line_top(vs->grid_geo, m.block_index);
        const float viewport_h = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        vs->scroll_y = std::clamp(line_top - viewport_h * 0.4f, 0.0f, vs->max_scroll_y);
        InvalidateRect(vs->hwnd, nullptr, FALSE);    // window slides + highlight paints
        return;
    }
    wlx::runtime::host::scroll_to_match(*vs, m);
}
```

(The renderer paints the actual match highlight from `SearchMatch` once the window covers the line — Task 10 made that line-aware.)

- [ ] **Step 4: Suites + an interactive check via TC's F7 lister search on a big file. Commit**

```bash
git commit -am "feat(search): line-callback index build + arithmetic scroll-to-match for grid mode"
```

### Task 13: Screenshot tool routes cached-tree through the grid (pixel parity gate)

**Files:**
- Modify: `src/tools/screenshot/colorizer_pipeline.cpp` (cached-tree route)
- Modify: `src/tools/screenshot/CMakeLists.txt` (add `grid_window.cpp` if not yet)

- [ ] **Step 1: Re-plumb the route**

Replace the route's "skeleton + viewport_byte_range + highlight_range + apply_spans_to_range" sequence (lines ~414-467) with the host's exact flow: grid `layout_source` (returns grid ctx) → `grid_window_lines` for the bench viewport at `scroll_y_ct` → `slide_grid_window` with a `colors_for` that uses the tree → first paint → sweep (Task 2's loop, unchanged) → `tree.reset()` → for `--scroll-screens`: per step `scroll_y += viewport_h; slide_grid_window(..., colors_for_table); paint;`. The sweep and sampling code from Task 2 survives verbatim; only the layout/window calls change.

- [ ] **Step 2: The actual gate — full visual suite**

Run: `./scripts/visual-test.sh`
Expected: token snapshots byte-identical; colorizer pixel smokes ≥ thresholds (these render through `--full`/eager — unchanged — AND any `--cached-tree` flagged smokes through the grid). If any smoke lacks a `--cached-tree` variant, add one: copy an existing smoke's `.flags` file with `--cached-tree` appended (e.g. `test_data/colorizer_smokes/sample2.flags`) and generate its golden from the PRE-grid build first (`git stash` the route change → generate → `git stash pop`) so the comparison is old-path vs new-path.

- [ ] **Step 3: Bench sanity**

Run: `uv run scripts/bench.py`
Expected: sqlite cached delta ≈ **≤ 70 MB**; json.hpp ≈ **≤ 35 MB**; post-scroll row within ~10 MB of the post-open row (window slide = eviction); open ms at or below baseline (we no longer decode/build 250k lines up front).

- [ ] **Step 4: Commit** — `git commit -am "feat(bench): cached-tree route through the implicit grid — parity + memory targets hit"`

### Task 14: M2 cleanup — delete the dead machinery

**Files:**
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp`, `src/plugin_colorizer/layout/colorizer_layout.{h,cpp}`, `tests/plugin_colorizer/layout/test_colorizer_layout.cpp`

- [ ] **Step 1: Remove now-unreachable code**

With every no-wrap paint going through `ensure_grid_window`:
- `colorize_viewport` + ViewState `colored_lo/colored_hi` + `colored_interval_update` — delete (wrap mode never had a tree; its whole-doc colors come from `cached_colors` at layout time).
- `viewport_byte_range` + `apply_spans_to_range` — delete from `colorizer_layout.{h,cpp}` after confirming the tool no longer calls them (Task 13 removed the last call sites). `distribute_spans_to_lines`/`build_color_ranges` stay (grid uses them).
- `MaterializeCtx::orig_lines` + the lazy `materialize_block` hook lambda remnants — delete (grid decodes from raw).
- `ColorViewState::cached_colors` — KEEP (wrap/unsupported whole-doc fallback still stores into it).

- [ ] **Step 2: Rework the orphaned tests**

In `test_colorizer_layout.cpp`, the ~17 cases covering `viewport_byte_range` / `colored_interval_update` / `apply_spans_to_range` (byte-window off-by-ones, overscan growth, interval skip/union/reset): delete the interval/apply cases; PORT the byte-window boundary cases to `grid_window_lines` + `colors_into` equivalents in `test_grid_window.cpp` (last-line hi == raw_size not raw_size+1; huge viewport == full file; empty file; window past EOF) — the off-by-one zoo must survive in the new API.

- [ ] **Step 3: Full verification + commit**

Run: build + both suites + `./scripts/visual-test.sh` + `uv run scripts/bench.py`.

```bash
git commit -am "refactor(colorizer): delete colored-interval/apply-spans machinery superseded by grid windowing"
```

---

## Milestone 3 — Markdown: instrument, then evict + cap caches

### Task 15: Per-phase memory instrumentation in the md bench (Stage 1 gate)

**Files:**
- Modify: `src/tools/screenshot/markdown_pipeline.cpp`

- [ ] **Step 1: Sample per phase**

`sample_working_set().current` is already included (line 95 baseline). Capture additional samples right after each phase timestamp (`t_read`, `t_parse`, `t_layout`, `t_target`, `t_materialize`, `t_paint`) into `size_t ws_read, ws_parse, ...`, then print in the bench block (after the existing `document AST` / `layout data` lines, labels chosen to NOT match `RX_DELTA`'s `process delta` anchor):

```cpp
        auto phase_mb = [mem_before](size_t ws) {
            return (static_cast<double>(ws) - static_cast<double>(mem_before))
                   / (1024.0 * 1024.0);
        };
        std::fprintf(stderr, "  ws after read         %+8.1f MB\n", phase_mb(ws_read));
        std::fprintf(stderr, "  ws after parse        %+8.1f MB\n", phase_mb(ws_parse));
        std::fprintf(stderr, "  ws after layout       %+8.1f MB\n", phase_mb(ws_layout));
        std::fprintf(stderr, "  ws after target       %+8.1f MB\n", phase_mb(ws_target));
        std::fprintf(stderr, "  ws after materialize  %+8.1f MB\n", phase_mb(ws_materialize));
        std::fprintf(stderr, "  ws after paint        %+8.1f MB\n", phase_mb(ws_paint));
```

- [ ] **Step 2: Run on big.md and RECORD THE BUCKETS — this is the Stage-2 gate**

Run: `./build/Release/screenshot_tool.exe test_data/bench/big.md --bench --lazy --dark --width 1000 --height 1200`
Expected: the ~123 MB delta decomposes across the six rows. **Decision point per the spec:** if the bulk lands in `after target`/`after paint` (D2D/DWrite internals — render target, font/glyph caches), it is process overhead, NOT per-file retention → fix the README's attribution in Task 18 and do NOT add AST work. If `after parse` or `after layout` is much larger than the printed `document AST`/`layout data` estimates, STOP and report to the user before inventing fixes — that's a real finding the spec defers decisions on. Tasks 16–17 proceed regardless (known-real independent of the buckets).

- [ ] **Step 3: Commit** — `git commit -m "feat(bench): per-phase working-set sampling in the md pipeline (Stage 1 instrumentation)"`

### Task 16: Markdown layout eviction (drop layouts, keep geometry)

**Files:**
- Modify: `src/runtime/layout/md_materialize.h` / `.cpp`
- Modify: `src/plugin_md/window/host_adapter.cpp` (retain the ctx, pass recipes)
- Test: `tests/runtime/layout/test_lazy_layout.cpp` (extend)

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("md eviction: far-off materialized blocks drop layouts but keep exact geometry") {
    // Lazy-layout a doc tall enough for 3+ screens. materialize_viewport at
    // scroll 0; record blocks[i].rect for every block and note which have
    // run.layout. Scroll far (e.g. total_height - viewport) and
    // materialize_viewport again (with recipes passed). Assert:
    //  - blocks near scroll 0 now have run.layout == nullptr, empty
    //    color_ranges/code_bg_rects/spans, but UNCHANGED rect and run.text;
    //  - line_tops identical to before eviction (no index rebuild from evict);
    //  - blocks WITHOUT a recipe (kind == None: lists/quotes/tables stay eager)
    //    keep their layouts untouched;
    //  - scrolling back re-materializes evicted blocks with delta == 0
    //    (recipe rebuild reproduces the measured height exactly), and
    //    materialize_viewport returns the correct `changed` flag.
}
```

- [ ] **Step 2: Implement**

`md_materialize.h` — extend the signature (default keeps every current caller compiling):

```cpp
// When `recipes` is non-null, blocks materialized earlier but now further than
// kEvictScreens screens outside the viewport drop their IDWriteTextLayout +
// per-paint decoration state (color_ranges, code_bg_rects, spans, ws/trailing)
// and re-materialize from their recipe on re-entry. Measured rects and
// run.text are KEPT, so geometry/line_tops/search stay exact and eviction
// never triggers a reflow or an index rebuild.
inline constexpr float kEvictScreens = 2.0f;
bool materialize_viewport(LayoutDocument& doc, float scroll_y, float viewport_h,
                          const std::vector<BlockRecipe>* recipes = nullptr);
```

`md_materialize.cpp` — at the end of `materialize_viewport` (after the delta tail-pass, before the `changed` index rebuild — eviction must NOT set `changed`):

```cpp
    if (recipes) {
        const float keep_top = vp_top - viewport_h * kEvictScreens;
        const float keep_bottom = vp_bottom + viewport_h * kEvictScreens;
        for (size_t bi = 0; bi < doc.blocks.size(); ++bi) {
            auto& b = doc.blocks[bi];
            if (b.rect.bottom >= keep_top && b.rect.top <= keep_bottom) continue;
            if (b.text_runs.empty() || !b.text_runs[0].layout) continue;
            if (bi >= recipes->size() ||
                (*recipes)[bi].kind == BlockRecipe::None)
                continue;   // eager block (list/quote/table): no recipe, never evict
            auto& run = b.text_runs[0];
            run.layout.Reset();
            run.color_ranges.clear();
            run.color_ranges.shrink_to_fit();
            run.code_bg_rects.clear();
            run.code_bg_rects.shrink_to_fit();
            b.spans.clear();
            b.spans.shrink_to_fit();
            b.ws_markers.clear();
            b.has_trailing_ws = false;
        }
    }
```

- [ ] **Step 3: Host wiring**

`src/plugin_md/window/host_adapter.cpp`: ViewState gains `std::shared_ptr<wlx::runtime::layout::MdMaterializeCtx> md_ctx;`. In `do_layout`'s lazy branch (line ~265, where `take_md_ctx()` already runs): `vs->md_ctx = ctx;` (the same shared_ptr the closure captured — recipes are shared state, never copied). The WM_PAINT call site (line 526) becomes:

```cpp
    materialize_viewport(*vs->layout, vs->scroll_y, vs->renderer->dip_height(),
                         vs->md_ctx ? &vs->md_ctx->recipes : nullptr);
```

(Adapt to the actual wrapper if the host calls through a local helper — keep the recipes argument threading.) Cache-hit layouts (`lookup_layout`) reuse a layout whose ctx lives in its closure; the host's `md_ctx` must be restored too — store the ctx alongside in the ViewState before `store_layout` and on cache hit re-derive: simplest correct rule: on cache hit, leave `vs->md_ctx = nullptr` (no eviction for cache-served layouts — they were already materialized; correctness unaffected, memory parity with today). Document this in a comment at the lookup site.

- [ ] **Step 4: Tests + suites green; commit**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe && ./scripts/visual-test.sh`

```bash
git commit -am "feat(md): evict off-window materialized layouts — geometry kept exact, recipes rebuild on re-entry"
```

### Task 17: CacheService byte budget

**Files:**
- Create: `src/runtime/cache/memory_estimate.h`
- Modify: `src/runtime/cache/cache_service.h` (+ `.cpp` if split there)
- Modify: `src/tools/screenshot/markdown_pipeline.cpp` (replace its local estimators with the shared header)
- Test: `tests/runtime/cache/test_cache_service.cpp` (extend)

- [ ] **Step 1: Failing tests**

```cpp
// Helper: a Document whose estimate_document_memory() is ~`mb` megabytes —
// one block with one InlineNode holding mb*1024*1024/2 wchar_t of text.
static std::shared_ptr<parser::Document> doc_of_mb(size_t mb);

TEST_CASE("cache byte budget: eviction frees LRU entries until under budget") {
    CacheService c;
    // kMaxBytesPerCache is 64 MB: five ~20 MB docs can never all be resident.
    for (int i = 0; i < 5; ++i)
        c.store_parse(key_for(i), doc_of_mb(20));
    CHECK(c.parse_total_bytes() <= CacheService::kMaxBytesPerCache);
    CHECK(c.lookup_parse(key_for(4)) != nullptr);   // just-stored always survives
    CHECK(c.lookup_parse(key_for(0)) == nullptr);   // LRU tail evicted
}

TEST_CASE("cache byte budget: an oversize single entry still survives its own store") {
    CacheService c;
    c.store_parse(key_for(0), doc_of_mb(100));      // alone over budget
    CHECK(c.lookup_parse(key_for(0)) != nullptr);   // map.size() > 1 guard
}

TEST_CASE("cache byte budget: overwrite updates the tracked total") {
    CacheService c;
    c.store_parse(key_for(0), doc_of_mb(20));
    const size_t before = c.parse_total_bytes();
    c.store_parse(key_for(0), doc_of_mb(1));        // same key, smaller doc
    CHECK(c.parse_total_bytes() < before);
}
```

(`key_for(i)` = a `ParseCacheKey` varying `path`; `parse_total_bytes()` is a new
const accessor added for tests alongside the tracked total. The entry-count cap
cases already exist in this file and must stay green.)
```

- [ ] **Step 2: Implement**

`memory_estimate.h`: move `estimate_document_memory(const parser::Document&)` and `estimate_layout_memory(const layout::LayoutDocument&)` verbatim from `markdown_pipeline.cpp:63-88` into `namespace wlx::runtime::cache` as inline functions; the tool includes this header and deletes its copies.

`cache_service.h`: per-entry tracked bytes + budget-evict on store:

```cpp
    static constexpr size_t kMaxEntries = 16;
    static constexpr size_t kMaxBytesPerCache = 64 * 1024 * 1024;  // ~one big doc + change
```

Each LRU map entry becomes `{value, size_t bytes}`; `store_*` computes bytes via the estimator, updates a running `total_bytes_`, then evicts from the LRU back while `(map.size() > kMaxEntries) || (total_bytes_ > kMaxBytesPerCache && map.size() > 1)` — the `> 1` guarantees the just-stored entry survives. Lookup/clear maintain `total_bytes_`.

- [ ] **Step 3: Tests + suites green; commit**

```bash
git commit -am "feat(cache): byte-budget eviction for parse+layout caches (shared memory estimators)"
```

---

## Milestone 4 — Re-baseline + soak

### Task 18: Final verification, README/CLAUDE.md updates, manual TC soak

- [ ] **Step 1: Full automated verification**

```bash
cmake --build --preset conan-release
./build/Release/tests.exe && ./build/Release/colorizer-tests.exe
./scripts/visual-test.sh
uv run scripts/bench.py
```
Acceptance (spec targets, median of 5): sqlite cached held **≤ 70 MB**, json.hpp **≤ 35 MB**, post-scroll within **~10 MB** of post-open, every open-ms within **+10%** of the README baseline, peak no worse. If a target misses, STOP — diagnose with the Task 15 instrumentation / tool sweep rows before touching the baseline.

- [ ] **Step 2: Rewrite the stale README/CLAUDE.md prose + update the baseline**

- README "Performance" intro (line 77): the sentence "*Memory held is mostly the parsed syntax tree — kept while the file is open…*" is now false — replace with the span-table reality ("parse once → background sweep extracts all colors into a compact table → syntax tree freed seconds after open; scrolling re-colors from the table").
- Set the md row's target/attribution per Task 15's findings.
- `uv run scripts/bench.py --update` to rewrite the marker block (includes the new post-scroll row).
- CLAUDE.md: update the "Viewport-scoped highlighting (colorizer)" and "Lazy markdown layout" sections to mention the sweep/SpanTable, grid windowing, md eviction, and cache byte budget (a few sentences each, same density as the existing text).

- [ ] **Step 3: Manual TC soak (spec checklist — not unit-testable, message-pump behavior)**

In a real Total Commander session, on sqlite3.c + json.hpp + a normal file:
1. Rapid file tabbing mid-sweep (open → next file within 1 s, repeatedly).
2. Dark-mode flip mid-sweep and post-settle (colors pop in async, no crash).
3. Close mid-sweep; reopen immediately (generation/token paths).
4. Scroll hard DURING the sweep on sqlite (mutex interleaving — no multi-second stalls; chunks are ~25 ms).
5. Quick-view panel (Ctrl+Q) reuse across files.
6. Force-language mid-sweep (context menu → language).
7. F7 lister search on sqlite post-settle (grid search index); Ctrl+G goto; Ctrl+A/Ctrl+C of a multi-screen selection.
8. Word-wrap toggle on a big file (eager fallback path — slow but correct, memory as before M2 for that mode).
9. Task Manager working set: after opening sqlite and waiting ~3 s, confirm the drop to the new floor; scroll through the file end-to-end, confirm memory stays ~flat.

- [ ] **Step 4: Final commit**

```bash
git add README.md CLAUDE.md
git commit -m "docs: memory optimization shipped — new bench baseline, sweep/grid architecture notes"
```

---

## Risk register (for the executor)

- **Sweep worker vs tree lifetime:** the tree is a `shared_ptr<WlxTree>` shared with at most one sweep worker; ALL frees route through `wlx_core_free_tree` via the deleter, possibly on the worker thread (takes the core mutex there — never the loader lock). Never convert back to a raw/unique owner.
- **Index semantics flip (M2):** after Task 10, `TextPosition::block_index` / `SearchMatch::block_index` / `HitResult::block_index` are SOURCE LINE indices for grid documents. Any new consumer must map via `first_block_line`. md documents keep `first_block_line == 0`, so md code paths are bit-identical.
- **Window invariant:** everything the user can click/see is inside the window by construction (window ⊇ viewport ± 1 screen). Code that reaches blocks for NON-visible lines (search extraction, select-all, goto) must use the raw-byte paths, never `doc.blocks`.
- **`run.text` is EXPANDED text** (tabs → spaces) everywhere offsets are exchanged (selection, search, hit-test). The raw-byte extractors must expand before applying char offsets — Tasks 11/12 do; keep it that way.
- **Eager/wrap path is the memory-unoptimized fallback by design** (spec non-goal). Don't "fix" it opportunistically.
- **Bench comparability:** intermediate milestones change what `process delta` *means* (settle point). Don't run `--update` until Task 18.
