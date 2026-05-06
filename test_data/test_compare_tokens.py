"""Self-tests for compare_tokens.py — run via:
    uv run --with pytest pytest test_data/test_compare_tokens.py -v
"""
import json
import subprocess
import sys
from pathlib import Path

import pytest

THIS_DIR = Path(__file__).parent
COMPARE = THIS_DIR / "compare_tokens.py"


def _write_pair(tmp_path, name, ours, golden, theme="dark"):
    """Write ours and golden JSON files under tmp_path/grammar_samples/."""
    samples_dir = tmp_path / "grammar_samples"
    samples_dir.mkdir(parents=True, exist_ok=True)
    src = samples_dir / name
    src.write_text("// placeholder source for tests\n", encoding="utf-8")
    (samples_dir / f"{name}_tokens.{theme}.json").write_text(json.dumps(ours), encoding="utf-8")
    (samples_dir / f"{name}_tokens.{theme}.golden.json").write_text(json.dumps(golden), encoding="utf-8")
    return samples_dir


def _run(samples_dir):
    """Invoke compare_tokens.py against samples_dir; return (rc, stdout)."""
    proc = subprocess.run(
        [sys.executable, str(COMPARE), "--samples-dir", str(samples_dir)],
        capture_output=True, text=True, check=False,
    )
    return proc.returncode, proc.stdout + proc.stderr


def _doc(tokens, *, source="sample.cpp", language="cpp", theme="default_dark", config_hash="h"):
    return {
        "source": source, "language": language, "theme": theme,
        "config_hash": config_hash, "token_count": len(tokens), "tokens": tokens
    }


def test_identical_passes(tmp_path):
    doc = _doc([{"line": 1, "col": 1, "len": 5, "color": "#FF0000", "mods": []}])
    samples = _write_pair(tmp_path, "sample.cpp", doc, doc)
    rc, out = _run(samples)
    assert rc == 0
    assert "PASS" in out


def test_color_diff_fails_at_correct_line_col(tmp_path):
    a = _doc([{"line": 7, "col": 4, "len": 5, "color": "#FF0000", "mods": []}])
    b = _doc([{"line": 7, "col": 4, "len": 5, "color": "#00FF00", "mods": []}])
    samples = _write_pair(tmp_path, "sample.cpp", a, b)
    rc, out = _run(samples)
    assert rc == 1
    assert "7:4" in out or "line 7" in out
    assert "#FF0000" in out and "#00FF00" in out


def test_token_count_mismatch_fails_with_delta(tmp_path):
    a = _doc([
        {"line": 1, "col": 1, "len": 1, "color": "#111111", "mods": []},
        {"line": 2, "col": 1, "len": 1, "color": "#222222", "mods": []},
        {"line": 3, "col": 1, "len": 1, "color": "#333333", "mods": []},
    ])
    b = _doc([
        {"line": 1, "col": 1, "len": 1, "color": "#111111", "mods": []},
        {"line": 3, "col": 1, "len": 1, "color": "#333333", "mods": []},
    ])
    samples = _write_pair(tmp_path, "sample.cpp", a, b)
    rc, out = _run(samples)
    assert rc == 1
    assert "token_count" in out.lower()


def test_config_hash_mismatch_exits_2_no_per_token_diff(tmp_path):
    a = _doc([{"line": 1, "col": 1, "len": 5, "color": "#FF0000", "mods": []}],
             config_hash="aaaa")
    b = _doc([{"line": 1, "col": 1, "len": 5, "color": "#00FF00", "mods": []}],
             config_hash="bbbb")
    samples = _write_pair(tmp_path, "sample.cpp", a, b)
    rc, out = _run(samples)
    assert rc == 2
    assert "config_hash" in out.lower()
    # Per-token diff must NOT be emitted (color difference would normally fire).
    assert "#FF0000" not in out
    assert "#00FF00" not in out


def test_empty_tokens_both_sides_passes(tmp_path):
    doc = _doc([])
    samples = _write_pair(tmp_path, "sample.cpp", doc, doc)
    rc, out = _run(samples)
    assert rc == 0
    assert "PASS" in out


def test_missing_golden_warns_does_not_fail(tmp_path):
    """A sample with a just-produced JSON but no golden warns and exits 0."""
    samples_dir = tmp_path / "grammar_samples"
    samples_dir.mkdir(parents=True)
    (samples_dir / "sample.cpp").write_text("// src\n", encoding="utf-8")
    (samples_dir / "sample.cpp_tokens.dark.json").write_text(
        json.dumps(_doc([{"line": 1, "col": 1, "len": 5, "color": "#FF0000", "mods": []}])),
        encoding="utf-8",
    )
    rc, out = _run(samples_dir)
    # No goldens at all → exit 2 ("no cases to validate"), matches compare.py behavior
    assert rc == 2

    # When *some* goldens exist, the missing-golden case should WARN. Add a paired
    # sample to test that path:
    (samples_dir / "sample.py").write_text("# src\n", encoding="utf-8")
    paired_doc = _doc([{"line": 1, "col": 1, "len": 1, "color": "#000000", "mods": []}])
    (samples_dir / "sample.py_tokens.dark.json").write_text(json.dumps(paired_doc))
    (samples_dir / "sample.py_tokens.dark.golden.json").write_text(json.dumps(paired_doc))
    rc, out = _run(samples_dir)
    assert rc == 0
    assert "WARN" in out or "SKIP" in out  # sample.cpp has no golden
    assert "PASS" in out                   # sample.py paired
