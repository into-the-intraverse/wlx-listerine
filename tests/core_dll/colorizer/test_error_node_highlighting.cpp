// Regression: a tree-sitter (ERROR)/(MISSING) node on a CONTAINER node (one that
// wraps validly-parsed children) must NOT paint its red error background over —
// and thereby erase — the foreground syntax coloring of everything it contains.
//
// tree-sitter is a fuzzy, preprocessor-unaware parser, so valid macro-heavy C/C++
// (e.g. sqlite3.c's SQLITE_API/SQLITE_PRIVATE declaration-prefix macros) routinely
// yields such error nodes — up to a whole-file root ERROR on large files. Before
// the guard in QueryHighlighter::highlight, that single error span swallowed every
// other color and rendered the whole file as a solid red wash.
#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include "core_dll/colorizer/colorizer.h"

using namespace wlx::core::colorizer;

namespace {

// Fraction of `src` covered by red error-background (#FF4444) spans.
double error_bg_fraction(Colorizer& c, const std::string& src, const char* lang) {
    auto r = c.colorize(src, lang, /*dark=*/true);
    size_t eb = 0;
    for (const auto& s : r.spans)
        if (s.has_bg && s.bg_color == 0xFF4444u) eb += s.length;
    return src.empty() ? 0.0 : double(eb) / double(src.size());
}

std::string repeat_to(const std::string& unit, size_t target) {
    std::string s; s.reserve(target + unit.size());
    while (s.size() < target) s += unit;
    return s;
}

const bool kHaveC = std::filesystem::exists("grammars/c/tree-sitter-c.dll");

} // namespace

// Plain valid C never produces error backgrounds, at any size (control).
TEST_CASE("highlight: valid C has no error background" * doctest::skip(!kHaveC)) {
    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("c"));
    for (size_t n : { size_t(4096), size_t(256u * 1024) })
        CHECK(error_bg_fraction(c, repeat_to("static int f(int a) { return a + 1; }\n", n), "c") == 0.0);
}

// Declaration-prefix macros (the SQLITE_API pattern) make tree-sitter wrap each
// declaration in a CONTAINER error node. Before the guard these painted the decls
// red (err_frac ~0.086, suppressing their syntax colors); the guard drops the
// container error so the code colors normally.
TEST_CASE("highlight: container error node does not wash valid code" * doctest::skip(!kHaveC)) {
    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("c"));
    std::string src = repeat_to("SQLITE_API int sqlite3_func(void);\n", 200u * 1024);
    CHECK(error_bg_fraction(c, src, "c") < 0.001);            // no red wash
    CHECK(c.colorize(src, "c", true).spans.size() > 1000);   // real foreground coloring survives
}
