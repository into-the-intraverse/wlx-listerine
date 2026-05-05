#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/theme/color_palette.h"
#include "runtime/theme/font_config.h"
#include "runtime/theme/spacing_config.h"
#include "runtime/theme/theme_config.h"

#include <cstdint>
#include <string>

#include <d2d1.h>

namespace wlx::runtime::theme {


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

}  // namespace wlx::runtime::theme
