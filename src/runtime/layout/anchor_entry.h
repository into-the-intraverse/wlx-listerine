#pragma once

#include <string>

namespace wlx::runtime::layout {


struct AnchorEntry {
    std::wstring slug;
    float y_offset = 0;
    int block_index = -1;  // owning Heading block; -1 in eager mode (anchors exact).
                           // Lazy mode sets it so y_offset can be re-derived after reflow.
};

}  // namespace wlx::runtime::layout
