#include <doctest/doctest.h>
#include "helix_theme.h"
#include <fstream>
#include <filesystem>
#include <cstdio>

// ===========================================================================
// HelixTheme::make_default
// ===========================================================================

TEST_CASE("HelixTheme::make_default dark has keyword color") {
    auto theme = HelixTheme::make_default(true);
    CHECK(!theme.empty());
    CHECK(theme.resolve_fg("keyword", 0) == 0xC586C0);
}

TEST_CASE("HelixTheme::make_default light has keyword color") {
    auto theme = HelixTheme::make_default(false);
    CHECK(!theme.empty());
    CHECK(theme.resolve_fg("keyword", 0) == 0xAF00DB);
}

TEST_CASE("HelixTheme::make_default covers core scopes") {
    auto dark = HelixTheme::make_default(true);
    // All these should resolve to non-zero colors
    CHECK(dark.resolve_fg("keyword", 0) != 0);
    CHECK(dark.resolve_fg("function", 0) != 0);
    CHECK(dark.resolve_fg("string", 0) != 0);
    CHECK(dark.resolve_fg("comment", 0) != 0);
    CHECK(dark.resolve_fg("type", 0) != 0);
    CHECK(dark.resolve_fg("variable", 0) != 0);
    CHECK(dark.resolve_fg("constant", 0) != 0);
    CHECK(dark.resolve_fg("operator", 0) != 0);
    CHECK(dark.resolve_fg("punctuation", 0) != 0);
    CHECK(dark.resolve_fg("tag", 0) != 0);
    CHECK(dark.resolve_fg("attribute", 0) != 0);
    CHECK(dark.resolve_fg("property", 0) != 0);
    CHECK(dark.resolve_fg("namespace", 0) != 0);
    CHECK(dark.resolve_fg("constructor", 0) != 0);
}

TEST_CASE("HelixTheme::make_default dark and light differ") {
    auto dark = HelixTheme::make_default(true);
    auto light = HelixTheme::make_default(false);

    // At least some scopes must have different colors
    bool any_differs =
        dark.resolve_fg("keyword", 0) != light.resolve_fg("keyword", 0) ||
        dark.resolve_fg("string", 0) != light.resolve_fg("string", 0) ||
        dark.resolve_fg("function", 0) != light.resolve_fg("function", 0);
    CHECK(any_differs);
}

// ===========================================================================
// Hierarchical scope resolution
// ===========================================================================

TEST_CASE("HelixTheme resolve: exact match") {
    auto theme = HelixTheme::make_default(true);
    CHECK(theme.resolve_fg("function.builtin", 0) == 0xDCDCAA);
}

TEST_CASE("HelixTheme resolve: prefix fallback") {
    auto theme = HelixTheme::make_default(true);
    // "keyword.control.conditional" not defined -> falls back to "keyword"
    CHECK(theme.resolve_fg("keyword.control.conditional", 0) == 0xC586C0);
}

TEST_CASE("HelixTheme resolve: two-level fallback") {
    auto theme = HelixTheme::make_default(true);
    // "function.method.private" -> "function.method" -> "function"
    CHECK(theme.resolve_fg("function.method.private", 0) == 0xDCDCAA);
}

TEST_CASE("HelixTheme resolve: unknown scope returns fallback") {
    auto theme = HelixTheme::make_default(true);
    CHECK(theme.resolve_fg("totally_unknown", 0x123456) == 0x123456);
}

TEST_CASE("HelixTheme resolve: empty scope returns fallback") {
    auto theme = HelixTheme::make_default(true);
    CHECK(theme.resolve_fg("", 0x111111) == 0x111111);
}

TEST_CASE("HelixTheme resolve returns nullopt for unknown") {
    auto theme = HelixTheme::make_default(true);
    CHECK(!theme.resolve("zzz_nonexistent").has_value());
}

TEST_CASE("HelixTheme resolve returns style for known scope") {
    auto theme = HelixTheme::make_default(true);
    auto style = theme.resolve("keyword");
    REQUIRE(style.has_value());
    CHECK(style->has_fg);
    CHECK(style->fg == 0xC586C0);
}

// ===========================================================================
// Loading from TOML files
// ===========================================================================

TEST_CASE("HelixTheme load: default dark theme from config/themes") {
    if (!std::filesystem::exists("config/themes/default.toml")) return;

    auto theme = HelixTheme::load("default", "config/themes");
    CHECK(!theme.empty());
    CHECK(theme.resolve_fg("keyword", 0) != 0);
    CHECK(theme.resolve_fg("function", 0) != 0);
    CHECK(theme.resolve_fg("string", 0) != 0);
}

TEST_CASE("HelixTheme load: default light theme from config/themes") {
    if (!std::filesystem::exists("config/themes/default_light.toml")) return;

    auto theme = HelixTheme::load("default_light", "config/themes");
    CHECK(!theme.empty());
    CHECK(theme.resolve_fg("keyword", 0) != 0);

    // Verify punctuation and comment resolve (critical for git-commit grammar)
    auto punc = theme.resolve("punctuation");
    CHECK(punc.has_value());
    if (punc) CHECK(punc->has_fg);

    auto punc_delim = theme.resolve("punctuation.delimiter");
    CHECK(punc_delim.has_value());
    if (punc_delim) CHECK(punc_delim->has_fg);

    auto comment = theme.resolve("comment");
    CHECK(comment.has_value());
    if (comment) CHECK(comment->has_fg);
    CHECK(theme.resolve_fg("comment", 999) == 0x008000);
}

TEST_CASE("HelixTheme load: missing theme falls back to built-in") {
    auto theme = HelixTheme::load("nonexistent_theme_name", "nonexistent_dir");
    CHECK(!theme.empty());
    // Should fall back to make_default(true)
    CHECK(theme.resolve_fg("keyword", 0) == 0xC586C0);
}

// ===========================================================================
// TOML parsing: palette, styles, hex colors
// ===========================================================================

class TempThemeDir {
public:
    TempThemeDir() : dir_("_test_themes_tmp") {
        std::filesystem::create_directories(dir_);
    }
    ~TempThemeDir() {
        std::filesystem::remove_all(dir_);
    }
    const std::filesystem::path& path() const { return dir_; }

    void write(const std::string& name, const std::string& content) {
        std::ofstream f(dir_ / (name + ".toml"));
        f << content;
    }
private:
    std::filesystem::path dir_;
};

TEST_CASE("HelixTheme load: simple string style (fg color)") {
    TempThemeDir tmp;
    tmp.write("test1", R"(
        "keyword" = "#FF0000"
        "string"  = "#00FF00"
    )");

    auto theme = HelixTheme::load("test1", tmp.path().string());
    CHECK(theme.resolve_fg("keyword", 0) == 0xFF0000);
    CHECK(theme.resolve_fg("string", 0) == 0x00FF00);
}

TEST_CASE("HelixTheme load: table style with fg and bg") {
    TempThemeDir tmp;
    tmp.write("test2", R"(
        "keyword" = { fg = "#AABBCC", bg = "#112233" }
    )");

    auto theme = HelixTheme::load("test2", tmp.path().string());
    auto style = theme.resolve("keyword");
    REQUIRE(style.has_value());
    CHECK(style->has_fg);
    CHECK(style->fg == 0xAABBCC);
    CHECK(style->has_bg);
    CHECK(style->bg == 0x112233);
}

TEST_CASE("HelixTheme load: palette color resolution") {
    TempThemeDir tmp;
    tmp.write("test3", R"(
        "keyword" = "my_red"
        "string"  = "blue"

        [palette]
        my_red = "#DD0000"
    )");

    auto theme = HelixTheme::load("test3", tmp.path().string());
    CHECK(theme.resolve_fg("keyword", 0) == 0xDD0000);
    // "blue" is a built-in ANSI color
    CHECK(theme.resolve_fg("string", 0) == 0x3465A4);
}

TEST_CASE("HelixTheme load: #RGB shorthand expansion") {
    TempThemeDir tmp;
    tmp.write("test4", R"(
        "keyword" = "#F00"
    )");

    auto theme = HelixTheme::load("test4", tmp.path().string());
    CHECK(theme.resolve_fg("keyword", 0) == 0xFF0000);
}

TEST_CASE("HelixTheme load: inherits from parent theme") {
    TempThemeDir tmp;
    tmp.write("parent", R"(
        "keyword" = "#AA0000"
        "string"  = "#00AA00"
    )");
    tmp.write("child", R"(
        inherits = "parent"
        "keyword" = "#FF0000"
    )");

    auto theme = HelixTheme::load("child", tmp.path().string());
    // keyword overridden by child
    CHECK(theme.resolve_fg("keyword", 0) == 0xFF0000);
    // string inherited from parent
    CHECK(theme.resolve_fg("string", 0) == 0x00AA00);
}

TEST_CASE("HelixTheme load: circular inherits does not loop") {
    TempThemeDir tmp;
    tmp.write("a", R"(
        inherits = "b"
        "keyword" = "#AA0000"
    )");
    tmp.write("b", R"(
        inherits = "a"
        "string" = "#00BB00"
    )");

    // Should not infinite loop
    auto theme = HelixTheme::load("a", tmp.path().string());
    CHECK(theme.resolve_fg("keyword", 0) == 0xAA0000);
}

TEST_CASE("HelixTheme load: ui scopes are ignored") {
    TempThemeDir tmp;
    tmp.write("test_ui", R"(
        "keyword"       = "#FF0000"
        "ui.background" = "#000000"
        "ui.cursor"     = "#FFFFFF"
    )");

    auto theme = HelixTheme::load("test_ui", tmp.path().string());
    CHECK(theme.resolve_fg("keyword", 0) == 0xFF0000);
    // ui scopes should not be in the map
    CHECK(!theme.resolve("ui.background").has_value());
    CHECK(!theme.resolve("ui.cursor").has_value());
}

TEST_CASE("HelixTheme load: modifiers are parsed but fg still works") {
    TempThemeDir tmp;
    tmp.write("test_mod", R"(
        "keyword" = { fg = "#CC00CC", modifiers = ["bold", "italic"] }
    )");

    auto theme = HelixTheme::load("test_mod", tmp.path().string());
    // modifiers ignored, but fg should work
    CHECK(theme.resolve_fg("keyword", 0) == 0xCC00CC);
}

TEST_CASE("HelixTheme load: dotted scope names work") {
    TempThemeDir tmp;
    tmp.write("test_dotted", R"(
        "function.builtin" = "#AABB00"
        "variable.other.member" = "#00CCDD"
    )");

    auto theme = HelixTheme::load("test_dotted", tmp.path().string());
    CHECK(theme.resolve_fg("function.builtin", 0) == 0xAABB00);
    CHECK(theme.resolve_fg("variable.other.member", 0) == 0x00CCDD);
}
