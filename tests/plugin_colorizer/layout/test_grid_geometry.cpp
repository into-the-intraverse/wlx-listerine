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
    // 320 px below scroll 0 = the edge at y=320, inside line 19's [308, 324):
    // exactly 20 lines (indices 0..19) intersect the window.
    CHECK(last == 19);
    // An edge exactly ON a line boundary floors onto that next line.
    CHECK(grid_window_lines(g, 0.0f, 160.0f, 164.0f).second == 20);  // y=324 == line 20's top
    auto [f2, l2] = grid_window_lines(g, 16.0f * 95, 160.0f, 160.0f);
    CHECK(l2 == 99);                               // clamps at the end
    CHECK(f2 < 95);
    auto [f3, l3] = grid_window_lines(GridGeometry{4.0f, 16.0f, 0}, 0, 160, 160);
    CHECK(f3 == 0);
    CHECK(l3 == -1);                               // empty doc -> empty window
}

TEST_CASE("wrapped grid: row_starts drives tops, heights, totals") {
    GridGeometry g;
    g.top_pad = 4.0f;
    g.line_height = 10.0f;
    g.line_count = 3;
    g.row_starts = {0, 1, 4, 6};  // line 0: 1 row, line 1: 3 rows, line 2: 2 rows

    CHECK(g.wrapped());
    CHECK(grid_line_rows(g, 0) == 1);
    CHECK(grid_line_rows(g, 1) == 3);
    CHECK(grid_line_rows(g, 2) == 2);
    CHECK(grid_line_top(g, 0) == doctest::Approx(4.0f));
    CHECK(grid_line_top(g, 1) == doctest::Approx(14.0f));
    CHECK(grid_line_top(g, 2) == doctest::Approx(44.0f));
    CHECK(grid_total_height(g) == doctest::Approx(4.0f + 6 * 10.0f + 4.0f));
}

TEST_CASE("wrapped grid: line_at_y maps rows back to source lines") {
    GridGeometry g;
    g.top_pad = 4.0f;
    g.line_height = 10.0f;
    g.line_count = 3;
    g.row_starts = {0, 1, 4, 6};

    CHECK(grid_line_at_y(g, 0.0f) == 0);     // above content clamps to 0
    CHECK(grid_line_at_y(g, 4.0f) == 0);
    CHECK(grid_line_at_y(g, 14.0f) == 1);    // row 1 = first row of line 1
    CHECK(grid_line_at_y(g, 33.9f) == 1);    // row 2 = still line 1
    CHECK(grid_line_at_y(g, 34.0f) == 1);    // row 3 = last row of line 1
    CHECK(grid_line_at_y(g, 43.9f) == 1);    // just before line 2
    CHECK(grid_line_at_y(g, 44.0f) == 2);
    CHECK(grid_line_at_y(g, 1000.0f) == 2);  // past end clamps to last line

    auto [first, last] = grid_window_lines(g, 14.0f, 20.0f, 0.0f);
    CHECK(first == 1);
    CHECK(last == 1);  // rows 1..3 (y 14..34) all belong to line 1

    auto [f2, l2] = grid_window_lines(g, 4.0f, 40.0f, 0.0f);
    CHECK(f2 == 0);
    CHECK(l2 == 2);  // bottom edge y=44 = row 4 = first row of line 2
}

TEST_CASE("uniform grid: empty row_starts keeps the old arithmetic") {
    GridGeometry g;
    g.top_pad = 4.0f;
    g.line_height = 10.0f;
    g.line_count = 5;

    CHECK(!g.wrapped());
    CHECK(grid_line_rows(g, 3) == 1);
    CHECK(grid_line_top(g, 3) == doctest::Approx(34.0f));
    CHECK(grid_total_height(g) == doctest::Approx(4.0f + 50.0f + 4.0f));
    CHECK(grid_line_at_y(g, 34.0f) == 3);
}
