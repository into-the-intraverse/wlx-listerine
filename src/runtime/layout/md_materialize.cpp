#include "runtime/layout/md_materialize.h"

#include "runtime/layout/code_fence_layout.h"
#include "runtime/layout/inline_layout.h"
#include "runtime/layout/line_index.h"

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
        auto r = build_code_fence_layout(ctx.dwrite.Get(), ctx.code_format.Get(), in);
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

bool materialize_viewport(LayoutDocument& doc, float scroll_y, float viewport_h,
                          const std::vector<BlockRecipe>* recipes) {
    if (!doc.materialize_block) return false;  // eager doc: nothing to do
    float vp_top = scroll_y;
    float vp_bottom = vp_top + viewport_h * 2.0f;  // one screenful of overscan below

    // Accumulate height deltas and translate each block once as it is visited
    // (plus one tail pass below), instead of an O(later blocks)
    // apply_height_delta per materialized block. Incremental anchor shifts are
    // skipped entirely — the absolute re-derive below supersedes them.
    bool changed = false;
    float delta_sum = 0.0f;
    int i = 0;
    for (; i < static_cast<int>(doc.blocks.size()); ++i) {
        auto& b = doc.blocks[i];
        if (delta_sum != 0.0f) shift_block_y(b, delta_sum);
        if (b.rect.bottom < vp_top) continue;        // above viewport
        if (b.rect.top > vp_bottom) break;           // below (blocks are Y-sorted by index)
        if (b.text_runs.empty() || b.text_runs[0].layout) continue;  // no runs to build, or already materialized
        float old_bottom = b.rect.bottom;
        doc.materialize_block(b, i);
        float delta = b.rect.bottom - old_bottom;
        if (delta != 0.0f) {
            delta_sum += delta;
            changed = true;
        }
    }
    if (delta_sum != 0.0f) {
        for (int j = i + 1; j < static_cast<int>(doc.blocks.size()); ++j)
            shift_block_y(doc.blocks[j], delta_sum);
        doc.total_height += delta_sum;
    }

    if (changed) {
        build_line_index(doc);
        // Re-derive anchor Y absolutely from corrected block tops (the loop
        // above shifts blocks but not anchors); this also covers an owning
        // heading's own anchor, not just anchors of later blocks.
        for (auto& a : doc.anchors)
            if (a.block_index >= 0 && a.block_index < static_cast<int>(doc.blocks.size()))
                a.y_offset = doc.blocks[a.block_index].rect.top;
    }

    if (recipes) {
        // Evict far-off materialized blocks: keep geometry/text (exact
        // line_tops, search, no reflow), drop the heavy per-paint state. Only
        // recipe-backed blocks (kind != None) can re-materialize, so only
        // those are eligible. Eviction must NOT set `changed` — heights are
        // unchanged, so no reflow or line_tops rebuild is needed.
        const float keep_top    = vp_top    - viewport_h * kEvictScreens;
        const float keep_bottom = vp_bottom + viewport_h * kEvictScreens;
        for (size_t bi = 0; bi < doc.blocks.size(); ++bi) {
            auto& b = doc.blocks[bi];
            if (b.rect.bottom >= keep_top && b.rect.top <= keep_bottom) continue;
            if (b.text_runs.empty() || !b.text_runs[0].layout) continue;
            if (bi >= recipes->size() ||
                (*recipes)[bi].kind == BlockRecipe::Kind::None)
                continue;
            // md materializable blocks always have exactly one text_run (Inline /
            // CodeFence recipes each push_back a single run in layout_engine.cpp
            // lines ~245, ~278, ~309). The loop over all runs is defensive and
            // correct: if a future recipe type ever has more runs they all get evicted.
            for (auto& run : b.text_runs) {
                run.layout.Reset();
                run.color_ranges.clear();
                run.color_ranges.shrink_to_fit();
                run.code_bg_rects.clear();
                run.code_bg_rects.shrink_to_fit();
            }
            b.spans.clear();
            b.spans.shrink_to_fit();
            b.ws_markers.clear();
            b.ws_markers.shrink_to_fit();
            b.has_trailing_ws = false;
        }
    }

    return changed;
}

}  // namespace wlx::runtime::layout
