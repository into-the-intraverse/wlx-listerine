#pragma once

#include <algorithm>
#include <utility>
#include <vector>

namespace wlx::plugin_colorizer::layout {

// Implicit grid. Every visual ROW is exactly line_height tall (uniform DWrite
// line spacing). No-wrap: every source line is one row, so Y is pure
// arithmetic. Wrap: row_starts[i] = total rows before line i (prefix sums,
// size line_count + 1), so line i spans rows [row_starts[i], row_starts[i+1]).
// Empty row_starts == uniform no-wrap grid. Single source of truth for grid Y.
struct GridGeometry {
    float top_pad = 4.0f;
    float line_height = 0.0f;
    int line_count = 0;
    std::vector<int> row_starts;  // empty => uniform (one row per line)
    bool wrapped() const { return !row_starts.empty(); }
};

inline int grid_line_rows(const GridGeometry& g, int line) {
    if (g.row_starts.empty()) return 1;
    return g.row_starts[static_cast<size_t>(line) + 1] -
           g.row_starts[static_cast<size_t>(line)];
}

inline float grid_line_top(const GridGeometry& g, int line) {
    const int row = g.row_starts.empty()
                        ? line
                        : g.row_starts[static_cast<size_t>(line)];
    return g.top_pad + g.line_height * static_cast<float>(row);
}

inline float grid_total_height(const GridGeometry& g) {
    const int rows = g.row_starts.empty()
                         ? g.line_count
                         : g.row_starts[static_cast<size_t>(g.line_count)];
    return g.top_pad + g.line_height * static_cast<float>(rows) + 4.0f;
}

inline int grid_line_at_y(const GridGeometry& g, float y) {
    if (g.line_count <= 0 || g.line_height <= 0.0f) return 0;
    const int row = static_cast<int>((y - g.top_pad) / g.line_height);
    if (g.row_starts.empty())
        return std::clamp(row, 0, g.line_count - 1);
    const int last_row = g.row_starts[static_cast<size_t>(g.line_count)] - 1;
    const int clamped = std::clamp(row, 0, std::max(0, last_row));
    // Last line i with row_starts[i] <= clamped.
    const auto it = std::upper_bound(g.row_starts.begin(), g.row_starts.end(),
                                     clamped);
    const int line = static_cast<int>(it - g.row_starts.begin()) - 1;
    return std::clamp(line, 0, g.line_count - 1);
}

// Inclusive [first, last] line window for viewport+overscan; {0, -1} if empty.
// Both bounds use grid_line_at_y (floor): the line CONTAINING each window edge.
inline std::pair<int, int> grid_window_lines(const GridGeometry& g, float scroll_y,
                                             float viewport_h, float overscan) {
    if (g.line_count <= 0) return {0, -1};
    const int first = grid_line_at_y(g, scroll_y - overscan);
    const int last = grid_line_at_y(g, scroll_y + viewport_h + overscan);
    return {first, last};
}

}  // namespace wlx::plugin_colorizer::layout
