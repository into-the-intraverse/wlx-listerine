#pragma once

#include "plugin_colorizer/layout/colorizer_layout.h"
#include "plugin_colorizer/layout/grid_geometry.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/text_position.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wlx::plugin_colorizer::layout {

// Colors for a byte range: slices the whole-file SpanTable once it is filled;
// before then (two-phase plain text / unsupported language) it returns an empty
// result.
// Returned spans carry ABSOLUTE raw_utf8 byte offsets (never window-relative).
using ColorsForRange =
    std::function<wlx::core::colorizer::ColorizeResult(uint32_t lo, uint32_t hi)>;

// Build the implicit-grid skeleton for `raw_utf8`: NO per-line blocks, no
// per-line text decode — just line_byte_starts (byte scan), line_tops
// (arithmetic), total_height, gutter, grid_line_count, and the build context
// future slide_grid_window calls need. Handles both modes: no-wrap (uniform row
// grid) and word-wrap (row_starts estimated from a byte scan, corrected to
// measured values by slide_grid_window).
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
//
// Wrap mode: entering lines are measured by create_line_layout; if measured
// row counts differ from geo.row_starts estimates, the corrections are folded
// into geo (suffix update), doc.line_tops / doc.total_height are rewritten,
// and the window is rebuilt ONCE against the corrected geometry (a rebuilt
// window cannot mismatch — measured values ARE the geometry). Steady-state
// slides (all estimates already corrected) skip all of this.
void slide_grid_window(wlx::runtime::layout::LayoutDocument& doc,
                       GridGeometry& geo,
                       MaterializeCtx& ctx,
                       const std::string& raw_utf8,
                       const std::vector<int>& line_byte_starts,
                       int first, int last,
                       const ColorsForRange& colors_for);

// Selection text for a grid doc, decoded from raw bytes — works for any line
// range regardless of the materialized window. lo/hi are PUBLIC (line) indices
// with char offsets into the EXPANDED (tab-expanded) line text, exactly the
// offsets hit-testing produces. Multi-line output joins with L'\n'.
std::wstring extract_selected_text_grid(const std::string& raw_utf8,
                                        const std::vector<int>& line_byte_starts,
                                        int tab_width,
                                        wlx::runtime::layout::TextPosition lo,
                                        wlx::runtime::layout::TextPosition hi);

}  // namespace wlx::plugin_colorizer::layout
