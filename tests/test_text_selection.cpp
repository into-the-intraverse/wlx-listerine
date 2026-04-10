#include <doctest/doctest.h>
#include "layout_engine.h"

#include <algorithm>

TEST_CASE("TextPosition default is invalid") {
    TextPosition pos;
    CHECK(pos.block_index == -1);
    CHECK(pos.char_offset == 0);
    CHECK_FALSE(pos.valid());
}

TEST_CASE("TextPosition with non-negative block_index is valid") {
    TextPosition pos{0, 0};
    CHECK(pos.valid());

    TextPosition pos2{5, 10};
    CHECK(pos2.valid());
}

TEST_CASE("TextPosition with negative block_index is invalid") {
    TextPosition pos{-1, 0};
    CHECK_FALSE(pos.valid());

    TextPosition pos2{-3, 5};
    CHECK_FALSE(pos2.valid());
}

TEST_CASE("TextPosition equality") {
    TextPosition a{1, 5};
    TextPosition b{1, 5};
    TextPosition c{1, 6};
    TextPosition d{2, 5};

    CHECK(a == b);
    CHECK_FALSE(a == c);
    CHECK_FALSE(a == d);

    CHECK_FALSE(a != b);
    CHECK(a != c);
    CHECK(a != d);
}

TEST_CASE("TextPosition less-than compares block_index first") {
    TextPosition a{0, 10};
    TextPosition b{1, 0};

    CHECK(a < b);
    CHECK_FALSE(b < a);
    CHECK_FALSE(a < a);
}

TEST_CASE("TextPosition less-than compares char_offset within same block") {
    TextPosition a{2, 3};
    TextPosition b{2, 7};

    CHECK(a < b);
    CHECK_FALSE(b < a);
}

TEST_CASE("TextPosition greater-than") {
    TextPosition a{1, 5};
    TextPosition b{0, 99};

    CHECK(a > b);
    CHECK_FALSE(b > a);
    CHECK_FALSE(a > a);
}

TEST_CASE("TextPosition less-or-equal") {
    TextPosition a{1, 5};
    TextPosition b{1, 5};
    TextPosition c{1, 6};

    CHECK(a <= b);
    CHECK(a <= c);
    CHECK_FALSE(c <= a);
}

TEST_CASE("TextPosition greater-or-equal") {
    TextPosition a{1, 5};
    TextPosition b{1, 5};
    TextPosition c{1, 4};

    CHECK(a >= b);
    CHECK(a >= c);
    CHECK_FALSE(c >= a);
}

TEST_CASE("TextPosition works with std::min and std::max") {
    TextPosition a{0, 5};
    TextPosition b{2, 3};

    CHECK(std::min(a, b) == a);
    CHECK(std::max(a, b) == b);

    TextPosition c{1, 0};
    TextPosition d{1, 10};

    CHECK(std::min(c, d) == c);
    CHECK(std::max(c, d) == d);
}
