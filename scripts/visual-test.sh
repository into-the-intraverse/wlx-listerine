#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CASES_DIR="$ROOT_DIR/test_data/cases"
SCREENSHOT_TOOL="$ROOT_DIR/build/Release/screenshot_tool.exe"

# --- Pre-flight ---
if [[ ! -f "$SCREENSHOT_TOOL" ]]; then
    echo "ERROR: screenshot_tool.exe not found."
    echo "Build first: cmake --build --preset conan-release"
    exit 1
fi

if ! command -v uv &> /dev/null; then
    echo "ERROR: uv not found. Install: https://docs.astral.sh/uv/"
    exit 1
fi

# --- Generate screenshots for all cases ---
echo "=== Generating screenshots ==="
gen_fail=0
gen_ok=0
for md_file in "$CASES_DIR"/*.md; do
    name="$(basename "$md_file" .md)"
    if "$SCREENSHOT_TOOL" "$md_file" --full > /dev/null 2>&1; then
        echo "  OK   $name"
        gen_ok=$((gen_ok + 1))
    else
        echo "  ERR  $name"
        gen_fail=$((gen_fail + 1))
    fi
done

echo "  Generated: $gen_ok OK, $gen_fail errors"

if [[ $gen_fail -gt 0 ]]; then
    echo "ERROR: $gen_fail case(s) failed to render"
    exit 1
fi

# --- Compare against golden PNGs ---
echo ""
echo "=== Comparing against golden PNGs ==="
uv run --with Pillow python "$ROOT_DIR/test_data/compare.py"
# compare.py exits non-zero on FAIL — propagated by set -e
