#!/usr/bin/env bash
set -euo pipefail

# Usage: ./scripts/fetch-grammars.sh [language...]
# Without args, fetches all starter grammars.
# With args, fetches only specified languages.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
GRAMMAR_OUT="$ROOT_DIR/grammars"
BUILD_DIR="$ROOT_DIR/build/grammars"

declare -A GRAMMAR_REPOS=(
    [c]="tree-sitter/tree-sitter-c"
    [cpp]="tree-sitter/tree-sitter-cpp"
    [python]="tree-sitter/tree-sitter-python"
    [javascript]="tree-sitter/tree-sitter-javascript"
    [typescript]="tree-sitter/tree-sitter-typescript"
    [rust]="tree-sitter/tree-sitter-rust"
    [go]="tree-sitter/tree-sitter-go"
    [java]="tree-sitter/tree-sitter-java"
    [c-sharp]="tree-sitter/tree-sitter-c-sharp"
    [json]="tree-sitter/tree-sitter-json"
    [html]="tree-sitter/tree-sitter-html"
    [css]="tree-sitter/tree-sitter-css"
    [bash]="tree-sitter/tree-sitter-bash"
    [toml]="tree-sitter-grammars/tree-sitter-toml"
    [yaml]="tree-sitter-grammars/tree-sitter-yaml"
    [lua]="MunifTanjim/tree-sitter-lua"
    [php]="tree-sitter/tree-sitter-php"
    [powershell]="airbus-cert/tree-sitter-powershell"
    [vim]="neovim/tree-sitter-vim"
    [dockerfile]="camdencheek/tree-sitter-dockerfile"
    [cmake]="uyha/tree-sitter-cmake"
    [markdown]="MDeiml/tree-sitter-markdown"
    [unreal-cpp]="taku25/tree-sitter-unreal-cpp"
    [gitcommit]="the-mikedavis/tree-sitter-git-commit"
    [gitconfig]="the-mikedavis/tree-sitter-git-config"
    [gitignore]="shuber/tree-sitter-gitignore"
    [gitattributes]="ObserverOfTime/tree-sitter-gitattributes"
    [git_rebase]="the-mikedavis/tree-sitter-git-rebase"
)

# Grammars where highlights.scm is in a subdirectory
declare -A QUERY_SUBDIR=(
    [vim]="queries/vim"
)

LANGS=("$@")
if [ ${#LANGS[@]} -eq 0 ]; then
    LANGS=("${!GRAMMAR_REPOS[@]}")
fi

for lang in "${LANGS[@]}"; do
    repo="${GRAMMAR_REPOS[$lang]:-}"
    if [ -z "$repo" ]; then
        echo "Unknown language: $lang"
        continue
    fi

    echo "--- Fetching $lang from $repo ---"
    clone_dir="$BUILD_DIR/$lang"

    if [ ! -d "$clone_dir" ]; then
        git clone --depth 1 "https://github.com/$repo.git" "$clone_dir"
    else
        git -C "$clone_dir" pull --ff-only 2>/dev/null || true
    fi

    # Copy highlights.scm
    query_dir="${QUERY_SUBDIR[$lang]:-queries}"
    mkdir -p "$GRAMMAR_OUT/$lang"
    if [ -f "$clone_dir/$query_dir/highlights.scm" ]; then
        cp "$clone_dir/$query_dir/highlights.scm" "$GRAMMAR_OUT/$lang/highlights.scm"
        echo "  -> highlights.scm copied"
    else
        echo "  WARNING: no highlights.scm found at $query_dir/highlights.scm"
    fi
done

echo "Done. Grammar sources are in $BUILD_DIR. Run CMake to compile DLLs."
