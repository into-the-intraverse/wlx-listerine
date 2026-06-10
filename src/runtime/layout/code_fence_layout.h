#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/layout/color_range.h"
#include "wlx_core/abi.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
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

// UTF-16 -> UTF-8 conversion plus a cumulative byte-offset table (one entry per
// UTF-16 unit + end sentinel) for mapping colorizer byte offsets back to wchar
// indices. Both units of a surrogate pair map to the pair's first byte, so a
// byte offset at a code-point boundary can never split the pair.
struct Utf8Mapping {
    std::string utf8;
    std::vector<uint32_t> wchar_to_byte;  // non-decreasing; size() == text.size() + 1
};
Utf8Mapping utf8_with_offsets(const std::wstring& text);

// Builds the code-fence text layout and resolves syntax color ranges via the
// core colorizer ABI. Shared by the eager layout_code_fence and (later) the lazy
// materializer. `code_format` must already have wrapping set to NO_WRAP.
CodeFenceLayoutResult build_code_fence_layout(
    IDWriteFactory* dwrite,
    IDWriteTextFormat* code_format,
    const CodeFenceInput& in);

}  // namespace wlx::runtime::layout
