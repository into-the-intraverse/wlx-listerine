#include <doctest/doctest.h>

#include "plugin_colorizer/layout/wrap_estimate.h"

#include <string>
#include <vector>

using namespace wlx::plugin_colorizer::layout;

TEST_CASE("estimate_wrap_rows: ceil division over display columns") {
    // 3 lines: 10 cols, 0 cols (empty), 25 cols. 10 cols per row.
    const std::string src = std::string(10, 'a') + "\n\n" + std::string(25, 'b');
    const std::vector<int> starts = {0, 11, 12};
    auto rows = estimate_wrap_rows(src, starts, 4, 10);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0] == 1);
    CHECK(rows[1] == 1);   // empty line still occupies one row
    CHECK(rows[2] == 3);   // ceil(25/10)
}

TEST_CASE("estimate_wrap_rows: tabs expand to the next tab stop") {
    // "\ta" with tab_width 4 = 5 columns; cols_per_row 4 -> 2 rows.
    const std::string src = "\ta";
    const std::vector<int> starts = {0};
    auto rows = estimate_wrap_rows(src, starts, 4, 4);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == 2);
}

TEST_CASE("estimate_wrap_rows: UTF-8 continuation bytes are not columns") {
    // U+00E9 'e-acute' is 2 bytes but 1 column; 4 of them = 4 cols -> 1 row at 4/row.
    const std::string src = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9";
    const std::vector<int> starts = {0};
    auto rows = estimate_wrap_rows(src, starts, 4, 4);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == 1);
}

TEST_CASE("estimate_wrap_rows: trailing CR excluded; degenerate width safe") {
    const std::string src = "abcd\r\nef";
    const std::vector<int> starts = {0, 6};
    auto rows = estimate_wrap_rows(src, starts, 4, 4);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == 1);   // 4 cols exactly, CR not counted
    CHECK(rows[1] == 1);
    auto degenerate = estimate_wrap_rows(src, starts, 4, 0);  // cols_per_row<=0
    CHECK(degenerate[0] == 1);
    CHECK(degenerate[1] == 1);
}

TEST_CASE("build_row_starts: prefix sums with leading zero") {
    auto rs = build_row_starts({1, 3, 2});
    REQUIRE(rs.size() == 4);
    CHECK(rs[0] == 0);
    CHECK(rs[1] == 1);
    CHECK(rs[2] == 4);
    CHECK(rs[3] == 6);
}

TEST_CASE("estimate_wrap_rows: lone mid-line CR stops counting (documented underestimate)") {
    // line_byte_starts comes from a '\n'-only scan, so "ab\rcd" is ONE line;
    // counting stops at the \r — 2 cols, not 5. Corrected at materialization.
    const std::string src = "ab\rcd\nef";
    const std::vector<int> starts = {0, 6};
    auto rows = estimate_wrap_rows(src, starts, 4, 2);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == 1);   // 2 cols at 2/row -> 1 row (would be 3 rows if \r didn't stop it)
    CHECK(rows[1] == 1);
}
