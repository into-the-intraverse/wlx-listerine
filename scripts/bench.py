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
