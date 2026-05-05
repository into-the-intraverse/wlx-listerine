#pragma once

#include "runtime/layout/layout_block.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/text_position.h"

namespace wlx::runtime::host {

// Hit-test (x, y) — already scroll-translated into document coordinates —
// against the laid-out document. Returns the (block, char_offset) the
// click landed on, snapping to the nearest block boundary if no block
// directly contains the point. Returns an invalid TextPosition only if
// `layout` has no text-bearing blocks.
wlx::runtime::layout::TextPosition hit_test_position(
    const wlx::runtime::layout::LayoutDocument& layout, float x, float y);

// Total character count across all text runs in `block`.
int block_text_length(const wlx::runtime::layout::LayoutBlock& block);

}  // namespace wlx::runtime::host
