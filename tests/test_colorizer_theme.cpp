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

TEST_CASE("SyntaxPalette new fields have non-zero defaults") {
    SyntaxPalette light = SyntaxPalette::defaults(false);
    CHECK(light.constant_builtin != 0);
    CHECK(light.function_builtin != 0);
    CHECK(light.function_call    != 0);
    CHECK(light.string_escape    != 0);
    CHECK(light.string_special   != 0);
    CHECK(light.boolean_lit      != 0);
    CHECK(light.tag              != 0);
    CHECK(light.tag_delimiter    != 0);
    CHECK(light.attribute        != 0);
    CHECK(light.constructor      != 0);
    CHECK(light.property         != 0);
    CHECK(light.label            != 0);

    SyntaxPalette dark = SyntaxPalette::defaults(true);
    CHECK(dark.constant_builtin != 0);
    CHECK(dark.function_builtin != 0);
    CHECK(dark.function_call    != 0);
    CHECK(dark.string_escape    != 0);
    CHECK(dark.string_special   != 0);
    CHECK(dark.boolean_lit      != 0);
    CHECK(dark.tag              != 0);
    CHECK(dark.tag_delimiter    != 0);
    CHECK(dark.attribute        != 0);
    CHECK(dark.constructor      != 0);
    CHECK(dark.property         != 0);
    CHECK(dark.label            != 0);
}

TEST_CASE("SyntaxPalette new fields differ between light and dark") {
    SyntaxPalette light = SyntaxPalette::defaults(false);
    SyntaxPalette dark  = SyntaxPalette::defaults(true);

    // At least some of the new fields must differ between light and dark
    bool any_differs =
        light.constant_builtin != dark.constant_builtin ||
        light.function_builtin != dark.function_builtin ||
        light.string_escape    != dark.string_escape    ||
        light.boolean_lit      != dark.boolean_lit      ||
        light.tag              != dark.tag              ||
        light.attribute        != dark.attribute;
    CHECK(any_differs);
}
