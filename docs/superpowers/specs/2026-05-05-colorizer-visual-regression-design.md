# Colorizer Visual Regression — Design

**Date:** 2026-05-05
**Status:** Approved (brainstorm); pending implementation plan
**Owner:** aleksej.pawlowskij

## Problem

The markdown plugin has automated visual regression: 28 cases under
`test_data/cases/`, each with a Chrome-rendered golden PNG, compared
pixel-wise by `test_data/compare.py` from `scripts/visual-test.sh`.

The colorizer plugin has none. The 26 grammar samples in
`test_data/grammar_samples/` are exercised only by hand and by one
routing-logic doctest. Theme tweaks, query edits, render-engine
refactors, and grammar updates can silently break highlighting; the
first signal is a user noticing.

This spec adds a regression suite that mirrors the markdown shape but
splits the colorizer's two distinct failure surfaces: *what color does
each token get* (semantic, theme-driven) and *how does that get painted*
(pixel, font/AA-driven).

## Goals

- Catch grammar/query semantic regressions per-token, with `line:col`
  precision.
- Catch theme→scope mapping regressions (e.g., bold accidentally dropped
  on `keyword.directive`).
- Catch colorizer-side render regressions that don't change tokens
  (gutter math, italic glyph wiring, indent guides, whitespace marks).
- Reuse the existing pipeline shape: one tool, one shell script, one
  Python comparator, one regen entry point — symmetric with the markdown
  side so the workflow is "the same thing, twice".
- Keep golden churn proportional to actual change. A theme tweak should
  not rewrite 26 JSON files for no semantic reason.

## Non-goals

- Cross-tool reference (bat / Helix / Pygments) — different highlighters
  make different opinionated choices; we'd fight noise. Self-snapshot
  is the ground truth.
- Cross-DPI / cross-font / cross-resolution coverage — single 800px-wide
  fixed-DPI snapshots only.
- Token coverage of every grammar in both themes — dark only, plus one
  light spot-check.
- Markdown-fenced-code highlighting coverage — already exercised by
  stage 1 (`06_code_fenced.md`); not double-covered.
- Editor-runtime concerns (scroll, hit-test, selection) — not paint.

## Architecture

Three stages of `scripts/visual-test.sh`, each independent, all run
even if an earlier one fails (accumulate-then-exit; see §"Failure
modes").

```
visual-test.sh
  ├─ Stage 1: Markdown cases (existing, unchanged)
  │     screenshot_tool *.md → *.png → compare.py vs *_chrome.png
  │
  ├─ Stage 2: Colorizer token snapshots (new)
  │     screenshot_tool sample.<ext> --dump-tokens → sample.<ext>_tokens.json
  │     compare_tokens.py vs sample.<ext>_tokens.golden.json
  │
  └─ Stage 3: Colorizer pixel smokes (new)
        screenshot_tool colorizer_smokes/<smoke> --full → smoke_*.png
        compare.py colorizer_smokes/ vs smoke_*_golden.png
```

### Reused without modification

- `compare.py` (gains an additive `--subdir <name>` flag; existing
  positional `[case_name]` filter and existing `cases/` default both
  unchanged).
- `RenderEngine`, `ThemeService`, `FileService`, `Colorizer`,
  `colorizer_layout::layout_source`, the wlx-core grammar cache.
- `bun run update-goldens` regen entry point (extended with a
  colorizer branch; same filter UX).
- The pre-commit hook (trigger globs extended).

### New artifacts

- One routing branch + one extracted pipeline file pair in
  `screenshot_tool` (see §"Components / 1. screenshot_tool").
- `--colorizer`, `--lang`, `--cpp-grammar`, `--dump-tokens`,
  `--display-config` flags on `screenshot_tool`.
- `test_data/compare_tokens.py`.
- `test_data/colorizer_smokes/` directory with 5 minimal samples and
  their `_golden.png` siblings.
- Token JSON goldens next to each `grammar_samples/sample.*` file
  (`sample.cpp_tokens.dark.golden.json` etc., plus the one
  `sample.py_tokens.light.golden.json`), 27 in total.
- One doctest "wiring alive" smoke and one C++ unit test for the JSON
  writer in `colorizer-tests`.

## Components

### 1. `screenshot_tool` extensions

New flags, additive — existing flag surface untouched:

```
screenshot_tool <input> [existing flags]
                        [--colorizer]
                        [--lang <id>]
                        [--cpp-grammar standard|unreal]
                        [--dump-tokens]
                        [--display-config <toml>]
```

Routing in `main.cpp`:

- `ext == ".md" && !--colorizer` → markdown pipeline (existing).
- otherwise → colorizer pipeline (new).

Pipeline body extraction (keeps `main.cpp` thin and matches the
project's "one public module per file" convention):

- Move existing markdown body to `src/tools/screenshot/markdown_pipeline.cpp`.
- Add `src/tools/screenshot/colorizer_pipeline.cpp`.
- `main.cpp` keeps arg parsing + routing only.

Colorizer pipeline:

```
FileService.read(path)
  → wlx_core::acquire_compatible() (existing — already used by markdown pipeline)
  → resolve language (--lang override, else infer from extension)
  → apply_cpp_variant(lang, --cpp-grammar)  (existing routing primitive)
  → core->colorize(source, lang, theme)
  → if --dump-tokens:
        write_token_json(out_path)
        return
    else:
        layout = colorizer_layout::layout_source(...)
        renderer.paint(layout, scroll=0)
        renderer.save_to_png(out_path)
```

Output path convention (matches existing markdown side; theme-aware
in dump-tokens mode so dark/light runs don't clobber each other):

- `--dump-tokens` writes `<input_dir>/<stem>.<ext>_tokens.<theme>.json`
  where `<theme>` is `dark` if `--dark` was passed, else `light`.
- Paint mode writes `<input_dir>/<stem>.<ext>.png` (or
  `<stem>.<ext>_dark.png` if `--dark`) — same suffix convention the
  existing markdown pipeline already follows. Extension-aware stem
  keeps `sample.cpp.png` distinct from `sample.cs.png`.

### 2. Token JSON schema

```json
{
  "source": "sample.cpp",
  "language": "cpp",
  "theme": "default_dark",
  "config_hash": "a1b2c3d4",
  "token_count": 2341,
  "tokens": [
    { "line": 1, "col": 0, "len": 8,
      "scope": "keyword.directive", "color": "#FF7B72", "mods": ["bold"] },
    { "line": 1, "col": 9, "len": 17,
      "scope": "string", "color": "#A5D6FF", "mods": [] }
  ]
}
```

Determinism rules (enforced by the writer):

- `line` / `col` are **1-based, char-indexed over the wstring source**.
  Sidesteps the byte-vs-wchar ambiguity flagged in `colorizer_layout.h`
  for the snapshot layer; the layout engine's existing assumption is
  unaffected.
- `mods` is sorted, lowercase, restricted to the four real bits
  (`bold`, `italic`, `underline`, `strikethrough`). Terminal-only
  Helix modifiers are dropped (already handled by `parse_style`).
- Adjacent tokens with identical `(scope, color, mods)` are collapsed
  by the writer regardless of how the query emitted them.
- Tokens are sorted by `(line, col, -len)` (longest span wins on ties)
  before write, so two tree-sitter query orderings produce identical
  JSON.
- `color` is a resolved hex string, not a palette name. A palette
  rename does not churn goldens; a real color change does.
- `config_hash` is a SHA1 hex digest computed by the JSON writer over
  the byte-stable concatenation of:
  - the theme name string,
  - the resolved theme TOML *after* `inherits` chains are flattened —
    palette + scope→style table — emitted as key-sorted canonical JSON,
  - the active `ColorizerDisplayConfig` fields emitted as key-sorted
    canonical JSON.
  Surfaces "you regenerated this with a different theme/config" as a
  one-line diff before any per-token diff appears. Crucially: a
  palette-key rename or whitespace edit of the theme TOML that does
  not change resolved colors does **not** change `config_hash`.

Goldens live next to the source:

- `grammar_samples/sample.cpp_tokens.dark.json` (just-produced,
  gitignored)
- `grammar_samples/sample.cpp_tokens.dark.golden.json` (committed)
- `grammar_samples/sample.py_tokens.light.json` (just-produced from
  the spot-check run, gitignored)
- `grammar_samples/sample.py_tokens.light.golden.json` (committed —
  the one light-theme spot check)

The `.dark` / `.light` infix is always present; there is no
unsuffixed default. Goldens for the 26 dark-theme samples are
`*_tokens.dark.golden.json`; the one light spot-check is
`sample.py_tokens.light.golden.json`. 27 goldens, fully symmetric.

### 3. `compare_tokens.py`

Walks `grammar_samples/`. For each `*_tokens.<theme>.golden.json`,
loads it together with the just-produced `*_tokens.<theme>.json` of
the same `(stem, theme)` pair. The committed golden set is the
authoritative list of "what should be checked"; samples without a
golden are skipped (see missing-golden behavior at the bottom of
this section).

Diff order — first divergence wins, no avalanche:

1. **`config_hash` mismatch** → exit with code 2 and a single line:
   `config_hash drift in sample.cpp_tokens.dark.json (got X, golden Y) — regenerate goldens (bun run update-goldens)`. Skip the per-token diff; it would be uniformly noisy.
2. **`token_count` mismatch** → report delta and the *first* index
   where the two streams diverge.
3. **Per-token diff** at the first divergent index → emit
   expected/got, byte-aligned.
4. Write `<sample>_tokens_diff.txt` with ~10 tokens of context above
   and below the divergence so the diff is reviewable in the PR/commit.

Exit codes:

- `0` — all PASS.
- `1` — at least one FAIL.
- `2` — at least one `config_hash` mismatch (treated separately so
  CI can flag "you forgot to run update-goldens" distinctly from
  "real regression").

Missing-golden behavior matches `compare.py`: `WARN`, exit 0, do not
fail the build. (You can add a sample without a golden in one commit
and add the golden in the next without breaking CI in between.)

### 4. Pre-commit hook trigger globs

Extend the hook from
`.cpp` / `.h` / `.toml` / `test_data/cases/*.md` to also fire on:

- `test_data/grammar_samples/sample.*`
- `test_data/colorizer_smokes/*`
- `config/themes/*.toml`
- `grammars/*/queries/*.scm`

Existing `.cpp` / `.h` / `.toml` patterns continue to cover everything
else. Goldens (`*_tokens.golden.json`, `*_golden.png`) themselves do
not trigger the hook — committing a regen alone validates against
itself, so the hook would always pass anyway.

### 5. Doctest "wiring alive" smoke

In `tests/plugin_colorizer/`:

```
TEST_CASE("colorize sample.cpp produces a non-empty token stream with a known keyword"
    * doctest::skip(!grammars_present)) {
    // colorize the bundled sample.cpp via core ABI
    // assert >100 tokens
    // assert at least one token has a scope starting with "keyword"
}
```

Cheap. Catches "core DLL won't load" / "grammar dir empty" *before*
`visual-test.sh` even tries.

### 6. JSON writer determinism unit test

In `tests/plugin_colorizer/`:

```
TEST_CASE("token JSON writer is deterministic") {
    // synthetic ColorizeResult with two adjacent tokens of identical style
    // assert: collapsed into one in output
    // assert: sorted (line, col, -len)
    // assert: mods alphabetized
    // run twice → identical bytes
}
```

This is the load-bearing assertion. If the writer isn't deterministic,
every CI run produces churn and the suite is useless.

## Scope coverage

- **Token snapshots:** all 26 grammar samples in dark theme
  (`<sample>_tokens.dark.golden.json` × 26) + `sample.py` repeated
  under light theme (`sample.py_tokens.light.golden.json` × 1) →
  27 JSON goldens total.
- **Pixel smokes** in `test_data/colorizer_smokes/` (5 files, all
  dark-themed, each 10–30 lines, 800px wide):
  - `smoke_modifiers.cpp` — exercises bold / italic / underline /
    strikethrough across visible tokens.
  - `smoke_whitespace_indent_guides.py` — tabs, trailing spaces, deep
    indent.
  - `smoke_line_numbers_wrap.cpp` — long lines forcing wrap, gutter
    math.
  - `smoke_dark.cpp` — simple cpp; catches "render engine drew on
    wrong-colored background" type regressions.
  - `smoke_unreal_cpp_variant.cpp` — minimal `UCLASS` / `UFUNCTION`
    snippet that highlights *only* under the unreal-cpp grammar; uses a
    `.flags` sidecar (`--cpp-grammar unreal`).

## Regen flow

```
bun run update-goldens             # all goldens (markdown + colorizer)
bun run update-goldens -- sample.cpp     # one golden, picked by name
```

Script branches on the directory the file lives in:

- `cases/*.md` → existing Playwright + Chrome path (unchanged).
- `grammar_samples/*` → new colorizer path:
  - run `screenshot_tool <sample> --dump-tokens --dark`
  - copy `<sample>_tokens.dark.json` → `<sample>_tokens.dark.golden.json`
  - if `<sample>` is the configured light-theme spot-check (default
    `sample.py`), additionally run
    `screenshot_tool <sample> --dump-tokens` (no `--dark`) and copy
    `<sample>_tokens.light.json` → `<sample>_tokens.light.golden.json`.
- `colorizer_smokes/*` → run `screenshot_tool <smoke> --full
  [.flags args]`, copy `*.png` → `*_golden.png`.

Final stdout matches the existing markdown-side message shape:
`git add test_data/grammar_samples/*_tokens.*.golden.json
       test_data/colorizer_smokes/*_golden.png`.

## Failure modes

| Failure | Where caught | What the dev sees |
|---|---|---|
| Grammar DLL missing for a sample's extension | `screenshot_tool --dump-tokens` exits nonzero | `ERROR: no grammar registered for "py" — check grammars/python/`. Stage continues to next sample; stage exits 1 at end. |
| Sample renders fine but token JSON differs | `compare_tokens.py` | First-divergence message + path to `_tokens_diff.txt`. Stage exits 1. |
| Token JSON identical but pixel smoke differs | stage 3 `compare.py` | Existing `_diff.png` side-by-side, similarity %. |
| Theme change → mass token drift | `compare_tokens.py` `config_hash` check | One line per affected sample: `config_hash drift … — regenerate goldens`. *No* per-token diffs spewed. Stage exits 2. |
| Tree-sitter query change → real semantic regression | `compare_tokens.py` per-token diff | Per-sample first divergence; reviewer reads `_tokens_diff.txt`. Stage exits 1. |
| Render-engine regression that doesn't change tokens | stage 3 pixel smokes only | Pixel diff PNG. Token snapshots stay green (correct — colorizer tokens are right; *paint* is wrong). |
| New grammar added without a sample | not caught (deliberate) | YAGNI; sample coverage grows by hand when new grammars are added. |
| Sample file added without a golden | `compare_tokens.py` | `WARN sample.foo (no golden — run "bun run update-goldens -- sample.foo")`. Does not fail. |
| `wlx-listerine-core.dll` not staged next to `screenshot_tool` | tool fails to load core | Existing screenshot_tool error path. |
| Regen-only commit (only `*_tokens.*.golden.json` or `*_golden.png` files staged) | hook | Hook does **not** fire — golden files aren't in the trigger globs. Commits land without running the suite. (Intentional: re-running the suite against goldens you just regenerated is tautological.) |

`visual-test.sh` accumulates failures across stages and exits non-zero
at the end if any stage failed — drops the current `set -e` short-circuit
between stages so one push surfaces all failures. (Within a stage,
fail-fast on tool errors stays as it is.)

## Determinism / stability hazards

Three to land in implementation, not as bugs later:

1. **Tree-sitter query order** — query matches can fire in any order
   on overlapping spans. Token writer enforces sort by
   `(line, col, -len)` and adjacent-collapse so two builds produce
   identical JSON.
2. **Theme palette resolution** — colors in JSON are resolved hex
   strings. Palette renames don't churn goldens; only real color
   changes do.
3. **Sub-pixel float drift in pixel smokes** — D2D float coordinates
   can drift by 1px on different DPI. Pixel smokes are 800px-wide
   fixed at one DPI. `compare.py`'s 32-per-channel tolerance plus
   99.5% similarity already absorbs AA noise within that.

## Testing strategy

### What this suite catches

See "Failure modes" table above.

### What it does not catch (acknowledged gaps)

- New grammars without samples (deliberate).
- Cross-DPI / cross-font drift (deliberate).
- Light-theme grammar coverage beyond `sample.py` (deliberate; scoped
  in §"Scope coverage").
- Markdown-fenced-code highlighting (already exercised by stage 1).
- Editor-runtime concerns (scroll, hit-test, selection — not paint).

### Tests for the suite itself

`test_data/test_compare_tokens.py` — six unit tests against synthetic
JSON, runnable via `uv run --with pytest pytest test_data/`:

| Case | Expected |
|---|---|
| Identical files | PASS |
| One token's `color` differs | FAIL at correct `(line, col)`, message includes both hex strings |
| `token_count` mismatch (extra token in middle) | FAIL with delta + first divergent index |
| `config_hash` mismatch | exit code 2, no per-token diff |
| Empty `tokens` array on both sides | PASS |
| Goldens missing entirely | WARN, exit 0 |

Plus the two C++ tests in `colorizer-tests` (§Components 5 & 6).

### Bring-up validation (one-time, before merging)

The goldens are produced by us, so they encode "correct = whatever the
code does today". To make that not self-fulfilling:

1. Spot-check 3 samples manually — open `sample.cpp`, `sample.py`,
   `sample.rs` in `wlx-listerine-colorizer` running in TC and confirm
   keyword/string/comment scopes look right against the rendered
   output.
2. Verify the unreal-cpp variant smoke catches its variant — render
   `smoke_unreal_cpp_variant.cpp` *with the standard* grammar, confirm
   the output differs from the unreal-grammar golden. Proves the smoke
   isn't a tautology.
3. Verify the suite catches a planted regression — temporarily swap
   `keyword` ↔ `string` in the dark theme, run the suite, confirm
   `compare_tokens.py` flags many samples (not 0, not 1).

Steps 1–3 go in the first PR's description as "validation performed",
then disappear.

### What gates merge

- All 28 markdown cases PASS (existing).
- All 27 colorizer token snapshots PASS.
- All 5 colorizer pixel smokes PASS.
- Doctest smoke PASS.
- JSON-writer determinism doctest PASS.
- `compare_tokens.py` self-tests PASS.

Pre-commit hook runs the same gate.

## Out of scope (future work, not in this spec)

- Token snapshots for the unreal-cpp variant (currently only smoke-tested).
- Light-theme token snapshots beyond `sample.py`.
- Token snapshots for markdown-fenced-code highlighting.
- Cross-DPI pixel snapshots.
- Performance regression checks (separate concern; `screenshot_tool --bench`
  already exists for ad-hoc use).
