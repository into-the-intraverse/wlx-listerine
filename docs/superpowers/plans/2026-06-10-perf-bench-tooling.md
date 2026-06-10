# Perf Bench Tooling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An in-repo `scripts/bench.py` that runs a fixed 6-scenario perf suite against `build/Release/screenshot_tool.exe`, compares medians against a baseline table stored in the README, and rewrites that table on `--update`.

**Architecture:** One stdlib-only Python script (PEP 723 header, run via `uv run scripts/bench.py`). Inputs: a committed synthetic `test_data/bench/big.md` plus two SHA-pinned downloads (`json.hpp`, `sqlite3.c`) fetched into gitignored `test_data/bench/fetched/`. The README gains a marker-delimited Performance section the script parses (compare mode, default) and rewrites (`--update`).

**Tech Stack:** Python ≥ 3.10 stdlib only (`urllib`, `zipfile`, `hashlib`, `statistics`, `subprocess`, `winreg`, `ctypes`). No test framework — verification is the spec's manual battery (spec §Verification).

**Spec:** `docs/superpowers/specs/2026-06-10-perf-bench-tooling-design.md`

**Important context for the engineer:**
- The bench tool is `build/Release/screenshot_tool.exe`; it prints timings/memory to **stderr** and writes a PNG next to the input file (side effect, gitignored).
- Tool runs MUST use `cwd=<repo root>` — `grammars/` and `config/themes/` resolve relative to cwd.
- md scenarios report `hot path` (ms) and `process delta` (+N **KB**); colorizer scenarios report `hot total` (ms), `peak workingset` (MB), `process delta` (+N.N **MB**). md has no peak metric — rendered as `—`.
- Everything is Windows-only by design (matches the plugins).

---

### Task 1: Bench inputs plumbing (big.md + .gitignore)

**Files:**
- Create: `test_data/bench/big.md` (copied from `D:\code\wlx-mm-bench\big.md`)
- Modify: `.gitignore`

- [ ] **Step 1: Copy the committed markdown input**

```powershell
New-Item -ItemType Directory -Force test_data\bench | Out-Null
Copy-Item D:\code\wlx-mm-bench\big.md test_data\bench\big.md
(Get-Item test_data\bench\big.md).Length
```

Expected output: `1059436` (the exact file benched on 2026-06-10). If `D:\code\wlx-mm-bench\big.md` is missing, STOP and ask the user — the generator was deliberately not kept; do not invent a new file.

- [ ] **Step 2: Add gitignore entries**

Append to `.gitignore` (keep existing content untouched):

```gitignore
test_data/bench/fetched/
test_data/bench/*.png
```

- [ ] **Step 3: Verify ignore rules work**

```powershell
git check-ignore -v test_data/bench/fetched/x.c test_data/bench/big_dark.png; git status --short test_data
```

Expected: both paths matched by the new rules; `git status` shows only `test_data/bench/big.md` as untracked.

- [ ] **Step 4: Commit**

```powershell
git add .gitignore test_data/bench/big.md
git commit -m "feat(bench): commit synthetic markdown bench input + ignore fetched/png artifacts"
```

---

### Task 2: bench.py — constants and input fetching

**Files:**
- Create: `scripts/bench.py`

- [ ] **Step 1: Verify the pinned URLs and compute checksums**

```powershell
$tmp = New-Item -ItemType Directory -Force "$env:TEMP\bench-pins"
Invoke-WebRequest "https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp" -OutFile "$tmp\json.hpp"
(Get-FileHash "$tmp\json.hpp" -Algorithm SHA256).Hash.ToLower()
Invoke-WebRequest "https://www.sqlite.org/2025/sqlite-amalgamation-3500100.zip" -OutFile "$tmp\sq.zip"
Expand-Archive "$tmp\sq.zip" -DestinationPath "$tmp\sqx" -Force
(Get-FileHash "$tmp\sqx\sqlite-amalgamation-3500100\sqlite3.c" -Algorithm SHA256).Hash.ToLower()
```

Expected: two lowercase 64-char hex strings — call them `<JSON_SHA>` and `<SQLITE_SHA>`; they are pasted into Step 2's code.
If the sqlite URL 404s (version rotated): open `https://www.sqlite.org/download.html`, take the current `sqlite-amalgamation-NNNNNNN.zip` URL, and use that version number consistently in the URL **and** the `SQLITE_ZIP_MEMBER` constant below.

- [ ] **Step 2: Create `scripts/bench.py`**

Replace `<JSON_SHA>` / `<SQLITE_SHA>` with the Step 1 hashes (and the sqlite version segment if it rotated):

```python
#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# ///
"""Perf bench suite: run screenshot_tool --bench scenarios, compare medians
against the README baseline table, rewrite it with --update.

Design: docs/superpowers/specs/2026-06-10-perf-bench-tooling-design.md
"""

import argparse
import ctypes
import hashlib
import re
import statistics
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from datetime import date
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TOOL = REPO_ROOT / "build" / "Release" / "screenshot_tool.exe"
BENCH_DIR = REPO_ROOT / "test_data" / "bench"
FETCHED_DIR = BENCH_DIR / "fetched"
README = REPO_ROOT / "README.md"

MARK_BEGIN = "<!-- bench:begin -->"
MARK_END = "<!-- bench:end -->"

SQLITE_ZIP_MEMBER = "sqlite-amalgamation-3500100/sqlite3.c"

# (filename, url, sha256 of the final extracted file, zip member or None)
DOWNLOADS = [
    ("json.hpp",
     "https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp",
     "<JSON_SHA>", None),
    ("sqlite3.c",
     "https://www.sqlite.org/2025/sqlite-amalgamation-3500100.zip",
     "<SQLITE_SHA>", SQLITE_ZIP_MEMBER),
]

COMMON_ARGS = ["--bench", "--width", "1000", "--height", "1200", "--dark"]

# (scenario key, input path, extra args). Keys are stable identifiers — the
# README rows and compare output match on them (startswith), don't rename.
SCENARIOS = [
    ("md eager",                   BENCH_DIR / "big.md",      []),
    ("md lazy",                    BENCH_DIR / "big.md",      ["--lazy"]),
    ("colorizer eager json.hpp",   FETCHED_DIR / "json.hpp",  ["--colorizer"]),
    ("colorizer cached json.hpp",  FETCHED_DIR / "json.hpp",  ["--colorizer", "--cached-tree"]),
    ("colorizer eager sqlite3.c",  FETCHED_DIR / "sqlite3.c", ["--colorizer"]),
    ("colorizer cached sqlite3.c", FETCHED_DIR / "sqlite3.c", ["--colorizer", "--cached-tree"]),
]

DEFAULT_RUNS = 5
FLAG_THRESHOLD_PCT = 10.0


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def ensure_inputs() -> None:
    """Download pinned inputs into fetched/ unless present and hash-matching."""
    FETCHED_DIR.mkdir(parents=True, exist_ok=True)
    for name, url, sha, member in DOWNLOADS:
        target = FETCHED_DIR / name
        if target.exists() and sha256_file(target) == sha:
            continue
        print(f"fetching {name} from {url} ...")
        with tempfile.TemporaryDirectory(dir=FETCHED_DIR) as td:
            tmp = Path(td) / "download.bin"
            try:
                urllib.request.urlretrieve(url, tmp)
            except OSError as e:
                sys.exit(f"download failed for {url}: {e}")
            if member:
                extracted = Path(td) / name
                with zipfile.ZipFile(tmp) as z, z.open(member) as src, \
                        open(extracted, "wb") as dst:
                    dst.write(src.read())
                tmp = extracted
            got = sha256_file(tmp)
            if got != sha:
                sys.exit(f"checksum mismatch for {url}:\n"
                         f"  expected {sha}\n  got      {got}")
            tmp.replace(target)


def main() -> int:
    ensure_inputs()  # temporary main — replaced in the next task
    print("inputs ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Verify fetch works and is idempotent**

```powershell
uv run scripts/bench.py
uv run scripts/bench.py
Get-ChildItem test_data\bench\fetched | Select-Object Name, Length
```

Expected: first run prints two `fetching ...` lines then `inputs ok`; second run prints only `inputs ok` (no network); listing shows `json.hpp` (~931 KB) and `sqlite3.c` (~5–6 MB). `git status` must NOT show `fetched/`.

- [ ] **Step 4: Verify the checksum-mismatch path**

```powershell
Set-Content test_data\bench\fetched\json.hpp "corrupted"
uv run scripts/bench.py
```

Expected: re-fetches `json.hpp` (hash mismatch → re-download) and ends with `inputs ok`.

- [ ] **Step 5: Commit**

```powershell
git add scripts/bench.py
git commit -m "feat(bench): bench.py skeleton — pinned input fetching with sha256 verification"
```

---

### Task 3: bench.py — scenario execution, parsing, medians

**Files:**
- Modify: `scripts/bench.py`

- [ ] **Step 1: Add run/parse code after `ensure_inputs`**

Insert between `ensure_inputs()` and `def main`:

```python
# ---- running & parsing ----------------------------------------------------
# md pipelines print "hot path"; colorizer pipelines print "hot total".
RX_OPEN = re.compile(r"^\s*hot (?:path|total)\s+([\d.]+)\s*ms", re.M)
RX_PEAK = re.compile(r"^\s*peak workingset\s*([\d.]+)\s*MB", re.M)
RX_DELTA = re.compile(r"^\s*process delta\s*([+-]?[\d.]+)\s*(KB|MB)", re.M)


def parse_bench_output(stderr: str) -> dict:
    m_open = RX_OPEN.search(stderr)
    m_delta = RX_DELTA.search(stderr)
    if not m_open or not m_delta:
        raise ValueError("expected bench labels not found in tool output")
    delta = float(m_delta.group(1))
    if m_delta.group(2) == "KB":  # md pipeline reports KB
        delta /= 1024.0
    m_peak = RX_PEAK.search(stderr)  # absent in md pipeline
    return {
        "open_ms": float(m_open.group(1)),
        "peak_mb": float(m_peak.group(1)) if m_peak else None,
        "delta_mb": delta,
    }


def run_scenario(key: str, input_path: Path, extra: list, runs: int) -> dict | None:
    """Median per metric across runs; None (after printing why) on any failure."""
    samples = []
    for i in range(runs):
        proc = subprocess.run(
            [str(TOOL), str(input_path), *COMMON_ARGS, *extra],
            cwd=REPO_ROOT, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=600)
        if proc.returncode != 0:
            print(f"FAIL {key} run {i + 1}: exit {proc.returncode}\n"
                  f"{proc.stderr[-2000:]}", file=sys.stderr)
            return None
        try:
            samples.append(parse_bench_output(proc.stderr))
        except ValueError as e:
            print(f"FAIL {key} run {i + 1}: {e}\n{proc.stderr[-2000:]}",
                  file=sys.stderr)
            return None

    def med(metric: str):
        vals = [s[metric] for s in samples if s[metric] is not None]
        return statistics.median(vals) if vals else None

    return {"open_ms": med("open_ms"), "peak_mb": med("peak_mb"),
            "delta_mb": med("delta_mb")}
```

- [ ] **Step 2: Replace the temporary `main` with an argparse one**

Replace the whole `def main` with:

```python
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runs", type=int, default=DEFAULT_RUNS,
                    help=f"runs per scenario (default {DEFAULT_RUNS})")
    ap.add_argument("--only", default=None, metavar="SUBSTR",
                    help="run only scenarios whose key contains SUBSTR")
    ap.add_argument("--update", action="store_true",
                    help="rewrite the README baseline table")
    args = ap.parse_args()

    if not TOOL.exists():
        sys.exit(f"{TOOL} not found — build first:\n"
                 "  conan install . --output-folder=build --build=missing"
                 " -s build_type=Release -s compiler.cppstd=20\n"
                 "  cmake --preset conan-default && cmake --build --preset conan-release")

    ensure_inputs()

    selected = [(k, p, e) for k, p, e in SCENARIOS
                if args.only is None or args.only in k]
    if not selected:
        sys.exit(f"--only {args.only!r} matches no scenario")

    results, failed = {}, []
    for key, input_path, extra in selected:
        print(f"running {key} ({args.runs} runs) ...")
        r = run_scenario(key, input_path, extra, args.runs)
        if r is None:
            failed.append(key)
        else:
            results[key] = r
            peak = "—" if r["peak_mb"] is None else f"{r['peak_mb']:.1f}"
            print(f"  open {r['open_ms']:.1f} ms  peak {peak} MB"
                  f"  delta {r['delta_mb']:.1f} MB")

    if failed:
        print(f"\nFAILED scenarios: {', '.join(failed)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Verify a quick scenario run end-to-end**

```powershell
uv run scripts/bench.py --runs 1 --only "md eager"
uv run scripts/bench.py --runs 1 --only "colorizer cached sqlite"
```

Expected: each prints `running <key> (1 runs) ...` then an `open/peak/delta` line with plausible numbers (md eager: open ≈ 300–360 ms, peak `—`; colorizer cached sqlite3.c: open ≈ 550–700 ms, peak ≈ 200–280 MB). Exit code 0. `--only nonsense` exits with the no-match message.

- [ ] **Step 4: Commit**

```powershell
git add scripts/bench.py
git commit -m "feat(bench): scenario execution, stderr parsing, per-metric medians"
```

---

### Task 4: README + CLAUDE.md sections

**Files:**
- Modify: `README.md` (insert before the `## 🚧 TODO` section)
- Modify: `CLAUDE.md` (insert after the `## Visual Regression Tests` section)

- [ ] **Step 1: Insert the README skeleton**

Directly above the line `## 🚧 TODO` in `README.md`, insert:

```markdown
## 📈 Performance

Developer baselines — machine-specific, only comparable on the same hardware.
Regenerate with `uv run scripts/bench.py --update` (see `scripts/bench.py`).

<!-- bench:begin -->
<!-- bench:end -->

```

- [ ] **Step 2: Insert the CLAUDE.md section**

After the `## Visual Regression Tests` section (before `## Configuration`), insert:

````markdown
## Performance Benchmarks

Machine-specific baselines live in the README "Performance" section (median of 5 runs, `scripts/bench.py`).

```bash
uv run scripts/bench.py            # run suite, print current vs README baseline
uv run scripts/bench.py --update   # re-measure and rewrite the README baseline
```

First run downloads pinned inputs into `test_data/bench/fetched/`. Requires a Release build of `screenshot_tool.exe`. Run on an idle machine; numbers are only comparable on the same hardware.
````

- [ ] **Step 3: Verify markers and rendering**

```powershell
Select-String -Path README.md -Pattern "bench:begin|bench:end|## .. Performance"
Select-String -Path CLAUDE.md -Pattern "Performance Benchmarks"
```

Expected: one hit each for begin/end markers and both headings.

- [ ] **Step 4: Commit**

```powershell
git add README.md CLAUDE.md
git commit -m "docs: performance baseline section (README) + bench how-to (CLAUDE.md)"
```

---

### Task 5: bench.py — README block I/O, machine info, compare, --update

**Files:**
- Modify: `scripts/bench.py`

- [ ] **Step 1: Add README-block and machine-info code after `run_scenario`**

Insert between `run_scenario` and `def main`:

```python
# ---- README baseline block -------------------------------------------------

def fmt_open(v) -> str:
    return "—" if v is None else f"{v:.0f}"


def fmt_mb(v) -> str:
    return "—" if v is None else f"{v:.1f}"


def scenario_label(key: str, input_path: Path) -> str:
    size_mb = input_path.stat().st_size / (1024 * 1024)
    return f"{key} ({size_mb:.1f} MB)"


def read_readme_block() -> str:
    text = README.read_text(encoding="utf-8")
    begin = text.find(MARK_BEGIN)
    end = text.find(MARK_END)
    if begin == -1 or end == -1 or end < begin:
        sys.exit(f"README markers {MARK_BEGIN} / {MARK_END} missing or malformed"
                 " — restore the Performance section from git history")
    return text[begin + len(MARK_BEGIN):end]


def write_readme_block(block: str) -> None:
    text = README.read_text(encoding="utf-8")
    begin = text.find(MARK_BEGIN) + len(MARK_BEGIN)
    end = text.find(MARK_END)
    README.write_text(text[:begin] + "\n" + block + "\n" + text[end:],
                      encoding="utf-8")


def parse_baseline(block: str) -> dict:
    """Table rows between the markers -> {scenario key: metrics dict}."""
    def num(s: str):
        return None if s == "—" else float(s)

    baseline = {}
    for line in block.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) != 4 or cells[0] in ("Scenario", "") or set(cells[0]) <= {"-"}:
            continue
        for key, _input, _extra in SCENARIOS:
            if cells[0].startswith(key):
                baseline[key] = {"open_ms": num(cells[1]),
                                 "peak_mb": num(cells[2]),
                                 "delta_mb": num(cells[3])}
    return baseline


def render_block(results: dict, runs: int) -> str:
    lines = [
        f"Measured on: {cpu_name()}, {ram_gb()} GB RAM, {os_name()}",
        f"Baseline: commit `{git_commit()}`, {date.today().isoformat()},"
        f" median of {runs} runs (`scripts/bench.py`)",
        "",
        "| Scenario | Open (ms) | Peak WS (MB) | Δ WS (MB) |",
        "|----------|-----------|--------------|-----------|",
    ]
    for key, input_path, _extra in SCENARIOS:
        r = results[key]
        lines.append(f"| {scenario_label(key, input_path)} | {fmt_open(r['open_ms'])}"
                     f" | {fmt_mb(r['peak_mb'])} | {fmt_mb(r['delta_mb'])} |")
    return "\n".join(lines)


# ---- machine info ----------------------------------------------------------

def cpu_name() -> str:
    import winreg
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                        r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as k:
        return winreg.QueryValueEx(k, "ProcessorNameString")[0].strip()


def ram_gb() -> int:
    class MemoryStatusEx(ctypes.Structure):
        _fields_ = [
            ("dwLength", ctypes.c_uint32), ("dwMemoryLoad", ctypes.c_uint32),
            ("ullTotalPhys", ctypes.c_uint64), ("ullAvailPhys", ctypes.c_uint64),
            ("ullTotalPageFile", ctypes.c_uint64),
            ("ullAvailPageFile", ctypes.c_uint64),
            ("ullTotalVirtual", ctypes.c_uint64),
            ("ullAvailVirtual", ctypes.c_uint64),
            ("ullAvailExtendedVirtual", ctypes.c_uint64),
        ]
    st = MemoryStatusEx()
    st.dwLength = ctypes.sizeof(st)
    ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(st))
    return round(st.ullTotalPhys / (1 << 30))


def os_name() -> str:
    import platform
    return f"Windows build {platform.version()}"


def git_commit() -> str:
    short = subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                           cwd=REPO_ROOT, capture_output=True, text=True,
                           check=True).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain"],
                           cwd=REPO_ROOT, capture_output=True, text=True,
                           check=True).stdout.strip()
    return short + ("-dirty" if dirty else "")


# ---- compare ----------------------------------------------------------------

def fmt_compare(cur, base) -> str:
    if cur is None:
        return "—"
    if base is None:
        return f"{cur:.1f} (no baseline)"
    pct = (cur - base) / base * 100.0 if base else 0.0
    flag = "  <<<" if abs(pct) > FLAG_THRESHOLD_PCT else ""
    return f"{cur:.1f} vs {base:.1f} ({pct:+.1f}%){flag}"


def print_compare(results: dict, baseline: dict) -> None:
    print(f"\n{'scenario':<30} {'open ms':<36} {'peak MB':<32} {'Δ MB':<32}")
    for key, _input, _extra in SCENARIOS:
        if key not in results:
            continue
        r = results[key]
        b = baseline.get(key, {})
        print(f"{key:<30} "
              f"{fmt_compare(r['open_ms'], b.get('open_ms')):<36} "
              f"{fmt_compare(r['peak_mb'], b.get('peak_mb')):<32} "
              f"{fmt_compare(r['delta_mb'], b.get('delta_mb')):<32}")
```

- [ ] **Step 2: Replace `main` with the final version**

Replace the whole `def main` with:

```python
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runs", type=int, default=DEFAULT_RUNS,
                    help=f"runs per scenario (default {DEFAULT_RUNS})")
    ap.add_argument("--only", default=None, metavar="SUBSTR",
                    help="run only scenarios whose key contains SUBSTR")
    ap.add_argument("--update", action="store_true",
                    help="rewrite the README baseline table")
    args = ap.parse_args()

    if args.update and (args.only is not None or args.runs != DEFAULT_RUNS):
        sys.exit("--update requires the full default suite"
                 " (no --only, default --runs)")
    if not TOOL.exists():
        sys.exit(f"{TOOL} not found — build first:\n"
                 "  conan install . --output-folder=build --build=missing"
                 " -s build_type=Release -s compiler.cppstd=20\n"
                 "  cmake --preset conan-default && cmake --build --preset conan-release")

    baseline = parse_baseline(read_readme_block())  # validates markers up front
    ensure_inputs()

    selected = [(k, p, e) for k, p, e in SCENARIOS
                if args.only is None or args.only in k]
    if not selected:
        sys.exit(f"--only {args.only!r} matches no scenario")

    results, failed = {}, []
    for key, input_path, extra in selected:
        print(f"running {key} ({args.runs} runs) ...")
        r = run_scenario(key, input_path, extra, args.runs)
        if r is None:
            failed.append(key)
        else:
            results[key] = r

    print_compare(results, baseline)

    if failed:
        print(f"\nFAILED scenarios: {', '.join(failed)}", file=sys.stderr)
        return 1
    if args.update:
        write_readme_block(render_block(results, args.runs))
        print(f"\nREADME baseline updated ({README})")
    elif not baseline:
        print("\nno baseline in README yet — run with --update to record one")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Verify guards and the no-baseline path**

```powershell
uv run scripts/bench.py --update --only sqlite        # must refuse
uv run scripts/bench.py --update --runs 2             # must refuse
uv run scripts/bench.py --runs 1 --only "md lazy"     # runs, compares
```

Expected: first two exit immediately with the `--update requires the full default suite` message (no benches run). Third prints the compare table with `(no baseline)` cells and the `no baseline in README yet` hint; exit 0.

- [ ] **Step 4: Commit**

```powershell
git add scripts/bench.py
git commit -m "feat(bench): README baseline I/O, machine info, compare mode, --update"
```

---

### Task 6: Record the first real baseline

**Files:**
- Modify: `README.md` (generated table between markers)

- [ ] **Step 1: Full 5-run suite with --update (idle machine, ~5 min)**

```powershell
uv run scripts/bench.py --update
```

Expected: six `running ...` lines, a compare table (all `(no baseline)`), then `README baseline updated`. Exit 0.

- [ ] **Step 2: Sanity-check the generated block**

```powershell
git diff README.md
```

Expected: the marker block now holds 2 preamble lines (real CPU/RAM/OS; commit hash without `-dirty`) and a 6-row table. Cross-check magnitudes against the 2026-06-10 session (auto-memory `project_perf_baselines`): md eager open ≈ 340 ms, md lazy ≈ 170 ms, colorizer cached sqlite3.c ≈ 600 ms, colorizer eager json.hpp ≈ 7400 ms, peaks within ±15% of 62/242/53/198 MB. Numbers wildly off (>2×) → investigate before committing (loaded machine? debug build?).

- [ ] **Step 3: Commit**

```powershell
git add README.md
git commit -m "docs(readme): record initial perf baseline (median of 5)"
```

---

### Task 7: Verification battery (spec §Verification)

**Files:** none modified permanently.

- [ ] **Step 1: Compare mode against the fresh baseline**

```powershell
uv run scripts/bench.py --runs 1 --only cached
```

Expected: two `colorizer cached ...` rows comparing against the README numbers, deltas mostly within ±10% (single runs are noisier than medians), no crash, exit 0.

- [ ] **Step 2: Tamper test — compare must flag a fake regression**

```powershell
# Manually edit README.md: in the "colorizer cached sqlite3.c" row, halve the Open (ms) value.
uv run scripts/bench.py --runs 1 --only "cached sqlite"
git checkout -- README.md
```

Expected: the open-ms cell shows roughly `+100%` and the `<<<` flag.

- [ ] **Step 3: --update idempotency (second full run, ~5 min)**

```powershell
uv run scripts/bench.py --update
git diff --stat README.md
git checkout -- README.md
```

Expected: diff touches only README.md and only number cells + the date/commit preamble line — structure, labels, and machine lines identical. Discard (the Task 6 baseline stays canonical).

- [ ] **Step 4: Missing-markers error**

```powershell
# Manually edit README.md: delete the "<!-- bench:end -->" line.
uv run scripts/bench.py --runs 1 --only "md eager"
git checkout -- README.md
```

Expected: immediate exit (before any benching) with the restore-from-git-history message.

- [ ] **Step 5: Final state check**

```powershell
git status --short
git log --oneline -6
```

Expected: clean tree; the log shows the five commits from Tasks 1–6.
