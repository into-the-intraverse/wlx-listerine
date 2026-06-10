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
