#pragma once

#include "core_dll/colorizer/color_span.h"
#include "wlx_core/abi.h"

#include <vector>

struct WLX_CORE_API ColorizeResult {
    std::vector<ColorSpan> spans;  // sorted by start, non-overlapping
};
