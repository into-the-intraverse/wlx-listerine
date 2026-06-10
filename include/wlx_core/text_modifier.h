#pragma once

#include <cstdint>

// Bit flags applied to text ranges by the colorizer pipeline.
// Stored on ResolvedStyle, ColorSpan, and ColorRange. Layout builders bake
// them into IDWriteTextLayouts at creation time (DWrite font/decoration
// setters, before height measurement); RenderEngine::paint_text_runs only
// re-applies per-range color brushes (SetDrawingEffect), never these.
enum TextModifier : uint8_t {
    MOD_BOLD          = 1 << 0,
    MOD_ITALIC        = 1 << 1,
    MOD_UNDERLINE     = 1 << 2,
    MOD_STRIKETHROUGH = 1 << 3,
};
