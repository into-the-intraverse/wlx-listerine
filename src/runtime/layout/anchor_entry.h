#pragma once

#include <string>

namespace wlx::runtime::layout {


struct AnchorEntry {
    std::wstring slug;
    float y_offset = 0;
    int block_index = -1;  // owning Heading block; set by both layout paths so
                           // y_offset can be re-derived after a lazy reflow.
};

}  // namespace wlx::runtime::layout
