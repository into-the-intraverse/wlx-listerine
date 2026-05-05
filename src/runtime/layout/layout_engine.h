#pragma once

#include "runtime/layout/code_bg_rect.h"
#include "runtime/layout/color_range.h"
#include "runtime/layout/interactive_span.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/text_position.h"
#include "runtime/parser/document_model.h"
#include "runtime/theme/theme_service.h"
#include "wlx_core/abi.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

std::wstring extract_selected_text(const LayoutDocument& layout,
                                   TextPosition start, TextPosition end);

std::pair<int, int> find_word_boundaries(const std::wstring& text, int offset);

class LayoutEngine {
public:
    LayoutEngine(IDWriteFactory* dwrite, const ThemeService& theme, bool dark_mode,
                 WlxCore* core = nullptr);

    LayoutDocument layout(const Document& doc, float viewport_width, bool wrap_code = false);

private:
    void layout_blocks(const std::vector<BlockNode>& blocks, float& y,
                       float left, float right, int list_depth);

    void layout_heading(const BlockNode& node, float& y, float left, float right);
    void layout_paragraph(const BlockNode& node, float& y, float left, float right);
    void layout_list(const BlockNode& node, float& y, float left, float right, int list_depth);
    void layout_list_item(const BlockNode& node, float& y, float left, float right,
                          int list_depth, bool ordered, int& counter);
    void layout_blockquote(const BlockNode& node, float& y, float left, float right);
    void layout_hr(float& y, float left, float right);
    void layout_code_fence(const BlockNode& node, float& y, float left, float right);
    void layout_table(const BlockNode& node, float& y, float left, float right);

    // Create a text layout from inline nodes, collecting interactive spans
    struct TextLayoutResult {
        ComPtr<IDWriteTextLayout> layout;
        std::wstring full_text;
        std::vector<InteractiveSpan> spans;
        std::vector<ColorRange> color_ranges;
        std::vector<CodeBgRect> code_bg_rects;
        float width = 0;
        float height = 0;
    };

    TextLayoutResult create_text_layout(const std::vector<InlineNode>& inlines,
                                        float max_width, uint32_t default_color);

    // Font helpers
    ComPtr<IDWriteTextFormat> get_body_format(float size);
    ComPtr<IDWriteTextFormat> get_code_format(float size);

    // Anchor slug
    std::wstring slugify(const std::vector<InlineNode>& inlines);

    IDWriteFactory* dwrite_;
    const ThemeService& theme_;
    const ColorPalette& colors_;
    const SpacingConfig& spacing_;
    const FontConfig& fonts_;
    WlxCore* core_;
    bool dark_mode_;

    LayoutDocument result_;

    // Cached text formats
    ComPtr<IDWriteTextFormat> body_format_;
    ComPtr<IDWriteTextFormat> code_format_;
    bool wrap_code_ = false;
};
