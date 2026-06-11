# Memory optimization — design

**Date:** 2026-06-11
**Status:** approved (brainstorming session 2026-06-10/11)

## Goal

Bring per-open-file memory down from ~40–60× file size to roughly "file size + colors
+ a small materialized window", and make scrolling memory-bounded. Current README
baselines (held after open, median of 5):

| Scenario | Input | Held today | Target |
|----------|-------|-----------:|-------:|
| colorizer cached-tree | sqlite3.c (8.8 MB) | 349.5 MB | **≤ 70 MB** |
| colorizer cached-tree | json.hpp (0.9 MB) | 53.7 MB | **≤ 35 MB** (expected ~25–30; ceiling allows for the unconfirmed fixed-overhead share) |
| markdown lazy | big.md (1.0 MB) | 123.1 MB | set after Stage 1 instrumentation |
| post-scroll held (new row) | sqlite3.c | unmeasured, unbounded | within ~10 MB of post-open |

Open time stays within +10% of the current baseline (expected to improve — we stop
building 250k LayoutBlocks); peak no worse than today's.

## Where the memory goes (verified against code, 2026-06-10)

Colorizer, sqlite3.c, cached-tree path, ~250k lines:

| Contributor | ~Size | Evidence |
|---|---:|---|
| TSTree, retained for view lifetime | 100–150 MB | README's own cached (349.5) vs whole-file (250.7) rows; tree is the difference |
| Eager per-line skeleton: 240 B `LayoutBlock`/line + heap-allocated `text_runs` vector/line + UTF-16 `run.text`/line | ~120 MB | `colorizer_layout.cpp` build loop (~:637–711) |
| Redundant whole-file copies: `cached_text` (UTF-16, **dead** — `layout_source` consumes only `raw_utf8`), `cached_raw_utf8`, tree's private source copy | ~35 MB | `colorizer_host_adapter.cpp:104-105`, `colorizer_layout.h:51` |
| Fixed process overhead inside the bench "delta" (factories, grammar DLL, query, fonts) | ~20–25 MB (unconfirmed) | inferred from json.hpp numbers; instrumentation separates it |

Markdown big.md: AST ≈ 9 MB (measured); the remaining ~100 MB of the 123 MB delta is
unexplained — the md bench reports one coarse number.

Both plugins: materialized `IDWriteTextLayout`s are never evicted. Scrolling once
through a large file accumulates per-line layouts (KBs each) without bound. Bench
numbers are scroll-position-0; real sessions are strictly worse.

## Decisions (with rejected alternatives)

1. **Tree policy: sweep-highlight, then free.** Parse stays whole-file (correctness);
   highlight is chunkable via the existing v5 `wlx_core_highlight_range`. After the
   sweep produces a whole-file span table, the tree is freed and the grammar unpinned.
   Rejected:
   - *Moving-window / chunked parse, releasing processed subtrees* — tree-sitter has no
     partial-parse result (cancelled parse returns NULL), no subtree-release API
     (`ts_tree_delete` is all-or-nothing), and independent chunk parses are wrong for
     C-family languages (block comments / strings / `#if` / templates span chunks; the
     repo already documents fragments parsing as ERROR).
   - *Chunk parse with a correctness margin* — the context a byte needs is unbounded and
     not locally discoverable (lexical state depends on all preceding bytes; C++ `<`
     needs a parse from clean state). Per-language prefix heuristics across 27 grammars
     fail silently with wrong colors.
   - *Prefix-parse windowing* (`included_ranges = [0, viewport_end+margin]`) — sound but
     optimizes the transient peak, not the held-while-viewing number that was the
     complaint; re-parse cost grows with scroll depth; Ctrl+End builds the full tree
     anyway; permanent correctness boundary at the cut.
   - *Re-parse on demand* — ~1 s color pop-in every couple of screens on big files.
   - *Idle-TTL* — does not reduce held-while-viewing.
   The full tree existing transiently during parse+sweep (~1–2 s) is irreducible on
   tree-sitter; every tree-sitter editor holds it forever, we hold it for seconds.
2. **Skeleton: implicit grid** (approach 2) over surgical field-trims (approach 1):
   only the grid reaches "proportional to file + fixed overhead".
3. **Scope:** colorizer skeleton + copies, markdown (instrumentation-gated), layout
   eviction on scroll (both plugins).

## Design — colorizer (no-wrap, supported language: the default path)

### Steady-state per view

| Kept | sqlite 8.8 MB | Notes |
|---|---:|---|
| `cached_raw_utf8` | 8.8 MB | the **one** text copy |
| `line_byte_starts` | ~1 MB | exists today |
| `span_table` (new) | ~24 MB | flat, sorted `{start, len, color, modifiers}`; sweep output; replaces `cached_colors` |
| materialized window | ~1–2 MB | real `LayoutBlock`s for viewport ± overscan (~100–200 lines) |
| `line_tops` | ~1 MB | kept as-is (gutter contract; not worth touching) |

Deleted from steady state: eager whole-file `blocks` vector, `cached_text`, the tree,
`cached_colors`, the `colored_lo/hi` interval machinery (retired once the table is
complete).

### Lifecycle

1. **Open (unchanged):** async read+parse worker → tree adopted on UI thread →
   viewport highlighted against the tree (today's v5 path) → first paint identical.
2. **Sweep (new):** background worker walks the file in fixed byte chunks (~256 KB),
   `wlx_core_highlight_range` per chunk against the live tree, appending to the
   span table. Core mutex taken per chunk → viewport highlights interleave; scrolling
   during the sweep behaves exactly like today. Each chunk checks the `ViewLiveToken`
   generation: close / reload / re-language / dark-flip aborts between chunks (free
   partial win on the README "monolithic colorize isn't cancellable" issue). The sweep
   worker follows the async-loader rules: holds no `ViewState`, only the token +
   copyable data; module pin applies. **The table has a single writer** — the sweep,
   linear 0→EOF, so it is sorted by construction. Mid-sweep viewport highlights feed
   only the materialized window's `color_ranges` (as today); the sweep re-covers that
   region when it arrives (tiny duplicate highlight work, no merge logic).
3. **Settle:** sweep done → tree freed (`wlx_core_free_tree`), grammar unpinned. Scroll
   coloring reads the table (binary search) — instant, forever.

No ABI change: v5 `highlight_range` + `free_tree` suffice; the table is plugin-side.

**Dark flip / forced language after settle:** colors are theme-resolved, so these
re-parse + re-sweep through the existing async recolor funnel (which now re-parses,
since no tree). Old colors stay painted until replaced; ~2 s pop-in on sqlite-sized
files, instant on normal ones. Theme-independent style-ID spans (ABI v6) would make
flips free — explicitly out of scope.

**Fallbacks:** word-wrap ON keeps today's eager whole-file path (needs measured
heights; opt-in, rare). Unsupported language / parse failure → grid with empty table
(plain text), no tree. Sweep chunk failure → abort sweep, keep the tree (today's
behavior as fallback — correct, memory-heavy, traced).

### Implicit grid

In no-wrap mode line geometry is arithmetic (`y = pad + index × line_height` — already
a documented invariant in the build loop). `LayoutDocument.blocks` becomes only the
materialized window plus a `first_block_line` offset:

- **Paint:** lines entering the window materialize exactly like today's lazy path —
  `decode_line` (exists) → `expand_tabs` → color ranges sliced from the span table →
  `CreateTextLayout` → URL/whitespace/indent/trailing decorations. Lines leaving the
  window are destroyed — **colorizer eviction falls out of the window for free**.
- **Hit-test/selection:** y→line arithmetic; positions become {line index, char
  offset}; off-window text decodes from raw on demand. Ctrl+A/Ctrl+C builds the
  clipboard string transiently (same cost as today).
- **Search:** `SearchIndex` builds from `cached_raw_utf8` (decode) on first search
  instead of flattening block `run.text`. Stays on-demand; transient ~4× file in
  UTF-16 while search is active on a huge file — accepted.
- **Links:** URL spans exist only for window lines — identical to today's lazy
  behavior (off-screen links were never hit-testable).
- **Blast radius:** `colorizer_layout.cpp` build loop, colorizer host adapter, and
  grid awareness in RenderEngine + InteractionEngine where `blocks[i]` ↔ line `i` is
  assumed. The md plugin's block model is untouched.

## Design — markdown

**Stage 1 — instrumentation (unconditional, gates Stage 2).** The md `--bench`
pipeline samples working set per phase: after read → after md4c parse → after
estimate layout → after first materialize+paint → after settle. Buckets the 123 MB
into AST / layout skeleton / materialized layouts / render target / DWrite-internal
(process-fixed). If the bulk is DWrite/D2D internals, it is shared process overhead —
correct the README table's attribution rather than "fixing" phantom memory.

**Stage 2 — fixes ranked by Stage 1; two known-real items regardless:**

- **md layout eviction:** materialized blocks outside viewport ± K screens drop the
  `IDWriteTextLayout` + decoration vectors but **keep the measured rect** — geometry
  stays exact (no estimate-drift, no re-shift); memory bounded by the window.
  Re-entry re-materializes via the existing recipe machinery.
- **CacheService byte budget:** parse + layout caches hold up to 16 full Documents and
  16 LayoutDocuments process-wide regardless of size. Entry cap → approximate byte
  budget using the existing AST size estimate; evict LRU until under budget; the
  current file is always retained.

AST text-storage slimming (per-inline `wstring` → offsets into one buffer) is
**deferred** unless Stage 1 shows the AST share is much larger than the measured 9 MB.

## Verification

1. **Bench correctness:** in bench mode the tool runs the sweep synchronously to
   completion before sampling "held" (otherwise the freed tree never shows up). New
   bench row: **post-scroll held** — scroll N screens through sqlite, sample again.
2. **Targets:** table in Goal, via `scripts/bench.py` (median of 5, this machine),
   README baseline updated via `--update` once shipped.
3. **Correctness:** ~470 unit tests green; 29 visual goldens at 100% parity
   (table-served colors must be byte-identical to tree-served colors — same query,
   same theme). New unit tests: grid window edges (first/last line, empty file, huge
   single line), span-table slicing, sweep abort-on-generation, md eviction geometry
   stability, CacheService byte-budget eviction.
4. **Manual TC soak** (async paths are not unit-testable, per the async-loader
   precedent): rapid file tabbing mid-sweep, dark-flip mid-sweep, close mid-sweep,
   scroll-during-sweep on sqlite, quick-view panel reuse, force-language mid-sweep.

## Edge cases

- Scroll past the swept region mid-sweep → on-demand `highlight_range` against the
  still-live tree (today's path); sweep continues. No UX gap during the sweep window.
- Huge single line (minified file) → one window line materializes one huge layout —
  same as today's lazy path.
- Glance-and-close → sweep aborts between chunks via generation check; tree freed by
  normal `TreePtr` destruction (never under the loader lock — ViewStates still leak on
  `DLL_PROCESS_DETACH` by design).
- `lc_setpercent`, Ctrl+G, gutter → pure arithmetic in the grid; behavior unchanged.

## Non-goals

- ABI v6 / theme-independent style-ID spans (dark flips stay re-parse + re-sweep).
- Word-wrap-mode memory for huge files (keeps the eager path; opt-in and rare).
- md AST storage redesign (unless Stage 1 instrumentation demands it).
- The registry-mutex-held-during-parse known issue (sweep chunking only softens the
  highlight side; parse cancellation stays a separate README TODO).
- File-I/O pagination (the parser needs all bytes; raw file is ~2.5% of the problem).
