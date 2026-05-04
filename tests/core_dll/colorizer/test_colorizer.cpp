#include <doctest/doctest.h>
#include <filesystem>
#include "core_dll/colorizer/colorizer.h"

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

TEST_CASE("Colorizer end-to-end with C grammar"
    * doctest::skip(!std::filesystem::exists("grammars/c/tree-sitter-c.dll"))) {
    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("c"));

    auto result = c.colorize("// comment\nint x = 1;", "c", false);
    CHECK_FALSE(result.spans.empty());

    for (size_t i = 1; i < result.spans.size(); i++) {
        CHECK(result.spans[i].start >= result.spans[i - 1].start);
    }
}

TEST_CASE("Colorizer dark mode produces different colors"
    * doctest::skip(!std::filesystem::exists("grammars/c/tree-sitter-c.dll"))) {
    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("c"));

    auto light = c.colorize("int x = 1;", "c", false);
    auto dark  = c.colorize("int x = 1;", "c", true);

    CHECK_FALSE(light.spans.empty());
    CHECK_FALSE(dark.spans.empty());
    CHECK(light.spans[0].color != dark.spans[0].color);
}

TEST_CASE("Colorizer end-to-end with JSON grammar"
    * doctest::skip(!std::filesystem::exists("grammars/json/tree-sitter-json.dll"))) {
    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("json"));

    auto result = c.colorize(R"({"key": "value", "num": 42})", "json", false);
    CHECK_FALSE(result.spans.empty());
}

TEST_CASE("Colorizer end-to-end with Python grammar"
    * doctest::skip(!std::filesystem::exists("grammars/python/tree-sitter-python.dll"))) {
    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("python"));

    auto result = c.colorize("def foo(x):\n    return x + 1\n", "python", false);
    CHECK_FALSE(result.spans.empty());
}
