#pragma once

#include "wlx_core/abi.h"

#include <cstdint>

namespace wlx::core::theme {


// A resolved style from a Helix theme. Carries fg/bg colors and a bitset
// of text modifiers (bold/italic/underline/strikethrough); see TextModifier
// in wlx_core/text_modifier.h.
struct WLX_CORE_API ResolvedStyle {
    uint32_t fg = 0;        // 0x00RRGGBB
    uint32_t bg = 0;        // 0x00RRGGBB
    bool has_fg = false;
    bool has_bg = false;
    uint8_t modifiers = 0;  // OR of TextModifier bits
};

}  // namespace wlx::core::theme
