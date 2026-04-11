#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GRAMMAR_BUILD_DIR="$REPO_ROOT/build/grammars"
mkdir -p "$GRAMMAR_BUILD_DIR"

declare -A GRAMMARS=(
    [c]="https://github.com/tree-sitter/tree-sitter-c"
    [json]="https://github.com/tree-sitter/tree-sitter-json"
    [python]="https://github.com/tree-sitter/tree-sitter-python"
)

for lang in "${!GRAMMARS[@]}"; do
    url="${GRAMMARS[$lang]}"
    repo_dir="$GRAMMAR_BUILD_DIR/$lang"

    if [ -d "$repo_dir/.git" ]; then
        echo "  SKIP (exists): $lang -> $repo_dir"
        continue
    fi

    echo "  Cloning $lang from $url ..."
    git clone --depth 1 "$url" "$repo_dir"
    echo "  Cloned $lang"
done

echo ""
echo "Grammar sources ready in $GRAMMAR_BUILD_DIR"
echo "Now run: cmake --preset conan-default && cmake --build --preset conan-release"
echo "Grammar DLLs will be placed in $REPO_ROOT/grammars/"
