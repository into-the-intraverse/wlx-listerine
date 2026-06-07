# Lever 2 — Async Non-Blocking `ListLoadW` Implementation Plan (DEPENDENT FOLLOW-ON)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.
>
> **⚠️ DEPENDENCY GATE:** Do **not** start this plan until **Lever 1 ([lazy markdown layout](2026-06-07-lever1-lazy-markdown-layout.md)) is merged.** Lever 2 moves work onto a worker thread; it relies on Lever 1 having already moved all `CreateTextLayout` (the heavy, COM-touching work) to UI-thread *materialization*, so the worker only ever does pure parsing. Several task signatures below reference Lever 1's `MdMaterializeCtx`/`materialize_viewport` and Lever 3's `wlx_core_parse`/`WlxTree`. **Re-read this plan and finalize its finest steps after Lever 1 (and ideally Lever 3) have landed**, when those exact types exist.
>
> **⚠️ MEASURE-FIRST GATE:** After Levers 1 & 3, the *only* remaining synchronous open cost is the irreducible whole-file parse (tree-sitter ~43 ms on a 12k file; md parse ~10 ms). **Bench a representative large file first.** Lever 2's payoff scales with file size (a 5 MB source could be 200 ms+ of parse); for typical files Levers 1+3 may already make open feel instant, in which case Lever 2's COM/threading risk is not worth it. Proceed only if the measured parse latency justifies it.

**Goal:** Make `ListLoadW` (and `ListLoadNextW`) return without blocking on the whole-file parse, so even a multi-MB file opens instantly; the parsed document streams in and the view repaints when ready.

**Architecture:** Keep **all** Direct2D/DirectWrite/render-target work on the UI thread (Lever 1 already put `CreateTextLayout` in UI-thread materialization; Lever 3 put highlight on the UI thread against a cached tree). `ListLoadW` creates the window + renderer immediately and paints an empty themed frame (instant), then spawns a worker that does only the **pure parse** (md4c → `Document`, or tree-sitter → `WlxTree` for the colorizer). On completion the worker `PostMessage`s the result (a heap-owned, plain-data payload + a generation tag) to the UI thread, which adopts it, runs the cheap Lever-1 estimate pass + first-viewport materialization, updates the scrollbar, and invalidates. Stale results (from fast file-tabbing) are dropped by generation tag. The worker **never dereferences `ViewState`** after spawn — it owns its inputs (a copied path / source) and its output until the `PostMessage` hands ownership over.

**Tech Stack:** C++20 `std::jthread`/`std::stop_token`, Win32 `PostMessage`/registered `WM_APP_*`, the existing `std::jthread` prewarm precedent in `ListLoadW`, `ViewState`/`g_views`.

---

## Invariants (memory safety & threading — priority #1; do not violate)

1. **UI-thread-only COM.** The worker thread must touch **no** `ID2D1*` (render target) and **no** `ViewState` member that holds COM render resources. DirectWrite factory/format/layout are documented thread-safe, but to minimize risk the worker does **only** the parse (md4c `Document`, pure data) or `wlx_core_parse` (which serializes through the core mutex and returns an opaque handle). All `IDWriteTextLayout` creation stays in UI-thread `materialize_viewport` (Lever 1) / `wlx_core_highlight_range` (Lever 3).
2. **Worker never dereferences `ViewState`.** The worker captures only: a `std::wstring` copy of the path (or the already-read source buffer), the `WlxCore*` handle, the target `HWND`, and a `uint64_t generation`. Its result is a heap-owned payload. It communicates **only** via `PostMessage(hwnd, WM_APP_DOC_READY, generation, payload*)`. It must not read or write `*vs`.
3. **Generation guard.** `ViewState` holds `uint64_t load_generation`. Each new `load_document` increments it and tags the worker. The `WM_APP_DOC_READY` handler: look up `g_views[hwnd]`; if the view is gone **or** `payload->generation != vs->load_generation`, `delete payload` and return. Only a current, live result is adopted. This makes fast tabbing and mid-load close safe.
4. **No UI-thread join stalls.** A new load must not block the UI thread joining the previous worker (`std::jthread`'s destructor joins). Use a process-global **retired-worker reaper** (a mutex-guarded `std::vector<std::jthread>` that detaching-but-tracked workers move into on completion, drained at `DLL_PROCESS_DETACH` per the repo's detach rules) — OR `request_stop()` the old worker and let generation-guarding drop its result while it finishes in the background. Do **not** call `.join()` on the UI thread in `ListLoadW`/`ListLoadNextW`.
5. **DLL unload safety.** Outstanding workers at `DLL_PROCESS_DETACH` follow the repo's existing detach rule (host_adapter.cpp:855-879): do not block/join under the loader lock; leak/abandon tracked threads (OS reclaims on process exit). `wlx_core_*` calls from a worker mid-detach are unsafe — gate worker bodies on a global "shutting down" flag set first thing in detach.
6. **Payload is plain data.** The `WM_APP_DOC_READY` payload owns a `std::shared_ptr<Document>` (md) or a `wlx_core::TreePtr` (colorizer) — both safe to move across threads. No raw pointers into per-view state.

---

## File Structure

| File | Create/Modify | Responsibility |
|------|---------------|----------------|
| `src/runtime/host/async_loader.h` / `.cpp` | **Create** | Reusable async-load scaffolding: `WM_APP_DOC_READY` constant, the retired-worker reaper, a `spawn_parse_worker(hwnd, gen, fn)` helper templated on the payload type. Lives in `runtime/host` so both plugins share it (mirrors the existing `runtime/host/*` shared helpers). |
| `src/plugin_md/window/host_adapter.cpp` | Modify | `ViewState.load_generation`; `load_document` splits into UI-cheap prelude + worker parse + `WM_APP_DOC_READY` adopt; `ListLoadW`/`ListLoadNextW` return after spawning. |
| `src/plugin_colorizer/window/colorizer_host_adapter.cpp` | Modify | Same split; worker runs `wlx_core_parse` (Lever 3), adopt swaps `vs->tree` + triggers viewport highlight. |
| `tests/runtime/host/test_async_loader.cpp` | **Create** | Unit-test the generation guard + reaper logic (the parts that don't need a live HWND). |

> **Before Task 1:** decide whether to lift the async scaffolding to `runtime/host` (recommended — both plugins need it) or inline per-plugin. Read `src/runtime/host/factories.{h,cpp}` and a couple of existing `runtime/host/*` headers to match the established shared-helper style and the `HostIntegration`/`HostView` concepts.

---

## Task 1: Async-load scaffolding (testable in isolation)

**Files:** Create `src/runtime/host/async_loader.h` / `.cpp`, `tests/runtime/host/test_async_loader.cpp`.

- [ ] **Step 1: Write the failing test (generation guard is pure logic)**

```cpp
#include <doctest/doctest.h>
#include "runtime/host/async_loader.h"

using namespace wlx::runtime::host;

TEST_CASE("generation guard accepts only the current generation") {
    AsyncLoadState s;
    uint64_t g1 = s.begin_load();   // ++generation, returns it
    uint64_t g2 = s.begin_load();
    CHECK(g1 != g2);
    CHECK(s.is_current(g2));
    CHECK_FALSE(s.is_current(g1));  // superseded
}
```

- [ ] **Step 2: Run to verify it fails** — `cmake --build --preset conan-release` → `async_loader.h: No such file`.

- [ ] **Step 3: Implement** the minimal `AsyncLoadState { uint64_t gen = 0; uint64_t begin_load(){return ++gen;} bool is_current(uint64_t g) const {return g==gen;} }`, the registered window message `inline constexpr UINT WM_APP_DOC_READY = WM_APP + 7;` (verify no collision with SearchHud/other WM_APP_* — `Grep "WM_APP"`), and the retired-worker reaper (`add_retired_worker(std::jthread&&)` + `reap_all()` under a global mutex). Keep the spawn helper a thin template:

```cpp
// Spawn a worker that produces Payload via make(stop_token) and PostMessages it.
template <class Payload, class MakeFn>
void spawn_parse_worker(HWND hwnd, uint64_t gen, MakeFn make);  // impl in header (template)
```

- [ ] **Step 4: Run tests** — `./build/Release/tests.exe -tc="generation guard*"` → PASS.
- [ ] **Step 5: Commit** — `feat(host): async-load scaffolding (generation guard + worker reaper)`.

## Task 2: Markdown — parse on a worker thread

**Files:** Modify `src/plugin_md/window/host_adapter.cpp`. Test: manual + existing suite.

- [ ] Add `AsyncLoadState async_load;` (or a `uint64_t load_generation`) to `ViewState`.
- [ ] Split `load_document` (host_adapter.cpp:234-280): keep the UI-cheap prelude (reset scroll/selection/search, `g_file_service.read` — fast — and the parse-cache lookup). On a **cache miss**, instead of parsing inline, spawn a worker that does `MarkdownParser().parse(...)` and posts a `shared_ptr<Document>` payload tagged with `vs->async_load`'s new generation. On a **cache hit**, adopt synchronously (no worker — it's already parsed).
- [ ] Add the `WM_APP_DOC_READY` case to `ViewWndProc`: guard by `g_views` + generation (Invariant 3); on accept, `g_cache.store_parse`, set `vs->document`, call `do_layout(vs)` (Lever 1: cheap estimate pass), `InvalidateRect`. On reject, `delete payload`.
- [ ] `ListLoadW`/`ListLoadNextW`: after the prelude they already `InvalidateRect` — the first paint now shows an empty themed frame (renderer exists, `vs->layout` is null → `WM_PAINT`'s `if (... && vs->layout)` guard skips painting content; ensure the background still clears, which `RenderEngine::paint` does on `BeginDraw`/`Clear` — but paint isn't called when `vs->layout` is null, so the empty frame is just the window background; acceptable. Optionally paint a faint "loading…" — YAGNI unless requested).
- [ ] Worker reaping: on each new `begin_load`, move the prior `std::jthread` into the reaper (Invariant 4); never join on the UI thread.
- [ ] Manual smoke: open a large md file — window appears instantly blank, content snaps in a beat later; fast-tab several md files — no crash, only the last one renders; close a file mid-load — no crash.
- [ ] Run full `./build/Release/tests.exe` + `./scripts/visual-test.sh` (the screenshot tool path is synchronous — see Task 4).
- [ ] Commit — `feat(plugin-md): parse off the UI thread; adopt via WM_APP_DOC_READY`.

## Task 3: Colorizer — parse the tree on a worker thread

**Files:** Modify `src/plugin_colorizer/window/colorizer_host_adapter.cpp`. **Depends on Lever 3 Phase B** (`wlx_core_parse`/`WlxTree`).

- [ ] Same generation/worker structure. The worker calls `wlx_core_parse(core, raw_utf8, len, lang)` (the heavy ~43 ms+ parse) and posts the `wlx_core::TreePtr` payload. (Reading the file + computing the language stays on the UI thread — both cheap.)
- [ ] `WM_APP_DOC_READY` (colorizer): guard; on accept, move the `TreePtr` into `vs->tree`, build the cheap Lever-1-style layout skeleton, run the first-viewport `wlx_core_highlight_range`, `InvalidateRect`.
- [ ] Keep the grammar **prewarm** jthread (perf memory: it overlaps query compile) — it composes: prewarm warms the query while the parse worker runs.
- [ ] Manual smoke: open a multi-MB C++ file — instant window, colors snap in; scroll colors new regions (Lever 3 B3) with no hitch; fast-tab — no crash, last file wins.
- [ ] Commit — `feat(plugin-colorizer): parse tree off the UI thread`.

## Task 4: Keep the synchronous tool path + tests working

**Files:** `src/tools/screenshot/*` (read first).

- [ ] The screenshot tool and unit tests must remain **synchronous** (they capture one frame and exit — they can't wait on a `PostMessage`). Provide a synchronous entry that bypasses the worker (parse inline) so `--bench`/golden capture is deterministic. Either a `bool synchronous` flag on the load helper or the tool calling the parse + `do_layout` directly (it largely already does via `markdown_pipeline.cpp`/`colorizer_pipeline.cpp`). Verify no test depends on the async path.
- [ ] Run `./build/Release/tests.exe && ./build/Release/colorizer-tests.exe && ./scripts/visual-test.sh` — all green/≥95%.
- [ ] Commit — `test(tools): keep screenshot/bench path synchronous after async load`.

## Task 5: Benchmark + soak (gate)

- [ ] Bench: open-to-first-paint latency on a multi-MB file should drop to ~0 (window appears immediately); total time-to-content ≈ unchanged (work is the same, just off-thread). Measure with the real plugin (the synchronous tool can't show the async win) — use a manual timer or `WLX_TRACE` timestamps around `ListLoadW` return vs `WM_APP_DOC_READY`.
- [ ] **Soak for the threading bugs unit tests can't catch:** rapidly tab through a directory of large files (hold Down in TC's file pane with the lister open) for a minute; open + immediately close repeatedly; toggle dark/wrap mid-load. Watch for crashes, leaked workers (Task 1 reaper), and stale renders. This is the real acceptance test for Invariants 2-5.
- [ ] Update perf memory with the async open result and note Lever 2 is complete.

---

## Self-Review checklist (run before handing off)

- **Spec coverage:** scaffolding + generation guard (T1) ✓, md worker parse + adopt (T2) ✓, colorizer worker parse + adopt (T3) ✓, synchronous tool/test path preserved (T4) ✓, soak/threading acceptance (T5) ✓.
- **Invariant audit (do this explicitly):** grep the worker lambdas for any `vs->`/`*vs`/`ID2D1` capture (Invariant 1/2 — there must be none); confirm every `WM_APP_DOC_READY` path that rejects also `delete`s the payload (Invariant 3); confirm no `.join()` on the UI thread (Invariant 4); confirm a `g_shutting_down` flag gates worker bodies and is set at the top of `DLL_PROCESS_DETACH` (Invariant 5).
- **Dependency check:** this plan compiles only after Lever 1 (`do_layout` lazy/cheap) and, for Task 3, Lever 3 Phase B (`wlx_core_parse`). If either is absent, stop and finish it first.
- **Honest scope note for the PR:** state the measured before/after open latency and file size; if the win is marginal for typical files, say so — Lever 2 is the highest-risk lever and should not ship unless the parse latency it removes is real for the target workload.
