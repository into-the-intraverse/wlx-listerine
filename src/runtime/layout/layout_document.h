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

    // Jump-to-line / gutter support.
    // line_tops[n-1] = document-space Y (in DIPs) of the top of logical line n.
    // size() == total logical-line count. Filled by build_line_index().
    std::vector<float> line_tops;
    // Width (DIPs) reserved on the left for the markdown line-number gutter.
    // 0 = no gutter (the colorizer draws its own gutter via bullets).
    float gutter_width = 0;
};

}  // namespace wlx::runtime::layout
