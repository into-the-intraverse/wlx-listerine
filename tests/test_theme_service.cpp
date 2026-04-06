#include <doctest/doctest.h>
#include "theme_service.h"
#include <fstream>
#include <cmath>
#include <cstdio>

TEST_CASE("parse_hex_color with # prefix") {
    CHECK(ThemeService::parse_hex_color("#FFFFFF") == 0xFFFFFF);
    CHECK(ThemeService::parse_hex_color("#000000") == 0x000000);
    CHECK(ThemeService::parse_hex_color("#0969DA") == 0x0969DA);
}

TEST_CASE("parse_hex_color without # prefix") {
    CHECK(ThemeService::parse_hex_color("FF0000") == 0xFF0000);
    CHECK(ThemeService::parse_hex_color("1F2328") == 0x1F2328);
}

TEST_CASE("parse_hex_color with invalid string returns fallback") {
    CHECK(ThemeService::parse_hex_color("not_a_color", 0x123456) == 0x123456);
    CHECK(ThemeService::parse_hex_color("", 0xABCDEF) == 0xABCDEF);
    CHECK(ThemeService::parse_hex_color("ZZZZZZ", 42) == 42);
}

TEST_CASE("to_d2d_color converts correctly") {
    auto c = ThemeService::to_d2d_color(0xFF0000);
    CHECK(c.r == doctest::Approx(1.0f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.b == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(1.0f));

    auto c2 = ThemeService::to_d2d_color(0x00FF00, 0.5f);
    CHECK(c2.r == doctest::Approx(0.0f));
    CHECK(c2.g == doctest::Approx(1.0f));
    CHECK(c2.b == doctest::Approx(0.0f));
    CHECK(c2.a == doctest::Approx(0.5f));

    auto c3 = ThemeService::to_d2d_color(0x0000FF);
    CHECK(c3.r == doctest::Approx(0.0f));
    CHECK(c3.g == doctest::Approx(0.0f));
    CHECK(c3.b == doctest::Approx(1.0f));
}

TEST_CASE("default config has correct extension count") {
    ThemeService svc;
    CHECK(svc.config().extensions.size() == 5);
}

TEST_CASE("default config light palette background") {
    ThemeService svc;
    CHECK(svc.palette(false).background == 0xFFFFFF);
}

TEST_CASE("default config dark palette background") {
    ThemeService svc;
    CHECK(svc.palette(true).background == 0x0D1117);
}

TEST_CASE("default font config body_family") {
    ThemeService svc;
    CHECK(svc.fonts().body_family == L"Segoe UI");
}

TEST_CASE("default spacing paragraph_spacing") {
    ThemeService svc;
    CHECK(svc.spacing().paragraph_spacing == doctest::Approx(12.0f));
}

TEST_CASE("theme_hash returns same value for same config") {
    ThemeService a;
    ThemeService b;
    CHECK(a.theme_hash() == b.theme_hash());
}

TEST_CASE("loading from nonexistent file uses defaults") {
    ThemeService svc;
    svc.load(L"this_file_does_not_exist_at_all.toml");

    CHECK(svc.config().extensions.size() == 5);
    CHECK(svc.palette(false).background == 0xFFFFFF);
    CHECK(svc.palette(true).background == 0x0D1117);
    CHECK(svc.fonts().body_family == L"Segoe UI");
    CHECK(svc.spacing().paragraph_spacing == doctest::Approx(12.0f));
}

TEST_CASE("loading from valid TOML overrides values") {
    const char* path = "test_data/test_theme.toml";
    {
        std::ofstream f(path);
        REQUIRE(f.is_open());
        f << R"(
[general]
detect_string = 'EXT="MD"'
extensions = ["md", "txt"]

[fonts]
body = "Arial"
body_size = 16.0
code = "Courier New"
code_size = 14.0
emoji = "Noto Color Emoji"

[spacing]
paragraph = 20.0
heading_above = 30.0
list_indent = 32.0
line_height_factor = 1.8

[colors.light]
background = "#AABBCC"
text = "#112233"
link = "#445566"

[colors.dark]
background = "#334455"
link = "#99AAFF"
selection = "#223344"
)";
    }

    ThemeService svc;
    svc.load(L"test_data/test_theme.toml");

    // general
    CHECK(svc.config().detect_string == L"EXT=\"MD\"");
    CHECK(svc.config().extensions.size() == 2);
    CHECK(svc.config().extensions[0] == L"md");
    CHECK(svc.config().extensions[1] == L"txt");

    // fonts -- overridden
    CHECK(svc.fonts().body_family == L"Arial");
    CHECK(svc.fonts().body_size == doctest::Approx(16.0f));
    CHECK(svc.fonts().code_family == L"Courier New");
    CHECK(svc.fonts().code_size == doctest::Approx(14.0f));
    CHECK(svc.fonts().emoji_family == L"Noto Color Emoji");

    // spacing -- overridden
    CHECK(svc.spacing().paragraph_spacing == doctest::Approx(20.0f));
    CHECK(svc.spacing().heading_spacing_above == doctest::Approx(30.0f));
    CHECK(svc.spacing().list_indent == doctest::Approx(32.0f));
    CHECK(svc.spacing().line_height_factor == doctest::Approx(1.8f));
    // spacing -- defaults kept
    CHECK(svc.spacing().heading_spacing_below == doctest::Approx(12.0f));
    CHECK(svc.spacing().quote_indent == doctest::Approx(16.0f));
    CHECK(svc.spacing().code_padding == doctest::Approx(8.0f));

    // colors.light -- overridden
    CHECK(svc.palette(false).background == 0xAABBCC);
    CHECK(svc.palette(false).text == 0x112233);
    CHECK(svc.palette(false).link == 0x445566);
    // colors.light -- defaults kept
    CHECK(svc.palette(false).heading == 0x1F2328);
    CHECK(svc.palette(false).muted == 0x57606A);
    CHECK(svc.palette(false).code_bg == 0xF6F8FA);

    // colors.dark -- overridden
    CHECK(svc.palette(true).background == 0x334455);
    CHECK(svc.palette(true).link == 0x99AAFF);
    CHECK(svc.palette(true).selection == 0x223344);
    // colors.dark -- defaults kept
    CHECK(svc.palette(true).text == 0xE6EDF3);
    CHECK(svc.palette(true).heading == 0xE6EDF3);
    CHECK(svc.palette(true).quote_border == 0x30363D);

    // hash should differ from defaults
    ThemeService def;
    CHECK(svc.theme_hash() != def.theme_hash());

    std::remove(path);
}
