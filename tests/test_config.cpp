#include <doctest/doctest.h>
#include "config.h"
#include <fstream>

TEST_CASE("parse_hex_color parses 6-digit hex with hash") {
    CHECK(parse_hex_color("#FFFFFF") == 0xFFFFFF);
    CHECK(parse_hex_color("#000000") == 0x000000);
    CHECK(parse_hex_color("#24292E") == 0x24292E);
    CHECK(parse_hex_color("#0366D6") == 0x0366D6);
}

TEST_CASE("parse_hex_color parses without hash") {
    CHECK(parse_hex_color("1E1E1E") == 0x1E1E1E);
}

TEST_CASE("default_config has correct defaults") {
    Config cfg = default_config();

    CHECK(cfg.extensions.size() == 5);
    CHECK(cfg.extensions[0] == "md");
    CHECK(cfg.detect_string == R"(EXT="MD" | EXT="MARKDOWN")");
    CHECK(cfg.body_font == "Segoe UI");
    CHECK(cfg.body_size == 11);
    CHECK(cfg.code_font == "Consolas");
    CHECK(cfg.code_size == 10);

    CHECK(cfg.light.background == 0xFFFFFF);
    CHECK(cfg.light.text == 0x24292E);
    CHECK(cfg.light.link == 0x0366D6);
    CHECK(cfg.dark.background == 0x1E1E1E);
    CHECK(cfg.dark.text == 0xD4D4D4);
    CHECK(cfg.dark.link == 0x58A6FF);
}

TEST_CASE("load_config from nonexistent file returns defaults") {
    Config cfg = load_config("nonexistent.toml");
    Config def = default_config();
    CHECK(cfg.body_font == def.body_font);
    CHECK(cfg.light.background == def.light.background);
}

TEST_CASE("load_config reads TOML overrides") {
    const char* path = "test_data/test_config.toml";
    {
        std::ofstream f(path);
        f << R"(
[options]
extensions = ["md", "txt"]
detect_string = 'EXT="MD"'

[fonts]
body = "Arial"
body_size = 14
code = "Courier New"
code_size = 12

[colors.light]
background = "#AABBCC"
text = "#112233"

[colors.dark]
background = "#334455"
link = "#99AAFF"
)";
    }

    Config cfg = load_config(path);

    CHECK(cfg.extensions.size() == 2);
    CHECK(cfg.extensions[0] == "md");
    CHECK(cfg.extensions[1] == "txt");
    CHECK(cfg.detect_string == R"(EXT="MD")");
    CHECK(cfg.body_font == "Arial");
    CHECK(cfg.body_size == 14);
    CHECK(cfg.code_font == "Courier New");
    CHECK(cfg.code_size == 12);
    CHECK(cfg.light.background == 0xAABBCC);
    CHECK(cfg.light.text == 0x112233);
    CHECK(cfg.light.link == 0x0366D6);  // unset keeps default
    CHECK(cfg.dark.background == 0x334455);
    CHECK(cfg.dark.link == 0x99AAFF);
    CHECK(cfg.dark.text == 0xD4D4D4);  // unset keeps default
}
