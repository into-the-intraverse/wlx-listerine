#pragma once

#include "document_model.h"
#include "theme_service.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct ColorRange {
    uint32_t start = 0;
    uint32_t length = 0;
    uint32_t color = 0;
};

struct TextRun {
    std::wstring text;
    D2D1_RECT_F rect = {};
    ComPtr<IDWriteTextLayout> layout;
    uint32_t color = 0;
    bool is_code = false;
    std::vector<ColorRange> color_ranges;  // per-range color overrides (links, etc.)
};

struct InteractiveSpan {
    LinkTarget target;
    D2D1_RECT_F rect = {};
};

struct AnchorEntry {
    std::wstring slug;
    float y_offset = 0;
};

struct LayoutBlock {
    BlockType type = BlockType::Paragraph;
    D2D1_RECT_F rect = {};
    std::vector<TextRun> text_runs;
    std::vector<InteractiveSpan> spans;

    // Heading
    int heading_level = 0;

    // Decorations
    bool has_left_border = false;
    uint32_t left_border_color = 0;
    bool has_bottom_rule = false;
    uint32_t bottom_rule_color = 0;
    uint32_t background_color = 0;
    bool has_background = false;

    // Bullet/number prefix
    std::wstring bullet_text;
    D2D1_POINT_2F bullet_pos = {};
    uint32_t bullet_color = 0;
};

struct LayoutDocument {
    std::vector<LayoutBlock> blocks;
    std::vector<AnchorEntry> anchors;
    float total_height = 0;
    float viewport_width = 0;
};

class LayoutEngine {
public:
    LayoutEngine(IDWriteFactory* dwrite, const ThemeService& theme, bool dark_mode);

    LayoutDocument layout(const Document& doc, float viewport_width);

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

    LayoutDocument result_;

    // Cached text formats
    ComPtr<IDWriteTextFormat> body_format_;
    ComPtr<IDWriteTextFormat> code_format_;
};
