#pragma once

#include "runtime/layout/code_bg_rect.h"
#include "runtime/layout/color_range.h"
#include "runtime/layout/inline_layout.h"
#include "runtime/layout/interactive_span.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/md_materialize.h"
#include "runtime/parser/document.h"
#include "runtime/theme/theme_service.h"
#include "wlx_core/abi.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <memory>
#include <string>
#include <vector>

namespace wlx::runtime::layout {

using Microsoft::WRL::ComPtr;

class LayoutEngine {
public:
    LayoutEngine(IDWriteFactory* dwrite, const theme::ThemeService& theme, bool dark_mode,
                 WlxCore* core = nullptr);

    LayoutDocument layout(const parser::Document& doc, float viewport_width, bool wrap_code = false,
                          float gutter_width = 0.0f, bool lazy = false);

    // After a lazy layout(), the host calls this to take ownership of the
    // materialize context and set ctx->document (keeping recipe `inlines`
    // pointers alive). Null after an eager layout.
    std::shared_ptr<MdMaterializeCtx> take_md_ctx() { return std::move(md_ctx_); }

private:
    void layout_blocks(const std::vector<parser::BlockNode>& blocks, float& y,
                       float left, float right, int list_depth);
    // Lay out a single block by type. Extracted from layout_blocks so callers
    // (e.g. layout_blockquote) can dispatch one child without wrapping it in a
    // temporary vector (which would deep-copy the whole subtree).
    void layout_block_dispatch(const parser::BlockNode& block, float& y,
                               float left, float right, int list_depth);

    void layout_heading(const parser::BlockNode& node, float& y, float left, float right);
    void layout_paragraph(const parser::BlockNode& node, float& y, float left, float right);
    void layout_list(const parser::BlockNode& node, float& y, float left, float right, int list_depth);
    void layout_list_item(const parser::BlockNode& node, float& y, float left, float right,
                          int list_depth, bool ordered, int& counter);
    void layout_blockquote(const parser::BlockNode& node, float& y, float left, float right);
    void layout_hr(float& y, float left, float right);
    void layout_code_fence(const parser::BlockNode& node, float& y, float left, float right);
    void layout_table(const parser::BlockNode& node, float& y, float left, float right);

    // Lazy estimate pass: emit a skeleton block + recipe for a flat top-level
    // Paragraph/Heading/CodeFence (returns true). Returns false for everything
    // else so the caller falls back to the eager dispatch.
    bool emit_lazy_block(const parser::BlockNode& block, float& y, float left, float right);
    float code_unit_line_height();

    // Create a text layout from inline nodes, collecting interactive spans
    using TextLayoutResult = InlineLayoutResult;

    TextLayoutResult create_text_layout(const std::vector<parser::InlineNode>& inlines,
                                        float max_width, uint32_t default_color,
                                        IDWriteTextFormat* format = nullptr,
                                        bool force_bold = false,
                                        DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING);

    // Font helpers
    ComPtr<IDWriteTextFormat> get_body_format(float size);
    ComPtr<IDWriteTextFormat> get_code_format(float size);

    // Anchor slug
    std::wstring slugify(const std::vector<parser::InlineNode>& inlines);

    IDWriteFactory* dwrite_;
    const theme::ThemeService& theme_;
    const theme::ColorPalette& colors_;
    const theme::SpacingConfig& spacing_;
    const theme::FontConfig& fonts_;
    WlxCore* core_;
    bool dark_mode_;

    LayoutDocument result_;

    // Cached text formats
    ComPtr<IDWriteTextFormat> body_format_;
    ComPtr<IDWriteTextFormat> code_format_;
    bool wrap_code_ = false;

    // Lazy layout state
    std::shared_ptr<MdMaterializeCtx> md_ctx_;
    bool lazy_ = false;
    float code_unit_lh_ = 0.0f;
};

}  // namespace wlx::runtime::layout
