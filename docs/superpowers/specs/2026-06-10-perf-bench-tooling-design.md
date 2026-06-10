# Perf bench tooling — design

**Date:** 2026-06-10
**Status:** approved (brainstorming session 2026-06-10)

## Goal

Make the ad-hoc performance benchmarking from the 2026-06-10 before/after session a
repeatable, in-repo tool: one script that runs a fixed suite against
`build/Release/screenshot_tool.exe`, compares the results against a baseline table
stored in the README, and updates that baseline on request. Run manually, once in a
while.

## Non-goals

- No CI integration. Bench numbers are machine-specific; shared GHA runners are too
  noisy (±20–30%) to compare against a locally measured baseline, and the cpp grammar
  currently misbehaves on the GHA windows-2025 image (see README TODO).
- No release-flow or git-hook integration. Purely manual.
- No C++ changes. The script consumes the existing `--bench` stderr output as-is.
- No before/after worktree automation (that was a one-off; the README baseline is the
  standing "before").

## Files

```
scripts/bench.py              # the tool — Python, stdlib only, PEP 723 header, no deps
test_data/bench/big.md        # committed synthetic 1.06 MB markdown (the exact file
                              # benched on 2026-06-10; baseline continuity)
test_data/bench/fetched/      # gitignored; downloaded colorizer inputs land here
README.md                     # gains a compact "Performance" section (marker-delimited)
CLAUDE.md                     # gains a short "Performance Benchmarks" how-to section
.gitignore                    # + test_data/bench/fetched/  and  test_data/bench/*.png
```

Run with `uv run scripts/bench.py` (plain `python scripts/bench.py` also works —
stdlib only, `requires-python >= 3.10`).

## Bench inputs

| input | provenance |
|---|---|
| `test_data/bench/big.md` | committed; never regenerated (generator not kept) |
| `fetched/json.hpp` | `https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp` (MIT) |
| `fetched/sqlite3.c` | sqlite.org amalgamation zip, version pinned at implementation time (latest available; public domain); extracted from the zip via stdlib `zipfile` |

Exact URLs and SHA-256 checksums for both downloads are hard-coded constants in
`bench.py`; downloads are verified against them (byte-stable inputs forever) and
written via temp-file + rename (no partial files). Downloads happen automatically on
first run; if the files exist and hash-match, no network access occurs.

The pinned downloads are NOT byte-identical to the files benched in the 2026-06-10
session, so the first `--update` establishes the canonical baseline; the session
numbers (memory: `project_perf_baselines`) serve only as a sanity cross-check.

## Suite

Six configs, run sequentially (never in parallel), cwd = repo root (grammars/ and
config/themes/ resolve relative to cwd). Common args:
`--bench --width 1000 --height 1200 --dark`.

| scenario key | input | extra args |
|---|---|---|
| `md eager` | big.md | — |
| `md lazy` | big.md | `--lazy` |
| `colorizer eager json.hpp` | json.hpp | `--colorizer` |
| `colorizer cached json.hpp` | json.hpp | `--colorizer --cached-tree` |
| `colorizer eager sqlite3.c` | sqlite3.c | `--colorizer` |
| `colorizer cached sqlite3.c` | sqlite3.c | `--colorizer --cached-tree` |

No `--lang` override: the tool follows the plugin's real routing
(`path_to_language.h`; `.c` → cpp grammar — deliberate, matches what TC users get).

Default 5 runs per config; median per metric across runs (medians are computed
per-metric, not per-run). Metrics parsed from the tool's stderr by label:

| column | md pipelines | colorizer pipelines |
|---|---|---|
| Open (ms) | `hot path` | `hot total` |
| Peak WS (MB) | — (not reported) | `peak workingset` |
| Δ WS (MB) | `process delta` (KB → MB) | `process delta` |

All parsing and formatting is locale-independent (no comma-decimal bugs). Output
rounding: ms to whole numbers, MB to one decimal.

## README section

A short dev-facing **Performance** section near the bottom of the README (above the
TODO section), fully generated between markers:

```markdown
## 📈 Performance

<!-- bench:begin -->
Measured on: <CPU name>, <N> GB RAM, <Windows version>
Baseline: commit `<short-hash>`, <YYYY-MM-DD>, median of 5 runs (`scripts/bench.py`)

| Scenario | Open (ms) | Peak WS (MB) | Δ WS (MB) |
|----------|-----------|--------------|-----------|
| md eager — big.md (1.0 MB) | 342 | — | 170.0 |
| ... 6 rows ... | | | |
<!-- bench:end -->
```

(Numbers above are illustrative; real values come from the first `--update` run.)
The implementation commits the section skeleton — heading plus empty marker pair —
so `--update` always has markers to rewrite between; the first `--update` fills it.
Machine info: CPU name from registry (`HKLM\HARDWARE\DESCRIPTION\System\
CentralProcessor\0 → ProcessorNameString` via `winreg`), RAM via `ctypes`
`GlobalMemoryStatusEx`, OS via `platform`. Commit from `git rev-parse --short HEAD`,
with a `-dirty` suffix when the working tree is not clean.

## CLI and compare flow

```
uv run scripts/bench.py [--runs N] [--only SUBSTR] [--update]
```

- **Default (compare) mode:** run the suite → parse the baseline table from the README
  markers → print a console table: scenario × metric, `current vs baseline (±N%)`,
  visually flagging deltas beyond ±10%. Informational only — deltas never affect the
  exit code (exit 0 when all scenarios ran, 1 when any scenario failed); the human
  judges.
- **`--update`:** after the run, rewrite the entire marker block (machine lines +
  table) in place. Refuses when `--only` or a non-default `--runs` is active (a
  partial or low-confidence run must not become the baseline).
- **`--only SUBSTR`:** run only scenarios whose key contains SUBSTR (quick checks);
  compare prints only those rows.
- **`--runs N`:** override the run count (default 5).

## Error handling

- `build/Release/screenshot_tool.exe` missing → exit with the build instructions
  one-liner.
- Download failure or checksum mismatch → actionable message naming the URL; the
  `fetched/` file is not left behind.
- README markers missing/malformed → error saying the block must be restored from git
  history (compare mode needs a baseline; `--update` also requires the markers — it
  rewrites between them, never inventing placement).
- Tool exits non-zero or expected labels absent from stderr → fail that scenario with
  the captured output, continue the rest, and skip `--update` if any scenario failed.
- PNG side-effects (`*_dark.png` next to inputs) are gitignored, never cleaned up by
  the script.

## CLAUDE.md addition

A short section following the existing style:

```markdown
## Performance Benchmarks

Machine-specific baselines live in README "Performance" (median of 5, `scripts/bench.py`).

​```bash
uv run scripts/bench.py            # run suite, print current vs README baseline
uv run scripts/bench.py --update   # re-measure and rewrite the README baseline
​```

First run downloads pinned inputs into `test_data/bench/fetched/`. Requires a Release
build. Run on an idle machine; numbers are only comparable on the same hardware.
```

## Verification (no new test infra)

1. `--runs 1` smoke run end-to-end (downloads, runs all 6, prints compare).
2. `--update` twice in a row → second README diff touches only numbers.
3. Hand-edit one baseline cell → compare mode flags the delta.
4. `--only sqlite --update` → refused.
5. Commit the first real 5-run baseline; cross-check magnitudes against the
   2026-06-10 session numbers (e.g. colorizer cached sqlite ≈ 600 ms, md lazy ≈ 200 ms).
