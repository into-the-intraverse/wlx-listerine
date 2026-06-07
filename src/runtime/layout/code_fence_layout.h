#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/layout/color_range.h"
#include "runtime/theme/color_palette.h"
#include "wlx_core/abi.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace wlx::runtime::layout {

struct CodeFenceInput {
    std::wstring code_text;        // already trailing-newline-stripped by caller
    std::string  code_language;    // from the fence info string (may be empty)
    std::string  default_language; // theme.config().code_default_language
    float        max_width = 0;    // right - left - 2*padding
    bool         wrap_code = false;
    bool         dark_mode = false;
    WlxCore*     core = nullptr;
};

struct CodeFenceLayoutResult {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    std::vector<ColorRange> color_ranges;
    float height = 0;              // metrics.height (NOT including padding)
};

// Builds the code-fence text layout and resolves syntax color ranges via the
// core colorizer ABI. Shared by the eager layout_code_fence and (later) the lazy
// materializer. `code_format` must already have wrapping set to NO_WRAP.
CodeFenceLayoutResult build_code_fence_layout(
    IDWriteFactory* dwrite,
    IDWriteTextFormat* code_format,
    const CodeFenceInput& in,
    const theme::ColorPalette& colors);

}  // namespace wlx::runtime::layout
