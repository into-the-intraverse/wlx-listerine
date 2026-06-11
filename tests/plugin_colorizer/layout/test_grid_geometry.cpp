#include <doctest/doctest.h>

#include "plugin_colorizer/layout/grid_geometry.h"

using namespace wlx::plugin_colorizer::layout;

TEST_CASE("grid geometry: line tops are arithmetic and clamped") {
    GridGeometry g{/*top_pad=*/4.0f, /*line_height=*/16.0f, /*line_count=*/100};
    CHECK(grid_line_top(g, 0) == doctest::Approx(4.0f));
    CHECK(grid_line_top(g, 10) == doctest::Approx(4.0f + 160.0f));
    CHECK(grid_line_at_y(g, 0.0f) == 0);           // above top pad clamps to 0
    CHECK(grid_line_at_y(g, 4.0f + 16.0f * 3 + 0.5f) == 3);
    CHECK(grid_line_at_y(g, 1e9f) == 99);          // clamps to last line
    CHECK(grid_total_height(g) == doctest::Approx(4.0f + 1600.0f + 4.0f));
}

TEST_CASE("grid geometry: window covers viewport plus overscan, clamped") {
    GridGeometry g{4.0f, 16.0f, 100};
    auto [first, last] = grid_window_lines(g, /*scroll_y=*/0.0f,
                                           /*viewport_h=*/160.0f, /*overscan=*/160.0f);
    CHECK(first == 0);
    CHECK(last == 20);                             // 2 screens from the top, inclusive
    auto [f2, l2] = grid_window_lines(g, 16.0f * 95, 160.0f, 160.0f);
    CHECK(l2 == 99);                               // clamps at the end
    CHECK(f2 < 95);
    auto [f3, l3] = grid_window_lines(GridGeometry{4.0f, 16.0f, 0}, 0, 160, 160);
    CHECK(f3 == 0);
    CHECK(l3 == -1);                               // empty doc -> empty window
}
