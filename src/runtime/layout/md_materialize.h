#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/layout/layout_document.h"
#include "runtime/parser/document.h"
#include "runtime/theme/color_palette.h"
#include "runtime/theme/font_config.h"
#include "wlx_core/abi.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wlx::runtime::layout {

// --- estimation (no IDWriteTextLayout created) ---

// Estimated height of an inline run: ceil(estimated_width / max_width) lines,
// each `line_height` tall, minimum one line.
float estimate_inline_height(int char_count, float avg_advance,
                             float max_width, float line_height);

// Estimated height of a no-wrap code fence: lines * code_line_height + 2*padding.
float estimate_code_fence_height(int line_count, float code_line_height, float padding);

// --- deferred materialization ---

struct BlockRecipe {
    // None       : eager, non-evictable (blockquote container, HR, nested code fence).
    // Inline     : lazy paragraph/heading — replay recomputes rect/run geometry from
    //              the estimate→measured transition (and may reflow later blocks).
    // InlineFixed: eager list item / table cell / quoted paragraph/heading — replay
    //              rebuilds ONLY the layout/colors/spans and keeps the already-exact
    //              rect/run geometry (delta 0, no reflow). Carries `alignment` for
    //              table-cell columns. See 2026-06-14-md-skeleton-stage2b-design.md.
    enum class Kind { None, Inline, InlineFixed, CodeFence } kind = Kind::None;

    // Kind::Inline / Kind::InlineFixed (Paragraph / Heading / list item / table cell)
    const std::vector<parser::InlineNode>* inlines = nullptr;  // into ctx.document
    float max_width = 0;
    uint32_t default_color = 0;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;  // null => ctx.body_format
    bool force_bold = false;
    DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING;  // InlineFixed only
    float left = 0;     // doc-space x of the run (for span offsetting)
    float right = 0;

    // Kind::CodeFence
    std::wstring code_text;
    std::string  code_language;
    bool         wrap_code = false;
    float        code_left = 0;
    float        code_padding = 0;
    float        code_right = 0;
};

struct MdMaterializeCtx {
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite;
    theme::FontConfig fonts;
    theme::ColorPalette colors;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> body_format;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> code_format;
    WlxCore* core = nullptr;
    bool dark_mode = false;
    std::string default_language;
    std::shared_ptr<const parser::Document> document;  // lifetime anchor for inlines ptrs
    std::vector<BlockRecipe> recipes;                  // indexed by block index
};

// Build the real IDWriteTextLayout for block `idx` and update its measured
// height in place. Idempotent. DEFINITION lands in Task 5 (declared here).
void md_materialize(MdMaterializeCtx& ctx, LayoutBlock& lb, int idx);

// --- reflow (Y-translation only) ---

// Translate every absolute-Y field of one block by dy.
void shift_block_y(LayoutBlock& lb, float dy);

// A block at `from_idx` changed height by `delta`. Translate all later blocks,
// anchors owned by later blocks, and total_height. Does NOT rebuild line_tops.
void apply_height_delta(LayoutDocument& doc, int from_idx, float delta);

// Materialize the lazy blocks intersecting [scroll_y, scroll_y + 2*viewport_h],
// reflow later blocks by Y-translation when measured heights differ from their
// estimates, then rebuild the line index and re-derive anchor Ys from the
// corrected block tops. No-op (returns false) if the document is fully eager
// (materialize_block == null). Returns true if any block was (re)materialized,
// so a host caller can refresh its scrollbar from doc.total_height.
//
// When `recipes` is non-null, blocks materialized earlier but now further than
// kEvictScreens screens outside the viewport drop their IDWriteTextLayout +
// per-paint decoration state (color_ranges, code_bg_rects, spans, ws/trailing)
// and re-materialize from their recipe on re-entry. Measured rects and
// run.text are KEPT, so geometry/line_tops/search stay exact and eviction
// never triggers a reflow or an index rebuild. Recipe-less blocks
// (lists/quotes/tables — built eagerly, kind == None) are never evicted.
inline constexpr float kEvictScreens = 2.0f;
bool materialize_viewport(LayoutDocument& doc, float scroll_y, float viewport_h,
                          const std::vector<BlockRecipe>* recipes = nullptr);

}  // namespace wlx::runtime::layout
