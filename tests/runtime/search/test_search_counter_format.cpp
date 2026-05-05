#include <doctest/doctest.h>
#include "runtime/search/search_counter_format.h"

using namespace wlx::runtime::search;

TEST_CASE("format_counter") {
    SUBCASE("normal match") {
        CHECK(format_counter(1, 27) == L"1 / 27");
    }
    SUBCASE("last match") {
        CHECK(format_counter(27, 27) == L"27 / 27");
    }
    SUBCASE("zero results") {
        CHECK(format_counter(0, 0) == L"0 / 0");
    }
    SUBCASE("large counts plain") {
        CHECK(format_counter(142, 1058) == L"142 / 1058");
    }
}
