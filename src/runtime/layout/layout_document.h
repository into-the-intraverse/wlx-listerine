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

    // Indices (ascending) of blockquote border-container blocks. Containers are
    // emitted BEFORE their children with a rect spanning the whole quote, so
    // block TOPS stay ascending but BOTTOMS are non-monotonic at exactly these
    // blocks. The renderer's lower_bound visibility seek assumes non-decreasing
    // bottoms and can land past a still-visible container, so it paints these
    // in a dedicated pass. Indices stay valid under materialize-time rect
    // shifts (blocks are never inserted/removed). Empty for colorizer docs.
    std::vector<int> border_containers;

    // Jump-to-line / gutter support.
    // line_tops[n-1] = document-space Y (in DIPs) of the top of logical line n.
    // size() == total logical-line count. Filled by build_line_index().
    std::vector<float> line_tops;
    // Width (DIPs) reserved on the left for the markdown line-number gutter.
    // 0 = no gutter (the colorizer draws its own gutter via bullets).
    float gutter_width = 0;

    // Implicit-grid mode (colorizer no-wrap): `blocks` holds only a
    // materialized viewport±overscan window; blocks[i] represents source line
    // first_block_line + i, every line is exactly line_height tall, and all
    // public block indices (TextPosition, SearchMatch, HitResult) are SOURCE
    // LINE indices. grid_line_count == 0 => classic whole-file blocks (md,
    // colorizer wrap mode) and first_block_line stays 0.
    int first_block_line = 0;
    int grid_line_count = 0;
    bool is_grid() const { return grid_line_count > 0; }
};

}  // namespace wlx::runtime::layout
