#pragma once

#include <cstdint>

// Bit flags applied to text ranges by the colorizer pipeline.
// Stored on ResolvedStyle, ColorSpan, and ColorRange and consumed
// by RenderEngine::paint_text_runs via DWrite font/decoration setters.
enum TextModifier : uint8_t {
    MOD_BOLD          = 1 << 0,
    MOD_ITALIC        = 1 << 1,
    MOD_UNDERLINE     = 1 << 2,
    MOD_STRIKETHROUGH = 1 << 3,
};
