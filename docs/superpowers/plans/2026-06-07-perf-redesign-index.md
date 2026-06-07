# Performance Redesign — Plan Index & Sequence

> **For agentic workers:** This is an index, not an executable plan. The three plans below are sequenced. Execute them **in order**, gating each on the one before. Each links to its own bite-sized, TDD plan document.

**Origin:** Follow-on to the 2026-06-05/06 low-level alloc/copy optimization pass (which netted ~3% on the hot path by reducing copying *around* the two dominant synchronous costs). This redesign attacks the costs themselves:

- **Markdown:** eager full-document `IDWriteTextLayout` creation in `ListLoadW` — 212 ms (12k file) / 385 ms (1 MB). Markdown is the only plugin still doing this; the colorizer already went viewport-lazy (210 ms → 16 ms).
- **Colorizer:** whole-file tree-sitter highlight — 35 ms recurring per file on a 12k file (parse is separate and stays whole-file).
- **Both:** `ListLoadW` blocks ~400 ms before the window shows anything.

---

## The three levers

| # | Lever | Plan | Attacks | Depends on |
|---|-------|------|---------|------------|
| 1 | Viewport-lazy **markdown layout** (estimate + reflow, single-threaded) | [lever1-lazy-markdown-layout](2026-06-07-lever1-lazy-markdown-layout.md) | Markdown layout 385 ms → ~tens of ms first-open | — |
| 3 | Viewport-scoped **tree-sitter highlight** (range primitive + colorizer viewport-colorize) | [lever3-viewport-scoped-highlight](2026-06-07-lever3-viewport-scoped-highlight.md) | Colorizer colorize 315–367 ms first-open | — (independent) |
| 2 | **Async non-blocking `ListLoadW`** (worker-thread layout/colorize, UI-thread paint) | [lever2-async-nonblocking-listload](2026-06-07-lever2-async-nonblocking-listload.md) | Perceived load latency → ~0; removes Lever 1's scroll drift | **Lever 1** (wraps its lazy materialization) |

## Execution order & gates

1. **Lever 1 first.** It is the foundation Lever 2 wraps (Lever 2 moves Lever 1's materialization off-thread and uses background measurement to remove Lever 1's scroll drift). **Gate:** all unit tests green (`tests.exe`), visual regression ≥ 95% on all 27 cases, and a bench run showing markdown `layout` first-open dropped substantially on a ≥ 1 MB file.
2. **Lever 3 second** (independent of Lever 1; can run in parallel if a second engineer is available). **Gate:** colorizer-tests green, Stage-2 token diff byte-identical at default range, visual smokes 100%, bench showing colorizer `colorize` first-open dropped on a ≥ 1 MB file.
3. **Lever 2 last.** Highest complexity (COM/threading). **Do not start until Lever 1 is merged** — its tasks reference Lever 1's `MdMaterializeCtx` / `materialize_viewport` directly. **Gate:** no UI-thread stall in `ListLoadW` (bench/manual), no crash under fast file-tabbing (ListLoadNextW spam), thread-sanitizer-style manual review of `ViewState` lifetime.

## Cross-cutting conventions (all three plans)

- **Build:** `cmake --build --preset conan-release` (after first-time `conan install . --output-folder=build --build=missing -s build_type=Release -s compiler.cppstd=20 && cmake --preset conan-default`).
- **Unit tests:** `./build/Release/tests.exe` (markdown, doctest) and `./build/Release/colorizer-tests.exe` (colorizer/core). Single case: append `-tc="<case name>"`.
- **Visual regression:** `./scripts/visual-test.sh` (needs bash + uv + Pillow). Threshold ≥ 95% pixel similarity.
- **Bench:** `./build/Release/screenshot_tool.exe --bench <file> --width 1000 --height 1200 --dark`. Median of ≥ 11 runs; discard a warm-up.
- **Memory safety is priority #1** (per repo `CLAUDE.md`). Every plan has an explicit invariants section; do not skip it.
- **Surgical changes:** match existing style; don't refactor adjacent code; don't delete pre-existing dead code (mention it).

## What is explicitly NOT in scope (documented no-gos, from the architecture review)

- Data-oriented AST rewrite (arena/SoA) — parse + AST build is ~10 ms, off the critical path.
- Sub-DirectWrite custom layout (cached glyph runs) — would trade away bidi/complex-script/font-fallback correctness.
- Disk-persistent layout/color cache — compiled `TSQuery` and `IDWriteTextLayout` are not serializable; the in-memory caches already cover the common revisit case.
- Incremental tree-sitter parsing — only helps re-parse on edit/`ListLoadNext`, not the initial open.
