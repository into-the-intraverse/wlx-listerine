#pragma once

#include "runtime/layout/anchor_entry.h"
#include "runtime/layout/layout_block.h"

#include <vector>

namespace wlx::runtime::layout {


struct LayoutDocument {
    std::vector<LayoutBlock> blocks;
    std::vector<AnchorEntry> anchors;
    float total_height = 0;
    float viewport_width = 0;
};

}  // namespace wlx::runtime::layout
