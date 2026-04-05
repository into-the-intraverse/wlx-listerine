#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct ColorScheme {
    uint32_t background;
    uint32_t text;
    uint32_t heading;
    uint32_t code_background;
    uint32_t blockquote;
    uint32_t link;
};

struct Config {
    std::vector<std::string> extensions;
    std::string detect_string;
    std::string body_font;
    int body_size;
    std::string code_font;
    int code_size;
    ColorScheme light;
    ColorScheme dark;
};

Config default_config();
Config load_config(const std::string& path);
uint32_t parse_hex_color(const std::string& hex);
uint32_t parse_hex_color(const std::string& hex, uint32_t fallback);
