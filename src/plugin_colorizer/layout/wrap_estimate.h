#pragma once

#include <string>
#include <vector>

namespace wlx::plugin_colorizer::layout {

// Estimate the wrapped visual row count for every source line — byte-only (no
// UTF-8 decode, no DWrite): columns = code points, with tabs advancing to the
// next tab stop; rows = max(1, ceil(columns / cols_per_row)). Estimates may
// differ from DWrite's word-boundary wrapping (and wide CJK glyphs); they are
// corrected to measured values when slide_grid_window materializes a line.
// cols_per_row <= 0 degenerates to one row per line.
std::vector<int> estimate_wrap_rows(const std::string& raw_utf8,
                                    const std::vector<int>& line_byte_starts,
                                    int tab_width, int cols_per_row);

// row_starts[i] = sum of rows for lines [0, i); size = rows.size() + 1,
// row_starts[0] = 0. The GridGeometry::row_starts builder.
std::vector<int> build_row_starts(const std::vector<int>& rows);

}  // namespace wlx::plugin_colorizer::layout
