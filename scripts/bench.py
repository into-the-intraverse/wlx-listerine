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
import shutil
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
     "9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6", None),
    ("sqlite3.c",
     "https://www.sqlite.org/2025/sqlite-amalgamation-3500100.zip",
     "292cdfac26469d65501e4058c7a55ae0f811da78b2ae1e5c25db2ea44ae988f9", SQLITE_ZIP_MEMBER),
]

COMMON_ARGS = ["--bench", "--width", "1000", "--height", "1200", "--dark"]

# (scenario key, input path, extra args). Keys are stable identifiers — the
# README rows and compare output match on them (startswith), don't rename
# without regenerating the baseline. First three rows = what opening a file
# actually costs (the plugins' real paths: lazy markdown layout, cached-tree
# viewport highlight). "worst case" rows = the whole-file paths the plugins
# only hit as fallbacks (word wrap / unsupported language); kept as
# regression sentinels.
SCENARIOS = [
    ("markdown",                                   BENCH_DIR / "big.md",      ["--lazy"]),
    ("C++ header json.hpp",                        FETCHED_DIR / "json.hpp",  ["--colorizer", "--cached-tree"]),
    ("C file sqlite3.c",                           FETCHED_DIR / "sqlite3.c", ["--colorizer", "--cached-tree"]),
    ("worst case: markdown full layout",           BENCH_DIR / "big.md",      []),
    ("worst case: whole-file highlight json.hpp",  FETCHED_DIR / "json.hpp",  ["--colorizer"]),
    ("worst case: whole-file highlight sqlite3.c", FETCHED_DIR / "sqlite3.c", ["--colorizer"]),
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
                with urllib.request.urlopen(url, timeout=60) as resp, \
                        open(tmp, "wb") as f:
                    shutil.copyfileobj(resp, f)
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
        try:
            proc = subprocess.run(
                [str(TOOL), str(input_path), *COMMON_ARGS, *extra],
                cwd=REPO_ROOT, capture_output=True, text=True,
                encoding="utf-8", errors="replace", timeout=600)
        except subprocess.TimeoutExpired as e:
            stderr = e.stderr or ""
            if isinstance(stderr, bytes):
                stderr = stderr.decode("utf-8", errors="replace")
            print(f"FAIL {key} run {i + 1}: timeout 600s\n{stderr[-2000:]}",
                  file=sys.stderr)
            return None
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
    begin = text.find(MARK_BEGIN)
    end = text.find(MARK_END)
    if begin == -1 or end == -1 or end < begin:
        sys.exit(f"README markers {MARK_BEGIN} / {MARK_END} missing or malformed"
                 " — restore the Performance section from git history")
    begin += len(MARK_BEGIN)
    README.write_text(text[:begin] + "\n" + block + "\n" + text[end:],
                      encoding="utf-8", newline="\n")


def parse_baseline(block: str) -> dict:
    """Table rows between the markers -> {scenario key: metrics dict}."""
    def num(s: str):
        if s == "—":
            return None
        try:
            return float(s)
        except ValueError:
            sys.exit(f"unparseable baseline cell {s!r} in README — fix the"
                     " table or restore it from git history")

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
        "| Scenario | Open (ms) | Peak memory (MB) | Memory held (MB) |",
        "|----------|-----------|------------------|------------------|",
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
    print(f"\n{'scenario':<46} {'open ms':<36} {'peak MB':<32} {'held MB':<32}")
    for key, _input, _extra in SCENARIOS:
        if key not in results:
            continue
        r = results[key]
        b = baseline.get(key, {})
        print(f"{key:<46} "
              f"{fmt_compare(r['open_ms'], b.get('open_ms')):<36} "
              f"{fmt_compare(r['peak_mb'], b.get('peak_mb')):<32} "
              f"{fmt_compare(r['delta_mb'], b.get('delta_mb')):<32}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runs", type=int, default=DEFAULT_RUNS,
                    help=f"runs per scenario (default {DEFAULT_RUNS})")
    ap.add_argument("--only", default=None, metavar="SUBSTR",
                    help="run only scenarios whose key contains SUBSTR")
    ap.add_argument("--update", action="store_true",
                    help="rewrite the README baseline table")
    args = ap.parse_args()

    if args.runs < 1:
        ap.error("--runs must be >= 1")
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
