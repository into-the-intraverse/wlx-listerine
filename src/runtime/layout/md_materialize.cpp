#include "runtime/layout/md_materialize.h"

#include "runtime/layout/code_fence_layout.h"
#include "runtime/layout/inline_layout.h"

#include <d2d1.h>

#include <algorithm>
#include <cmath>
#include <utility>

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

void md_materialize(MdMaterializeCtx& ctx, LayoutBlock& lb, int idx) {
    if (idx < 0 || idx >= static_cast<int>(ctx.recipes.size())) return;
    BlockRecipe& rcp = ctx.recipes[idx];
    if (rcp.kind == BlockRecipe::Kind::None) return;
    if (lb.text_runs.empty() || lb.text_runs[0].layout) return;  // idempotent

    if (rcp.kind == BlockRecipe::Kind::Inline) {
        IDWriteTextFormat* fmt = rcp.format ? rcp.format.Get() : ctx.body_format.Get();
        auto r = build_inline_layout(ctx.dwrite.Get(), *rcp.inlines, rcp.max_width,
                                     rcp.default_color, fmt, rcp.force_bold,
                                     ctx.fonts, ctx.colors);
        if (!r.layout) return;
        auto& run = lb.text_runs[0];
        run.layout = r.layout;
        run.color_ranges = std::move(r.color_ranges);
        run.code_bg_rects = std::move(r.code_bg_rects);
        float top = lb.rect.top;
        lb.rect.bottom = top + r.height;
        run.rect = D2D1::RectF(rcp.left, top, rcp.right, top + r.height);
        lb.spans.clear();
        for (auto& s : r.spans) {
            s.rect.left += rcp.left; s.rect.right += rcp.left;
            s.rect.top  += top;      s.rect.bottom += top;
            lb.spans.push_back(std::move(s));
        }
    } else {  // CodeFence
        CodeFenceInput in;
        in.code_text = rcp.code_text;
        in.code_language = rcp.code_language;
        in.default_language = ctx.default_language;
        // NET text width: subtract padding on both sides exactly once here. The
        // recipe stores raw code_left/right/padding; build_code_fence_layout
        // expects max_width already net of padding.
        in.max_width = rcp.code_right - rcp.code_left - rcp.code_padding * 2;
        in.wrap_code = rcp.wrap_code;
        in.dark_mode = ctx.dark_mode;
        in.core = ctx.core;
        auto r = build_code_fence_layout(ctx.dwrite.Get(), ctx.code_format.Get(), in, ctx.colors);
        if (!r.layout) return;
        auto& run = lb.text_runs[0];
        run.layout = r.layout;
        run.color_ranges = std::move(r.color_ranges);
        float top = lb.rect.top;
        float block_h = r.height + rcp.code_padding * 2;
        lb.rect.bottom = top + block_h;
        run.rect = D2D1::RectF(rcp.code_left + rcp.code_padding, top + rcp.code_padding,
                               rcp.code_right - rcp.code_padding, top + rcp.code_padding + r.height);
    }
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
