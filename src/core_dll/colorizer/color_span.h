#pragma once

#include "wlx_core/abi.h"

#include <cstdint>

struct WLX_CORE_API ColorSpan {
    uint32_t start = 0;       // byte offset in UTF-8 source
    uint32_t length = 0;
    uint32_t color = 0;       // foreground 0x00RRGGBB
    uint32_t bg_color = 0;
    bool has_bg = false;
    uint8_t modifiers = 0;    // OR of TextModifier bits
};
