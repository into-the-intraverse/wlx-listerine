# Design: Lexilla migration round 2 — ABI v6, 4 languages, M6 finish

**Date:** 2026-06-19
**Status:** approved (user "accept all phases"); proceeds to writing-plans → subagent execution.
**Builds on:** `docs/superpowers/specs/2026-06-18-lexilla-migration-design.md` (round 1 — engine
swap shipped: Lexilla is the live colorizer engine, tree-sitter + Unreal removed, 428 tests
green, ABI kept at v5-shape, 4 languages plain-text).

## Goal

Finish the migration: (A) the ABI-v6 simplification deferred in round 1, (B) Lexilla coverage
for the 4 plain-text languages, (C) the M6 finish (perf bench, docs, visual goldens).

## A. ABI v6 — remove the cached-tree path + background sweep

The cached-tree ABI (`wlx_core_parse` / `highlight_range` / `free_tree`, `WlxTree`) and the
colorizer's chunked background sweep exist only so the old tree-sitter path could parse once,
slice per chunk, and free the giant tree. Lexilla streams (no tree), so this collapses.

**Approach:** the colorizer worker calls `wlx_core_colorize(whole file)` **once** → feeds the
`SpanTable` → seals. Removes from the ABI: `WlxTree`, `wlx_core_parse`,
`wlx_core_highlight_range`, `wlx_core_free_tree`, `wlx_core_prewarm` (vestigial no-op), and the
`TreePtr`/`TreeDeleter` shim. `WLX_CORE_ABI_VERSION` 5 → **6**.

*Rejected:* a chunked feed via repeated `colorize(range)` calls — Lexilla re-lexes from byte 0
each call → O(chunks × filesize). The single whole-file call is correct and fast (streaming lex,
far cheaper than the tree-sitter parse it replaced).

**Core DLL:** drop the matching `Colorizer` / `CoreRegistry` methods + the `WlxTree` struct;
keep `colorize`, `supports`, `available_languages`, `theme`, `list_languages`, `theme_color`.

**Plugin (`plugin_colorizer`):** the worker's `parse_tree → ParseDone → chunked sweep →
free_tree` becomes `colorize(whole) → feed SpanTable → seal`. Delete `sweep_chunk.h`, the
`SweepDone` message + sweep worker, the free-tree generation dance, and `WlxTree` from
`ColorViewState`. **Kept unchanged to bound regression risk:** the two-phase open (`TextReady`
plain → colored), the `SpanTable`, the implicit grid, and the `ViewLiveToken` HWND-recycle
safety. The core mutex stays (now held once, briefly, per open — Lexilla is fast).

**Tests:** delete `test_sweep_parity.cpp` (sweep gone); rewrite `test_wlx_core_abi.cpp` for v6
(drop parse/highlight_range/free_tree cases, version → 6); keep `test_span_table` / grid tests.

## B. The 4 remaining languages

- **html (`hypertext`) / xml (`xml`) / php (`phpscript`)** — all via `LexHTML`, which HAS
  `lexicalClasses` but uses HTML-specific tags (`tag`, `attribute`, `tag operator`, `literal`)
  and prefixes embedded-script tags (`client javascript keyword`, `server php literal string`,
  `client python literal string character`, …). Change `tag_to_scope` from **prefix-match to
  suffix/contains-match** (so the generic token at the end of an embedded-script phrase
  resolves) and add the base HTML tokens. **RISK: `tag_to_scope` is shared by all 15 working
  languages** — full colorizer-tests + a per-language span check must pass unchanged.
- **cmake** — `LexCMake` has no `lexicalClasses`, so add an explicit `SCE_CMAKE_*` map +
  command/parameter word-lists (cmake colors commands only via word lists).

Each new language gets a `test_lexer_registry` coverage case.

## C. M6 — finish

- **Perf bench:** re-run `scripts/bench.py`, update the README baseline.
- **Docs:** scrub tree-sitter / grammar references from CLAUDE.md, README, CONFIGURATION.md,
  LANGUAGES.md, BUILDING.md; delete dead `scripts/{fetch,build,package}-grammars.sh`; fix the
  stale `apply_cpp_variant` comment in `colorizer_host_adapter` (force_grammar_id doc).
- **Visual goldens (needs user review):** the 29 cases compare our output to *Chrome's*
  highlighter at ≥95% similarity. Lexilla's palette differs, so some may dip below threshold.
  Regenerate via the screenshot tool and **report pass/fail per case** rather than silently
  rebaselining; the user decides if any need attention.

## Execution

Subagent-driven (user: "fix with subagents"). Dependency order: **A first** (ABI/core/plugin —
sequential, shared surface), then **B** and **C-docs** can run in parallel, then **C-bench +
goldens** last (need the final build). Each subagent task is self-contained with an explicit
verify (build + targeted tests). Final gate: full build + both suites green + goldens reviewed.

## Risks

1. Plugin async-flow rewrite (A) — subtle two-phase/generation/token races; keep scaffolding,
   swap only parse+sweep+free → colorize+feed.
2. `tag_to_scope` suffix-match (B) — shared by 15 langs; re-verify full suite.
3. Goldens (C) — palette differs from Chrome; review, don't blind-rebaseline.
