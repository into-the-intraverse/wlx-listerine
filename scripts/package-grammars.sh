#!/usr/bin/env bash
set -euo pipefail

# Package all complete grammar folders (DLL + highlights.scm) into grammars-all.zip
# Usage: ./scripts/package-grammars.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
GRAMMAR_DIR="$ROOT_DIR/grammars"
OUT="$ROOT_DIR/build/grammars-all.zip"

cd "$GRAMMAR_DIR"

dirs_to_include=()
for d in */; do
    lang="${d%/}"
    if ls "$lang"/tree-sitter-*.dll 1>/dev/null 2>&1 && [ -f "$lang/highlights.scm" ]; then
        dirs_to_include+=("$lang")
    fi
done

if [ ${#dirs_to_include[@]} -eq 0 ]; then
    echo "No complete grammar packages found."
    exit 1
fi

echo "Packaging ${#dirs_to_include[@]} grammars into $OUT"

cd "$ROOT_DIR"
rm -f "$OUT"

zip_args=()
for lang in "${dirs_to_include[@]}"; do
    zip_args+=("grammars/$lang/")
done

zip -r "$OUT" "${zip_args[@]}"

echo "Done: $OUT ($(du -h "$OUT" | cut -f1))"
