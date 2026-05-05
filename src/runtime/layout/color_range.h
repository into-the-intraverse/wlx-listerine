#pragma once

#include <cstdint>

namespace wlx::runtime::layout {


struct ColorRange {
    uint32_t start = 0;
    uint32_t length = 0;
    uint32_t color = 0;       // foreground 0x00RRGGBB
    uint32_t bg_color = 0;    // background 0x00RRGGBB
    bool has_bg = false;
    uint8_t modifiers = 0;    // OR of TextModifier bits
};

}  // namespace wlx::runtime::layout
