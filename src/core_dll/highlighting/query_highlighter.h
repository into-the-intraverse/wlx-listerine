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
    // Resolve each capture index of `query` to its theme style. Depends only
    // on the query's capture names and the theme, so callers may cache the
    // table across highlight calls (see Colorizer's capture-style memo) —
    // rebuilding it costs a theme resolve (with string allocs) per capture.
    static std::vector<theme::ResolvedStyle> resolve_capture_styles(
        const TSQuery* query, const theme::HelixTheme& theme);

    // Memo-friendly overload: caller supplies the resolved capture-style
    // table (typically cached via resolve_capture_styles).
    static std::vector<colorizer::ColorSpan> highlight(
        const TSTree* tree,
        const TSQuery* query,
        const std::vector<theme::ResolvedStyle>& capture_styles,
        std::string_view source,
        uint32_t range_start = 0,
        uint32_t range_end = 0);   // 0,0 (or end<=start) => whole document

    // Convenience overload: resolves the capture-style table from `theme` on
    // every call.
    static std::vector<colorizer::ColorSpan> highlight(
        const TSTree* tree,
        const TSQuery* query,
        const theme::HelixTheme& theme,
        std::string_view source,
        uint32_t range_start = 0,
        uint32_t range_end = 0);
};

}  // namespace wlx::core::highlighting
