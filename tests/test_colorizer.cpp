#include <doctest/doctest.h>
#include "colorizer.h"

TEST_CASE("colorizer stub returns empty result") {
    Colorizer c(L"nonexistent", L"nonexistent");
    auto result = c.colorize("int x = 1;", "c", false);
    CHECK(result.spans.empty());
}

TEST_CASE("colorizer stub supports returns false") {
    Colorizer c(L"nonexistent", L"nonexistent");
    CHECK_FALSE(c.supports("c"));
}
