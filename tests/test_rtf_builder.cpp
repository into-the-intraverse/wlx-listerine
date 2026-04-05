#include <doctest/doctest.h>
#include "rtf_builder.h"

static Config cfg() { return default_config(); }

TEST_CASE("RTF output starts with rtf1 header and ends with closing brace") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("Hello", 5);
    CHECK(rtf.substr(0, 10) == "{\\rtf1\\ans");
    CHECK(rtf.back() == '}');
}

TEST_CASE("RTF contains font table with body and code fonts") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("x", 1);
    CHECK(rtf.find("\\fonttbl") != std::string::npos);
    CHECK(rtf.find("Segoe UI") != std::string::npos);
    CHECK(rtf.find("Consolas") != std::string::npos);
}

TEST_CASE("RTF contains color table") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("x", 1);
    CHECK(rtf.find("\\colortbl;") != std::string::npos);
}

TEST_CASE("dark mode uses dark color scheme") {
    RtfBuilder b(cfg(), true);
    auto rtf = b.build("x", 1);
    // Dark text color is #D4D4D4 = rgb(212,212,212)
    CHECK(rtf.find("\\red212\\green212\\blue212") != std::string::npos);
}

TEST_CASE("plain text appears in output") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("Hello world", 11);
    CHECK(rtf.find("Hello world") != std::string::npos);
}

TEST_CASE("RTF special chars are escaped") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("a\\b{c}d", 7);
    CHECK(rtf.find("a\\\\b\\{c\\}d") != std::string::npos);
}

TEST_CASE("non-ASCII UTF-8 emits RTF unicode escapes") {
    // U+00E9 (e-acute) = UTF-8: 0xC3 0xA9 = RTF: \u233?
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("\xC3\xA9", 2);
    CHECK(rtf.find("\\u233?") != std::string::npos);
}
