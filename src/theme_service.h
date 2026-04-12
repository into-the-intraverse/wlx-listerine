#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <string>
#include <vector>
#include <d2d1.h>

struct ColorPalette {
    uint32_t background;
    uint32_t text;
    uint32_t heading;
    uint32_t muted;
    uint32_t link;
    uint32_t link_hover;
    uint32_t code_bg;
    uint32_t quote_border;
    uint32_t rule;
    uint32_t selection;
};

struct SpacingConfig {
    float paragraph_spacing = 12.0f;
    float heading_spacing_above = 24.0f;
    float heading_spacing_below = 12.0f;
    float list_indent = 24.0f;
    float quote_indent = 16.0f;
    float quote_border_width = 3.0f;
    float code_padding = 8.0f;
    float line_height_factor = 1.5f;
};

struct FontConfig {
    std::wstring body_family = L"Segoe UI";
    std::wstring code_family = L"Cascadia Code";
    std::wstring emoji_family = L"Segoe UI Emoji";
    float body_size = 14.0f;
    float code_size = 13.0f;
};

struct ThemeConfig {
    int version = 2;
    std::wstring detect_string = L"EXT=\"MD\" | EXT=\"MARKDOWN\"";
    std::vector<std::wstring> extensions = {L"md", L"markdown", L"mdown", L"mkd", L"mkdn"};
    FontConfig fonts;
    SpacingConfig spacing;
    ColorPalette light;
    ColorPalette dark;

    // Code highlighting
    std::wstring code_grammar_dir = L"grammars";
    std::wstring code_theme_dir = L"themes";
    std::string code_default_language;  // empty = no highlighting for untagged blocks
    std::string code_theme = "default";
};

class ThemeService {
public:
    ThemeService();

    void load(const std::wstring& toml_path);

    const ThemeConfig& config() const { return config_; }
    ThemeConfig& mutable_config() { return config_; }
    const ColorPalette& palette(bool dark_mode) const;
    const FontConfig& fonts() const { return config_.fonts; }
    const SpacingConfig& spacing() const { return config_.spacing; }

    static D2D1_COLOR_F to_d2d_color(uint32_t rgb, float alpha = 1.0f);
    static uint32_t parse_hex_color(const std::string& hex, uint32_t fallback = 0);

    uint64_t theme_hash() const;

private:
    ThemeConfig config_;
    static ThemeConfig default_config();
};
