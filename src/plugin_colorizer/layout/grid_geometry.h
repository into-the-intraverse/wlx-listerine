#pragma once

#include <algorithm>
#include <utility>

namespace wlx::plugin_colorizer::layout {

// Uniform no-wrap grid: line i spans
// [top_pad + i*line_height, top_pad + (i+1)*line_height). Mirrors the
// arithmetic the eager build loop used; single source of truth for grid mode.
struct GridGeometry {
    float top_pad = 4.0f;
    float line_height = 0.0f;
    int line_count = 0;
};

inline float grid_line_top(const GridGeometry& g, int line) {
    return g.top_pad + g.line_height * static_cast<float>(line);
}

inline float grid_total_height(const GridGeometry& g) {
    return g.top_pad + g.line_height * static_cast<float>(g.line_count) + 4.0f;
}

inline int grid_line_at_y(const GridGeometry& g, float y) {
    if (g.line_count <= 0 || g.line_height <= 0.0f) return 0;
    const int line = static_cast<int>((y - g.top_pad) / g.line_height);
    return std::clamp(line, 0, g.line_count - 1);
}

// Inclusive [first, last] line window for viewport+overscan; {0, -1} if empty.
// Both bounds use grid_line_at_y (floor): the line CONTAINING each window edge.
// A bottom edge exactly on a line boundary floors onto that next line, so the
// boundary case is covered without over-including a non-intersecting line.
inline std::pair<int, int> grid_window_lines(const GridGeometry& g, float scroll_y,
                                             float viewport_h, float overscan) {
    if (g.line_count <= 0) return {0, -1};
    const int first = grid_line_at_y(g, scroll_y - overscan);
    const int last = grid_line_at_y(g, scroll_y + viewport_h + overscan);
    return {first, last};
}

}  // namespace wlx::plugin_colorizer::layout
