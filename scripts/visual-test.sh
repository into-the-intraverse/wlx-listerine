#!/usr/bin/env bash
set -uo pipefail   # NB: no -e; we accumulate stage failures across stages 1-3

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CASES_DIR="$ROOT_DIR/test_data/cases"
SAMPLES_DIR="$ROOT_DIR/test_data/grammar_samples"
SMOKES_DIR="$ROOT_DIR/test_data/colorizer_smokes"
SCREENSHOT_TOOL="$ROOT_DIR/build/Release/screenshot_tool.exe"

if [[ ! -f "$SCREENSHOT_TOOL" ]]; then
    echo "ERROR: screenshot_tool.exe not found."
    echo "Build first: cmake --build --preset conan-release"
    exit 1
fi
if ! command -v uv &> /dev/null; then
    echo "ERROR: uv not found. Install: https://docs.astral.sh/uv/"
    exit 1
fi

# Track stage statuses so we surface every failure in one run.
stage1_rc=0
stage2_rc=0
stage3_rc=0

# ===== Stage 1: Markdown cases =====
echo "=== Stage 1: Markdown cases ==="
gen_fail=0
gen_ok=0
for md_file in "$CASES_DIR"/*.md; do
    name="$(basename "$md_file" .md)"
    flags_file="$CASES_DIR/${name}.flags"
    extra_args=()
    if [[ -f "$flags_file" ]]; then
        # shellcheck disable=SC2207
        extra_args=($(tr -d '\r' < "$flags_file"))
    fi
    if "$SCREENSHOT_TOOL" "$md_file" --full "${extra_args[@]}" > /dev/null 2>&1; then
        gen_ok=$((gen_ok + 1))
    else
        echo "  ERR  $name"
        gen_fail=$((gen_fail + 1))
    fi
done
echo "  Generated: $gen_ok OK, $gen_fail errors"
if [[ $gen_fail -gt 0 ]]; then
    echo "  Stage 1 generation: FAIL"
    stage1_rc=1
else
    echo ""
    uv run --with Pillow python "$ROOT_DIR/test_data/compare.py"
    stage1_rc=$?
fi

# ===== Stage 2: Colorizer token snapshots =====
echo ""
echo "=== Stage 2: Colorizer token snapshots ==="
if [[ ! -d "$SAMPLES_DIR" ]]; then
    echo "  SKIP  $SAMPLES_DIR not present"
else
    s2_ok=0
    s2_fail=0
    for sample in "$SAMPLES_DIR"/sample.*; do
        # Skip the just-produced and golden token JSON, the PNGs, and the diff
        # files. The pattern *_tokens.*.json catches both *_tokens.dark.json
        # and *_tokens.dark.golden.json without colliding with sample.json
        # (a real source file whose grammar is "json").
        case "$sample" in
            *_tokens.*.json|*.png|*_diff.txt) continue ;;
        esac
        name="$(basename "$sample")"
        if "$SCREENSHOT_TOOL" "$sample" --colorizer --dump-tokens --dark > /dev/null 2>&1; then
            s2_ok=$((s2_ok + 1))
        else
            echo "  ERR  $name (--dump-tokens failed)"
            s2_fail=$((s2_fail + 1))
        fi
        # Light-theme spot-check (mirror update-goldens.ts: only sample.py)
        if [[ "$name" == "sample.py" ]]; then
            if ! "$SCREENSHOT_TOOL" "$sample" --colorizer --dump-tokens > /dev/null 2>&1; then
                echo "  ERR  $name (--dump-tokens light failed)"
                s2_fail=$((s2_fail + 1))
            fi
        fi
    done
    echo "  Generated: $s2_ok OK, $s2_fail errors"
    if [[ $s2_fail -gt 0 ]]; then
        stage2_rc=1
    fi
    echo ""
    uv run --with Pillow python "$ROOT_DIR/test_data/compare_tokens.py" \
        --samples-dir "$SAMPLES_DIR"
    s2_compare_rc=$?
    if [[ $stage2_rc -eq 0 ]]; then
        stage2_rc=$s2_compare_rc
    fi
fi

# ===== Stage 3: Colorizer pixel smokes =====
echo ""
echo "=== Stage 3: Colorizer pixel smokes ==="
if [[ ! -d "$SMOKES_DIR" ]]; then
    echo "  SKIP  $SMOKES_DIR not present"
else
    s3_ok=0
    s3_fail=0
    for smoke in "$SMOKES_DIR"/*; do
        case "$smoke" in
            *.flags|*.png|*_diff.txt) continue ;;
        esac
        name="$(basename "$smoke")"
        flags_file="$SMOKES_DIR/${name}.flags"
        extra_args=("--full" "--dark")
        if [[ -f "$flags_file" ]]; then
            # shellcheck disable=SC2207
            extra_args=($(tr -d '\r' < "$flags_file"))
        fi
        if "$SCREENSHOT_TOOL" "$smoke" "${extra_args[@]}" > /dev/null 2>&1; then
            s3_ok=$((s3_ok + 1))
        else
            echo "  ERR  $name"
            s3_fail=$((s3_fail + 1))
        fi
    done
    echo "  Generated: $s3_ok OK, $s3_fail errors"
    if [[ $s3_fail -gt 0 ]]; then
        stage3_rc=1
    fi
    echo ""
    # Smokes write <name>_dark.png (since all smokes use --dark); rename to
    # <name>.png so compare.py picks them up against <name>_golden.png.
    # Symmetric with the markdown side's <stem>.png convention.
    for png in "$SMOKES_DIR"/*_dark.png; do
        [[ -f "$png" ]] || continue
        target="${png%_dark.png}.png"
        cp "$png" "$target"
    done
    uv run --with Pillow python "$ROOT_DIR/test_data/compare.py" --subdir colorizer_smokes
    s3_compare_rc=$?
    if [[ $stage3_rc -eq 0 ]]; then
        stage3_rc=$s3_compare_rc
    fi
fi

# ===== Summary =====
echo ""
echo "=== Summary ==="
[[ $stage1_rc -eq 0 ]] && echo "  Stage 1 (markdown):       PASS" || echo "  Stage 1 (markdown):       FAIL ($stage1_rc)"
[[ $stage2_rc -eq 0 ]] && echo "  Stage 2 (colorizer tok):  PASS" || echo "  Stage 2 (colorizer tok):  FAIL ($stage2_rc)"
[[ $stage3_rc -eq 0 ]] && echo "  Stage 3 (colorizer pix):  PASS" || echo "  Stage 3 (colorizer pix):  FAIL ($stage3_rc)"

if [[ $stage1_rc -ne 0 || $stage2_rc -ne 0 || $stage3_rc -ne 0 ]]; then
    exit 1
fi
exit 0
