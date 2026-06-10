#pragma once

#include "core_dll/colorizer/colorize_result.h"
#include "wlx_core/abi.h"

namespace wlx_core {

// Convert an ABI WlxColorSpan array into the C++ ColorizeResult type. Frees the
// spans via wlx_core_free_spans. Returns an empty ColorizeResult if spans is
// null or count is zero. Shared by the colorizer host adapter and the
// screenshot tool (both link wlx-core and consume the same ABI).
inline wlx::core::colorizer::ColorizeResult abi_spans_to_result(
    WlxColorSpan* spans, uint32_t count) {
    wlx::core::colorizer::ColorizeResult out;
    if (!spans || count == 0) {
        if (spans) wlx_core_free_spans(spans);
        return out;
    }
    out.spans.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& s = spans[i];
        wlx::core::colorizer::ColorSpan cs;
        cs.start = s.start; cs.length = s.length;
        cs.color = s.color; cs.bg_color = s.bg_color;
        cs.has_bg = s.has_bg != 0; cs.modifiers = s.modifiers;
        out.spans.push_back(cs);
    }
    wlx_core_free_spans(spans);
    return out;
}

}  // namespace wlx_core
