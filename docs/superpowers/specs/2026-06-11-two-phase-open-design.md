# Two-phase open (colorizer) — design

**Date:** 2026-06-11
**Status:** approved (brainstorming session 2026-06-11)

## Goal

Make big files readable near-instantly. Today the colorizer shows a Loading frame
until the load worker finishes read + whole-file parse (~900 ms for sqlite3.c);
the implicit grid made the layout independent of colors (a millisecond byte-scan
over raw bytes), so the view can show plain text as soon as the read completes
and let colors arrive when the parse lands. Target: text visible in well under
100 ms for any file the read can deliver that fast; colors pop in afterwards with
no loss of scroll, selection, or search state. No pending-colors indicator
(matches the sweep's existing pop-in behavior).

## Non-goals

- The markdown plugin (md4c parses ~1 MB in ~10 ms — nothing to gain).
- Wrap mode (the eager layout needs measured, colored runs; it stays
  single-phase exactly as today).
- Bench/tool changes (the win is host-side perceived latency; measurement via a
  `WLX_TRACE` stamp, not a bench row).
- Any change to `runtime/host/async_loader.h` (the mid-body post follows the
  existing discipline; the template's final post is unchanged).

## Design

### Worker: one thread, two posts

`begin_async_load`'s worker body (colorizer_host_adapter.cpp), after a
successful `fs.read`:

1. **Skip phase 1 when `wrap` is true** (single-phase as today) or when the read
   failed (the existing failed `ParseResultColor` is the only post).
2. Otherwise heap-allocate `TextResultColor {generation, live, raw_utf8}` with a
   **copy** of the raw bytes (the worker keeps its original for the parse — one
   transient file-sized copy, worker-side, freed when the parse ends), and post
   it via the new registered message `WlxListerineColorizer.TextReady`,
   mirroring `spawn_parse_worker`'s posting discipline exactly: re-check
   `g_shutting_down` and `live->closed` first (free locally on either), reclaim
   + free if `PostMessage` fails.
3. Continue as today: `wlx_core_prewarm` → `supports` → `wlx_core_parse` →
   return `ParseResultColor` (the template posts it — phase 2).
   `ParseResultColor` loses its `raw_utf8` field: phase 2 never carries the
   source again (the UI owns it after phase 1; the wrap/failed single-phase
   paths keep carrying it — see Adopt below for the exact field split).

`PostMessage` calls from one thread to one queue are delivered in order, so
phase 1 always adopts before phase 2 of the same generation.

### Adopt: phase 1 (`TextReady`)

Gated by `should_adopt_result` (token identity + generation + closed), view
recovered via `g_views` — the same pattern as the existing handlers. On adopt:

- `cached_raw_utf8 = std::move(res->raw_utf8)`;
- drop the OLD file's `tree` + `span_table.clear()` (they describe the previous
  file; the generation bump already cancelled its sweep);
- `do_layout(v, v->cached_raw_utf8, /*colors=*/{})` — grid skeleton, no blocks,
  milliseconds;
- `state = Ready` — the view is fully interactive on plain text (selection,
  search, copy, go-to-line all run over raw bytes in grid mode; colors are
  cosmetic);
- `InvalidateRect`.

### Adopt: phase 2 (`ParseDone`, slimmed)

Because the raw bytes did not change between phases, the skeleton, line index,
search index, scroll position, and any selection made meanwhile are all still
exact. The tree branch therefore does **not** relayout:

- `v->tree = shared_ptr<WlxTree>(...)` (null-safe conversion, as today);
- **clear the window**: `v->layout->blocks.clear(); v->layout->first_block_line
  = 0;` — the next paint's `ensure_grid_window` rebuilds the visible window
  through the normal `colors_for` path (tree highlight), so colors appear
  without touching any other view state;
- `begin_sweep(v)` + `InvalidateRect`.

Single-phase arrivals keep today's full adopt: the **wrap** path (carries
`raw_utf8`, runs the eager `do_layout` + whole-doc colors), the **failed-read**
path (clears the view), and the **treeless** phase 2 after a phase 1
(unsupported language → already plain, nothing to do; supported-but-parse-failed
→ `apply_whole_doc_fallback`, which colorizes whole-doc into `cached_colors`
and re-runs `do_layout`, feeding the span table). Field split (decided):
`ParseResultColor` keeps `raw_utf8` and gains `bool two_phase = false`. The
two-phase worker sets `two_phase = true` and leaves `raw_utf8` empty (phase 1
carried it); the adopt handler branches on the flag — `two_phase` takes the
minimal tree + window-clear path, otherwise today's full swap. Phase 1 uses a
new `TextResultColor {generation, live, raw_utf8}` with its own registered
message.

### Interleavings (resolved by existing machinery; verify each in the plan)

- **F2/reload between phases** — generation bump; the orphaned phase 2 fails
  `should_adopt_result`; the new load owns the view.
- **Dark-flip between phases** — `state == Ready` + `tree == nullptr` + raw
  non-empty routes `lc_newparams` to `begin_async_recolor`; its generation bump
  drops the in-flight phase 2; the recolor re-parses the already-swapped new
  raw. Correct colors, no stall.
- **Close between phases** — `ListCloseWindow`'s drain gains a third
  `PeekMessageW` loop for queued `TextReady` payloads (mirrors the existing
  parse/sweep drains).
- **Scroll/select during the gap** — works on plain text; phase 2 preserves it
  (window clear only).

### Testing

- Unit: the result-struct split compiles into the existing async-loader guard
  tests; a small test for the phase-2 window-clear behavior (blocks emptied,
  `first_block_line` reset, line_tops/total_height untouched) using the grid
  fixtures.
- Suites + visual stay green (tool untouched).
- Manual TC soak addition: open sqlite3.c — text readable in well under 100 ms,
  colors arrive ~1 s later with scroll/selection preserved; dark-flip and F2
  inside the gap behave; `WLX_TRACE` stamps read→text-ready and
  text-ready→parse-done for DebugView measurement.

## Success criteria

- Opening sqlite3.c in TC shows readable plain text essentially at read speed
  (tens of ms), colors arrive at parse completion (~1 s), and the view never
  loses scroll/selection/search state across the transition.
- All ~480 unit tests + 29 goldens unchanged and green; no bench regressions
  (`uv run scripts/bench.py` — open-ms rows unaffected, since the tool path is
  untouched and the worker does the same total work plus one transient copy).
