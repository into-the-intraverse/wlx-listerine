# Stage-2b — markdown skeleton memory

Follow-up to `2026-06-11-memory-optimization-design.md` (which deferred this after
spec'ing the decision). Goal: cut the held memory of a large markdown file.

## Measured problem (2026-06-14, this machine)

`screenshot_tool big.md --bench --lazy` (1.0 MB, 8401 AST blocks → 19601 layout blocks):

| phase | cumulative WS |
|-------|---------------|
| after parse (AST built) | +19.0 MB |
| **after estimate layout** | **+103.5 MB** |
| after materialize + paint | +122.7 MB |

The **layout estimate pass alone adds ~84 MB**. The AST is only ~13 MB; materialize
+ paint add ~20 MB. So the skeleton — not the AST, not the viewport layouts — is the
bulk.

Root cause: `layout_engine.cpp:322` builds **List / BlockQuote / Table / HR eagerly**
— every list item, table cell, and quoted paragraph gets a real `IDWriteTextLayout`
created during the estimate pass and **never evicted** (their recipe kind is `None`,
which `materialize_viewport`'s eviction loop skips). big.md is block-diverse (every
section has a list, a 6-cell table, and a blockquote), so ~10k eager layouts are held
for the whole document. Lazy blocks (paragraph/heading/code-fence) already defer their
layout and only the viewport is materialized.

## Decision — evict the eager blocks (not full deferral)

Two options were on the table:

- **(A) Full deferral** — estimate list/table/quote geometry arithmetically and build
  layouts only on paint (the colorizer-grid approach). Cuts peak + open time too, but
  needs estimate-pass geometry for tables (row height = max cell height) with reflow
  correction — high regression risk across hit-testing, search, gutter, and the 29
  visual goldens.
- **(B) Evict the eager blocks** *(chosen)* — keep building them eagerly (geometry
  stays byte-exact: no estimate drift, no reflow, no line-index/hit-test changes), but
  give them a recipe so the existing eviction drops their off-screen layouts. The host
  already passes recipes, so this is a real product win.

The bench measures **held** memory for md (peak isn't reported), and held is identical
between A and B (both end with off-screen layouts gone). B's only cost vs A is that
open still builds-then-frees the eager layouts (peak + open time unchanged). Given the
risk delta, B captures essentially the whole measured win at a fraction of the risk.

## Mechanism — `BlockRecipe::Kind::InlineFixed`

A geometry-preserving recipe for eager inline blocks. Unlike `Kind::Inline` (which
recomputes `rect.bottom`/`run.rect` from the estimate→measured transition),
`InlineFixed` rebuilds **only** the `IDWriteTextLayout` + colors + spans and leaves the
(already-exact) `lb.rect`/`run.rect` untouched:

- stores `inlines`, `max_width` (exact, not derived — avoids float-ULP drift on cells),
  `default_color`, `force_bold`, `format`, and **`alignment`** (table-cell columns);
- replay calls `build_inline_layout(..., alignment)`, assigns the run layout, and
  offsets spans by the kept `run.rect` origin;
- re-materialization is byte-identical because both the eager build and the replay use
  the same `build_inline_layout` inputs → delta 0, no reflow, no index rebuild.

Eager builders (`layout_list_item`, `layout_table` cells, eager `layout_paragraph` /
`layout_heading`) set an `InlineFixed` recipe right after pushing a block that has a
layout, only when `lazy_`. The blockquote container, HR, and (M1) eager nested code
fences have no `InlineFixed` recipe and stay non-evictable (cheap / rare).

The screenshot tool's lazy bench path now passes `&ctx->recipes` to
`materialize_viewport` (it did not before), so the bench reflects the host's eviction.

## Blast radius

`md_materialize.{h,cpp}` (recipe kind + replay), `layout_engine.{h,cpp}` (recipe
capture at 4 eager sites), `markdown_pipeline.cpp` (bench passes recipes). No ABI
change. The md block model and the colorizer are untouched.

## Verification

- ~470 unit tests green; new: `InlineFixed` cell re-materialization preserves alignment
  + geometry; updated md-eviction test (list items now evict + re-materialize).
- 29 visual goldens at 100% (scroll 0 — visible blocks are never evicted, so output is
  unchanged; eviction only frees off-screen layouts).
- `scripts/bench.py`: md held drops; open time within noise; eager "worst case" row
  (non-lazy) unchanged. Re-baseline via `--update`.
