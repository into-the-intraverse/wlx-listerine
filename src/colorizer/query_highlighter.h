#pragma once

#include <tree_sitter/api.h>
#include "colorizer.h"
#include "helix_theme.h"
#include <vector>

class QueryHighlighter {
public:
    static std::vector<ColorSpan> highlight(
        const TSTree* tree,
        const TSQuery* query,
        const HelixTheme& theme,
        const std::string& source,
        uint32_t default_color = 0xD4D4D4);
};
