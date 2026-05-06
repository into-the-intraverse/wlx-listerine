"""Compare screenshot_tool --dump-tokens output vs golden token JSON.

Usage: python compare_tokens.py [--samples-dir <path>] [filter]

Walks <samples-dir> for *_tokens.<theme>.golden.json. For each, loads the
sibling *_tokens.<theme>.json (just produced) and diffs.

Exit codes:
  0 — all PASS
  1 — at least one FAIL (token diff or token_count mismatch)
  2 — at least one config_hash mismatch, OR no goldens found at all
"""
import argparse
import json
import re
import sys
from pathlib import Path

DEFAULT_SAMPLES = Path(__file__).parent / "grammar_samples"
GOLDEN_RE = re.compile(r"^(?P<stem>.+)_tokens\.(?P<theme>dark|light)\.golden\.json$")
CONTEXT = 10  # tokens of context above/below divergence in the diff file


def load_json(path: Path):
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def first_divergent_index(ours_tokens, golden_tokens):
    n = min(len(ours_tokens), len(golden_tokens))
    for i in range(n):
        if ours_tokens[i] != golden_tokens[i]:
            return i
    if len(ours_tokens) != len(golden_tokens):
        return n
    return None


def write_diff_file(path: Path, idx, ours, golden):
    lo = max(0, idx - CONTEXT)
    hi_o = min(len(ours["tokens"]), idx + CONTEXT + 1)
    hi_g = min(len(golden["tokens"]), idx + CONTEXT + 1)
    lines = [f"# Diff at token index {idx}", "",
             f"# Ours ({ours['source']} / {ours['theme']})", ""]
    for i in range(lo, hi_o):
        marker = ">>>" if i == idx else "   "
        lines.append(f"{marker} [{i:5d}] {json.dumps(ours['tokens'][i])}")
    lines += ["", f"# Golden ({golden['source']} / {golden['theme']})", ""]
    for i in range(lo, hi_g):
        marker = ">>>" if i == idx else "   "
        lines.append(f"{marker} [{i:5d}] {json.dumps(golden['tokens'][i])}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def diff_one(stem, theme, samples_dir):
    """Return (status, message). status in {'PASS', 'WARN', 'FAIL', 'CONFIG'}."""
    ours_path   = samples_dir / f"{stem}_tokens.{theme}.json"
    golden_path = samples_dir / f"{stem}_tokens.{theme}.golden.json"

    if not ours_path.exists():
        return ("WARN", f"no just-produced JSON at {ours_path.name} "
                        f"(did you run screenshot_tool first?)")

    ours   = load_json(ours_path)
    golden = load_json(golden_path)

    if ours.get("config_hash") != golden.get("config_hash"):
        return ("CONFIG", f"config_hash drift in {ours_path.name} "
                          f"(got {ours.get('config_hash')}, golden {golden.get('config_hash')}) "
                          f"- regenerate goldens (bun run update-goldens)")

    if ours.get("token_count") != golden.get("token_count"):
        idx = first_divergent_index(ours.get("tokens", []), golden.get("tokens", []))
        msg = (f"token_count mismatch (got {ours.get('token_count')}, "
               f"golden {golden.get('token_count')})")
        if idx is not None:
            diff_path = samples_dir / f"{stem}_tokens.{theme}_diff.txt"
            write_diff_file(diff_path, idx, ours, golden)
            msg += f"; first divergence at index {idx}; see {diff_path.name}"
        return ("FAIL", msg)

    idx = first_divergent_index(ours["tokens"], golden["tokens"])
    if idx is None:
        return ("PASS", f"{ours['token_count']} tokens")

    diff_path = samples_dir / f"{stem}_tokens.{theme}_diff.txt"
    write_diff_file(diff_path, idx, ours, golden)
    o = ours["tokens"][idx]
    g = golden["tokens"][idx]
    return ("FAIL", f"first divergence at index {idx} "
                    f"(line {o['line']} col {o['col']}): "
                    f"expected color={g['color']} mods={g['mods']}, "
                    f"got color={o['color']} mods={o['mods']}; "
                    f"see {diff_path.name}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("filter", nargs="?", default=None,
                   help="Substring filter on sample stem (optional)")
    p.add_argument("--samples-dir", default=str(DEFAULT_SAMPLES),
                   help="Directory to walk for *_tokens.<theme>.golden.json")
    args = p.parse_args()

    samples_dir = Path(args.samples_dir)
    if not samples_dir.is_dir():
        print(f"  ERROR  samples-dir does not exist: {samples_dir}")
        sys.exit(2)

    pairs = []  # list of (stem, theme)
    for f in sorted(samples_dir.iterdir()):
        m = GOLDEN_RE.match(f.name)
        if not m:
            continue
        stem = m.group("stem")
        theme = m.group("theme")
        if args.filter and args.filter not in stem:
            continue
        pairs.append((stem, theme))

    if not pairs:
        print("  No goldens found - nothing to validate")
        sys.exit(2)

    # Also scan for just-produced JSONs that have no matching golden → emit WARN
    produced_re = re.compile(r"^(?P<stem>.+)_tokens\.(?P<theme>dark|light)\.json$")
    paired_set = {(stem, theme) for stem, theme in pairs}
    for f in sorted(samples_dir.iterdir()):
        m = produced_re.match(f.name)
        if not m:
            continue
        stem = m.group("stem")
        theme = m.group("theme")
        if (stem, theme) not in paired_set:
            if args.filter and args.filter not in stem:
                continue
            pairs.append((stem, theme))

    fail_count = 0
    config_mismatch = 0
    for stem, theme in sorted(pairs):
        golden_path = samples_dir / f"{stem}_tokens.{theme}.golden.json"
        if not golden_path.exists():
            # no golden for this produced JSON — emit WARN and continue
            print(f"  {'WARN':6s} {stem} ({theme})  no golden file — run update-goldens to register")
            continue
        status, msg = diff_one(stem, theme, samples_dir)
        print(f"  {status:6s} {stem} ({theme})  {msg}")
        if status == "FAIL":
            fail_count += 1
        elif status == "CONFIG":
            config_mismatch += 1

    if config_mismatch:
        sys.exit(2)
    if fail_count:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
