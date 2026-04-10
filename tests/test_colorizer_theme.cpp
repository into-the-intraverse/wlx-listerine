#include <doctest/doctest.h>
#include "theme_loader.h"
#include <fstream>
#include <cstdio>

TEST_CASE("SyntaxPalette default has correct plain color") {
    SyntaxPalette pal = SyntaxPalette::defaults(false);
    CHECK(pal.plain == 0x1F2328);

    SyntaxPalette dark = SyntaxPalette::defaults(true);
    CHECK(dark.plain == 0xD4D4D4);
}

TEST_CASE("ThemeLoader loads default theme") {
    ThemeLoader loader(L"config/themes");
    auto pal = loader.palette_for("c", false);
    CHECK(pal.keyword == 0xAF00DB);
    CHECK(pal.comment == 0x008000);
    CHECK(pal.string == 0xA31515);
}

TEST_CASE("ThemeLoader dark mode returns dark palette") {
    ThemeLoader loader(L"config/themes");
    auto pal = loader.palette_for("c", true);
    CHECK(pal.keyword == 0xC586C0);
    CHECK(pal.comment == 0x6A9955);
}

TEST_CASE("ThemeLoader missing theme dir uses defaults") {
    ThemeLoader loader(L"nonexistent_dir");
    auto pal = loader.palette_for("c", false);
    CHECK(pal.plain == 0x1F2328);
}

TEST_CASE("ThemeLoader set_language_theme maps language to theme file") {
    ThemeLoader loader(L"config/themes");
    loader.set_language_theme("python", "default");
    auto pal = loader.palette_for("python", false);
    CHECK(pal.keyword == 0xAF00DB);
}
