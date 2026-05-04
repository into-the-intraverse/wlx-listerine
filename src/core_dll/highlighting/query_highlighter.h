#pragma once

#include "wlx_core/abi.h"

#include <tree_sitter/api.h>
#include "core_dll/colorizer/colorizer.h"
#include "core_dll/theme/helix_theme.h"
#include <vector>

class WLX_CORE_API QueryHighlighter {
public:
    static std::vector<ColorSpan> highlight(
        const TSTree* tree,
        const TSQuery* query,
        const HelixTheme& theme,
        const std::string& source,
        uint32_t default_color = 0xD4D4D4);
};
