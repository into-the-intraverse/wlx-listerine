#pragma once

#include "plugin_colorizer/layout/colorizer_layout.h"
#include "plugin_colorizer/layout/grid_geometry.h"
#include "runtime/layout/layout_document.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wlx::plugin_colorizer::layout {

// Colors for a byte range: post-settle this slices the SpanTable; mid-sweep it
// calls wlx_core_highlight_range against the live tree; plain-text mode
// returns an empty result.
// Returned spans carry ABSOLUTE raw_utf8 byte offsets (never window-relative).
using ColorsForRange =
    std::function<wlx::core::colorizer::ColorizeResult(uint32_t lo, uint32_t hi)>;

// Build the implicit-grid skeleton for `raw_utf8`: NO per-line blocks, no
// per-line text decode — just line_byte_starts (byte scan), line_tops
// (arithmetic), total_height, gutter, grid_line_count, and the build context
// future slide_grid_window calls need. The eager (word-wrap) path stays in
// layout_source; this is the no-wrap replacement (consumers switch in M2.9/13).
//
// out_line_byte_starts / out_ctx / out_geo are filled for the caller to retain;
// none may be null. doc.blocks is left EMPTY (the first slide builds them).
wlx::runtime::layout::LayoutDocument layout_grid_skeleton(
    IDWriteFactory* dwrite,
    const std::string& raw_utf8,
    const wlx::runtime::theme::ThemeService& theme,
    bool dark_mode,
    float viewport_width,
    const ColorizerDisplayConfig& display,
    std::vector<int>* out_line_byte_starts,
    std::shared_ptr<MaterializeCtx>* out_ctx,
    GridGeometry* out_geo);

// Build one line's block (decode -> expand -> color_ranges -> layout ->
// decorations). spans_for_line is the window-relative output of
// distribute_spans_to_lines for this line.
wlx::runtime::layout::LayoutBlock build_grid_line(
    int line, const GridGeometry& geo, MaterializeCtx& ctx,
    const std::string& raw_utf8, const std::vector<int>& line_byte_starts,
    const std::vector<PerLineSpan>& spans_for_line);

// Slide doc.blocks (grid mode) to cover source lines [first, last] inclusive.
// Blocks already inside the new window are MOVED (their IDWriteTextLayouts
// survive untouched); entering lines are built; leaving lines are destroyed —
// this IS the colorizer's layout eviction. Colors for entering lines are
// fetched with ONE colors_for call per contiguous entering range.
//
// last < first => empty window: clears doc.blocks and sets first_block_line to
// std::max(0, first) (the {0, -1} empty-window convention from grid_geometry).
void slide_grid_window(wlx::runtime::layout::LayoutDocument& doc,
                       const GridGeometry& geo,
                       MaterializeCtx& ctx,
                       const std::string& raw_utf8,
                       const std::vector<int>& line_byte_starts,
                       int first, int last,
                       const ColorsForRange& colors_for);

}  // namespace wlx::plugin_colorizer::layout
