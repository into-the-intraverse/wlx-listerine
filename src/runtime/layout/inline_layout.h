#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/layout/code_bg_rect.h"
#include "runtime/layout/color_range.h"
#include "runtime/layout/interactive_span.h"
#include "runtime/parser/document.h"
#include "runtime/theme/color_palette.h"
#include "runtime/theme/font_config.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace wlx::runtime::layout {

struct InlineLayoutResult {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    std::wstring full_text;
    std::vector<InteractiveSpan> spans;
    std::vector<ColorRange> color_ranges;
    std::vector<CodeBgRect> code_bg_rects;
    float width = 0;
    float height = 0;
};

// Build an IDWriteTextLayout from inline nodes, collecting interactive spans and
// inline-code background rects. Free function so both the eager layout pass and
// (later) a lazy materializer can call it. `format` must be non-null.
InlineLayoutResult build_inline_layout(
    IDWriteFactory* dwrite,
    const std::vector<parser::InlineNode>& inlines,
    float max_width,
    uint32_t default_color,
    IDWriteTextFormat* format,
    bool force_bold,
    const theme::FontConfig& fonts,
    const theme::ColorPalette& colors);

}  // namespace wlx::runtime::layout
