#pragma once

#include "runtime/layout/anchor_entry.h"
#include "runtime/layout/layout_block.h"

#include <functional>
#include <vector>

namespace wlx::runtime::layout {


struct LayoutDocument {
    std::vector<LayoutBlock> blocks;
    std::vector<AnchorEntry> anchors;
    float total_height = 0;
    float viewport_width = 0;

    // Lazy/windowed layout hook. When set, the renderer calls this for each
    // visible block (passing the block and its index) before painting it, to
    // build the block's IDWriteTextLayout + decorations on demand. This lets a
    // large document compute geometry (rects/line_tops/total_height) up front
    // arithmetically while deferring the per-line CreateTextLayout cost to the
    // ~viewport of lines actually drawn. Null => the document is fully eager
    // (markdown, and the colorizer word-wrap path). Idempotent: a block whose
    // layout is already built is left untouched.
    std::function<void(LayoutBlock&, int)> materialize_block;

    // Jump-to-line / gutter support.
    // line_tops[n-1] = document-space Y (in DIPs) of the top of logical line n.
    // size() == total logical-line count. Filled by build_line_index().
    std::vector<float> line_tops;
    // Width (DIPs) reserved on the left for the markdown line-number gutter.
    // 0 = no gutter (the colorizer draws its own gutter via bullets).
    float gutter_width = 0;
};

}  // namespace wlx::runtime::layout
