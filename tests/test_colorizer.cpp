#include <doctest/doctest.h>
#include "colorizer.h"

TEST_CASE("Colorizer with no grammar dir returns empty result") {
    Colorizer c(L"nonexistent", L"nonexistent");
    auto result = c.colorize("int x = 1;", "c", false);
    CHECK(result.spans.empty());
}

TEST_CASE("Colorizer with no grammar dir supports returns false") {
    Colorizer c(L"nonexistent", L"nonexistent");
    CHECK_FALSE(c.supports("c"));
}

TEST_CASE("Colorizer available_languages with no grammar dir is empty") {
    Colorizer c(L"nonexistent", L"nonexistent");
    CHECK(c.available_languages().empty());
}
