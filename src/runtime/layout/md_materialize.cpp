#include "runtime/layout/md_materialize.h"

#include <algorithm>
#include <cmath>

namespace wlx::runtime::layout {

float estimate_inline_height(int char_count, float avg_advance,
                             float max_width, float line_height) {
    if (max_width <= 1.0f) return line_height;
    float est_width = static_cast<float>(std::max(0, char_count)) * avg_advance;
    // Stay in float: casting a ceil() that exceeds INT_MAX to int is UB. A real
    // paragraph never approaches that, but keeping it in float removes the hazard.
    float lines = std::max(1.0f, std::ceil(est_width / max_width));
    return lines * line_height;
}

float estimate_code_fence_height(int line_count, float code_line_height, float padding) {
    int lines = std::max(1, line_count);
    return static_cast<float>(lines) * code_line_height + 2.0f * padding;
}

void shift_block_y(LayoutBlock& lb, float dy) {
    lb.rect.top += dy;
    lb.rect.bottom += dy;
    for (auto& r : lb.text_runs) { r.rect.top += dy; r.rect.bottom += dy; }
    lb.bullet_pos.y += dy;
    for (auto& s : lb.spans) { s.rect.top += dy; s.rect.bottom += dy; }
    if (lb.has_trailing_ws) { lb.trailing_ws_rect.top += dy; lb.trailing_ws_rect.bottom += dy; }
    // ws_markers (y relative to run origin) and indent_guides (absolute x; y read
    // from rect at paint) carry no absolute-y state -> nothing to shift.
}

void apply_height_delta(LayoutDocument& doc, int from_idx, float delta) {
    if (delta == 0.0f) return;
    for (int i = from_idx + 1; i < static_cast<int>(doc.blocks.size()); ++i)
        shift_block_y(doc.blocks[i], delta);
    for (auto& a : doc.anchors)
        if (a.block_index > from_idx) a.y_offset += delta;
    doc.total_height += delta;
}

}  // namespace wlx::runtime::layout
