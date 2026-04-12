#pragma once

#include <tree_sitter/api.h>
#include "colorizer.h"
#include "theme_loader.h"
#include <vector>

class QueryHighlighter {
public:
    static std::vector<ColorSpan> highlight(
        const TSTree* tree,
        const TSQuery* query,
        const SyntaxPalette& palette,
        const std::string& source);
};
