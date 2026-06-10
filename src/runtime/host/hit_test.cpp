#include "runtime/host/hit_test.h"

#include <dwrite.h>

#include <cmath>

namespace wlx::runtime::host {

using wlx::runtime::layout::LayoutBlock;
using wlx::runtime::layout::LayoutDocument;
using wlx::runtime::layout::TextPosition;

TextPosition hit_test_position(const LayoutDocument& layout, float x, float y) {
    int block_count = static_cast<int>(layout.blocks.size());

    for (int i = 0; i < block_count; i++) {
        auto& block = layout.blocks[i];
        if (block.text_runs.empty()) continue;
        // Text-bearing blocks are Y-ordered (containers like the blockquote
        // border block are appended out of order, but carry no text runs and
        // were skipped above) — nothing below this one can contain y.
        if (y < block.rect.top) break;
        if (y > block.rect.bottom) continue;
        // For table cells (multiple blocks share the same row), also check x bounds
        if (x < block.rect.left || x > block.rect.right) continue;

        auto& run = block.text_runs[0];
        if (!run.layout) continue;

        float local_x = x - run.rect.left;
        float local_y = y - run.rect.top;
        BOOL is_trailing = FALSE;
        BOOL is_inside = FALSE;
        DWRITE_HIT_TEST_METRICS htm = {};
        run.layout->HitTestPoint(local_x, local_y, &is_trailing, &is_inside, &htm);

        int offset = static_cast<int>(htm.textPosition);
        if (is_trailing) offset++;
        return TextPosition{i, offset};
    }

    // Snap to nearest block boundary
    int closest = -1;
    float closest_dist = 1e9f;
    for (int i = 0; i < block_count; i++) {
        auto& block = layout.blocks[i];
        if (block.text_runs.empty()) continue;
        float mid = (block.rect.top + block.rect.bottom) * 0.5f;
        float dist = std::abs(y - mid);
        if (dist < closest_dist) {
            closest_dist = dist;
            closest = i;
        }
    }

    if (closest >= 0) {
        auto& block = layout.blocks[closest];
        if (y < (block.rect.top + block.rect.bottom) * 0.5f) {
            return TextPosition{closest, 0};
        }
        return TextPosition{closest, block_text_length(block)};
    }
    return TextPosition{};
}

int block_text_length(const LayoutBlock& block) {
    int len = 0;
    for (auto& run : block.text_runs) len += static_cast<int>(run.text.size());
    return len;
}

}  // namespace wlx::runtime::host
