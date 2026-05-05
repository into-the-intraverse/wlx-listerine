#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/layout/code_bg_rect.h"
#include "runtime/layout/color_range.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wlx::runtime::layout {


struct TextRun {
    std::wstring text;
    D2D1_RECT_F rect = {};
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    uint32_t color = 0;
    bool is_code = false;
    std::vector<ColorRange> color_ranges;
    std::vector<CodeBgRect> code_bg_rects;
};

}  // namespace wlx::runtime::layout
