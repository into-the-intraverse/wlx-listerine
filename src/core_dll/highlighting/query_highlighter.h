#pragma once

#include "wlx_core/abi.h"

#include <tree_sitter/api.h>
#include "core_dll/colorizer/colorizer.h"
#include "core_dll/theme/helix_theme.h"
#include <string_view>
#include <vector>

namespace wlx::core::highlighting {


class WLX_CORE_API QueryHighlighter {
public:
    static std::vector<colorizer::ColorSpan> highlight(
        const TSTree* tree,
        const TSQuery* query,
        const theme::HelixTheme& theme,
        std::string_view source,
        uint32_t default_color = 0xD4D4D4,
        uint32_t range_start = 0,
        uint32_t range_end = 0);   // 0,0 (or end<=start) => whole document
};

}  // namespace wlx::core::highlighting
