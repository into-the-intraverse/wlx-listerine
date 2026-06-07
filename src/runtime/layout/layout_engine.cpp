#include "runtime/layout/layout_engine.h"
#include "runtime/layout/code_fence_layout.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cwctype>
#include <cmath>

namespace wlx::runtime::layout {

using namespace wlx::runtime::parser;
using namespace wlx::runtime::theme;

static const float kHeadingSizes[] = {28.0f, 24.0f, 20.0f, 17.0f, 14.0f, 12.0f};

LayoutEngine::LayoutEngine(IDWriteFactory* dwrite, const ThemeService& theme, bool dark_mode,
                           WlxCore* core)
    : dwrite_(dwrite)
    , theme_(theme)
    , colors_(theme.palette(dark_mode))
    , spacing_(theme.spacing())
    , fonts_(theme.fonts())
    , core_(core)
    , dark_mode_(dark_mode) {
    // Create cached text formats
    dwrite_->CreateTextFormat(
        fonts_.body_family.c_str(), nullptr,
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fonts_.body_size, L"", body_format_.GetAddressOf());

    dwrite_->CreateTextFormat(
        fonts_.code_family.c_str(), nullptr,
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fonts_.code_size, L"", code_format_.GetAddressOf());

    if (body_format_) {
        body_format_->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                                     fonts_.body_size * spacing_.line_height_factor,
                                     fonts_.body_size * spacing_.line_height_factor * 0.8f);
        body_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }
    if (code_format_) {
        code_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
}

ComPtr<IDWriteTextFormat> LayoutEngine::get_body_format(float size) {
    if (std::abs(size - fonts_.body_size) < 0.1f && body_format_)
        return body_format_;

    ComPtr<IDWriteTextFormat> fmt;
    dwrite_->CreateTextFormat(
        fonts_.body_family.c_str(), nullptr,
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        size, L"", fmt.GetAddressOf());
    if (fmt)
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    return fmt;
}

ComPtr<IDWriteTextFormat> LayoutEngine::get_code_format(float size) {
    if (std::abs(size - fonts_.code_size) < 0.1f && code_format_)
        return code_format_;

    ComPtr<IDWriteTextFormat> fmt;
    dwrite_->CreateTextFormat(
        fonts_.code_family.c_str(), nullptr,
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        size, L"", fmt.GetAddressOf());
    if (fmt)
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return fmt;
}

// ---------- slugify ----------

std::wstring LayoutEngine::slugify(const std::vector<InlineNode>& inlines) {
    // Filter characters straight into the slug — same per-character mapping the
    // old two-pass version applied (break -> space -> '-'), without the
    // intermediate concatenated `text` wstring.
    std::wstring slug;
    auto emit = [&](wchar_t c) {
        if (c == L' ' || c == L'\t')
            slug += L'-';
        else if (iswalnum(c) || c == L'-' || c == L'_')
            slug += static_cast<wchar_t>(towlower(c));
    };
    for (auto& n : inlines) {
        if (n.type == InlineType::SoftBreak || n.type == InlineType::HardBreak)
            emit(L' ');
        else
            for (wchar_t c : n.text) emit(c);
    }

    // Remove leading/trailing hyphens
    while (!slug.empty() && slug.front() == L'-') slug.erase(slug.begin());
    while (!slug.empty() && slug.back() == L'-') slug.pop_back();

    return slug;
}

// ---------- create_text_layout ----------

LayoutEngine::TextLayoutResult LayoutEngine::create_text_layout(
    const std::vector<InlineNode>& inlines, float max_width, uint32_t default_color,
    IDWriteTextFormat* format, bool force_bold) {
    IDWriteTextFormat* fmt = format ? format : body_format_.Get();
    return build_inline_layout(dwrite_, inlines, max_width, default_color, fmt,
                               force_bold, fonts_, colors_);
}

// ---------- layout entry point ----------

LayoutDocument LayoutEngine::layout(const Document& doc, float viewport_width, bool wrap_code,
                                    float gutter_width) {
    result_ = LayoutDocument{};
    result_.viewport_width = viewport_width;
    result_.gutter_width = gutter_width;
    wrap_code_ = wrap_code;

    float content_padding = 16.0f;
    float y = content_padding;
    float left = (gutter_width > 0.0f) ? gutter_width : content_padding;
    float right = viewport_width - content_padding;

    layout_blocks(doc.blocks, y, left, right, 0);

    result_.total_height = y + content_padding;
    return std::move(result_);
}

void LayoutEngine::layout_block_dispatch(const BlockNode& block, float& y,
                                          float left, float right, int list_depth) {
    switch (block.type) {
    case BlockType::Heading:
        layout_heading(block, y, left, right);
        break;
    case BlockType::Paragraph:
        layout_paragraph(block, y, left, right);
        break;
    case BlockType::List:
        layout_list(block, y, left, right, list_depth);
        break;
    case BlockType::BlockQuote:
        layout_blockquote(block, y, left, right);
        break;
    case BlockType::HorizontalRule:
        layout_hr(y, left, right);
        break;
    case BlockType::CodeFence:
        layout_code_fence(block, y, left, right);
        break;
    case BlockType::Table:
        layout_table(block, y, left, right);
        break;
    default:
        // ListItem, TaskList, TableRow, TableCell handled by parent
        break;
    }
}

void LayoutEngine::layout_blocks(const std::vector<BlockNode>& blocks, float& y,
                                  float left, float right, int list_depth) {
    for (auto& block : blocks)
        layout_block_dispatch(block, y, left, right, list_depth);
}

// ---------- heading ----------

void LayoutEngine::layout_heading(const BlockNode& node, float& y, float left, float right) {
    y += spacing_.heading_spacing_above;

    int level = std::clamp(node.heading_level, 1, 6);
    float font_size = kHeadingSizes[level - 1];

    auto fmt = get_body_format(font_size);
    if (!fmt) return;

    float max_width = right - left;
    // force_bold=true bolds the whole heading inside create_text_layout, before
    // it measures and computes span/code-background rects — so those rects match
    // the bold glyph positions (height/width also reflect the bold layout).
    auto tlr = create_text_layout(node.inlines, max_width, colors_.heading, fmt.Get(),
                                  /*force_bold=*/true);
    if (!tlr.layout) return;

    LayoutBlock lb;
    lb.type = BlockType::Heading;
    lb.heading_level = level;
    lb.rect = D2D1::RectF(left, y, right, y + tlr.height);

    TextRun run;
    run.text = std::move(tlr.full_text);
    run.rect = lb.rect;
    run.layout = tlr.layout;
    run.color = colors_.heading;
    run.color_ranges = std::move(tlr.color_ranges);
    run.code_bg_rects = std::move(tlr.code_bg_rects);
    lb.text_runs.push_back(std::move(run));

    // Offset interactive span rects to document coordinates and append.
    for (auto& s : tlr.spans) {
        s.rect.left  += left;
        s.rect.right += left;
        s.rect.top   += y;
        s.rect.bottom += y;
        lb.spans.push_back(std::move(s));
    }

    // H1/H2 bottom border
    if (level <= 2) {
        lb.has_bottom_rule = true;
        lb.bottom_rule_color = colors_.rule;
    }

    // Anchor
    std::wstring slug = slugify(node.inlines);
    if (!slug.empty()) {
        result_.anchors.push_back({slug, y});
    }

    y += tlr.height + spacing_.heading_spacing_below;
    result_.blocks.push_back(std::move(lb));
}

// ---------- paragraph ----------

void LayoutEngine::layout_paragraph(const BlockNode& node, float& y, float left, float right) {
    float max_width = right - left;
    auto tlr = create_text_layout(node.inlines, max_width, colors_.text);
    if (!tlr.layout) return;

    LayoutBlock lb;
    lb.type = BlockType::Paragraph;
    lb.rect = D2D1::RectF(left, y, right, y + tlr.height);

    TextRun run;
    run.text = std::move(tlr.full_text);
    run.rect = lb.rect;
    run.layout = tlr.layout;
    run.color = colors_.text;
    run.color_ranges = std::move(tlr.color_ranges);
    run.code_bg_rects = std::move(tlr.code_bg_rects);
    lb.text_runs.push_back(std::move(run));

    // Offset interactive span rects to document coordinates
    for (auto& s : tlr.spans) {
        s.rect.left += left;
        s.rect.right += left;
        s.rect.top += y;
        s.rect.bottom += y;
        lb.spans.push_back(std::move(s));
    }

    y += tlr.height + spacing_.paragraph_spacing;
    result_.blocks.push_back(std::move(lb));
}

// ---------- list ----------

void LayoutEngine::layout_list(const BlockNode& node, float& y, float left, float right,
                                int list_depth) {
    bool ordered = (node.list_style == ListStyle::Ordered);
    int counter = node.list_start;

    for (auto& child : node.children) {
        if (child.type == BlockType::ListItem || child.type == BlockType::TaskList) {
            layout_list_item(child, y, left, right, list_depth, ordered, counter);
        }
    }
}

void LayoutEngine::layout_list_item(const BlockNode& node, float& y, float left, float right,
                                     int list_depth, bool ordered, int& counter) {
    float indent = left + spacing_.list_indent * (list_depth + 1);
    float bullet_x = indent - spacing_.list_indent * 0.75f;

    // Bullet/number text
    std::wstring bullet;
    if (node.type == BlockType::TaskList) {
        bullet = node.task_checked ? L"\u2611 " : L"\u2610 ";
    } else if (ordered) {
        bullet = std::to_wstring(counter) + L". ";
        counter++;
    } else {
        bullet = L"\u2022 ";
    }

    // For loose lists, node.inlines is empty and content is in child Paragraphs.
    // Use the first child paragraph's inlines for the bullet line.
    const std::vector<InlineNode>* first_inlines = &node.inlines;
    size_t first_child_skip = 0;
    if (node.inlines.empty()) {
        for (size_t i = 0; i < node.children.size(); i++) {
            if (node.children[i].type == BlockType::Paragraph) {
                first_inlines = &node.children[i].inlines;
                first_child_skip = i + 1;
                break;
            }
        }
    }

    // Layout inline content
    float max_width = right - indent;
    auto tlr = create_text_layout(*first_inlines, max_width, colors_.text);

    float item_height = tlr.layout ? tlr.height : fonts_.body_size;

    LayoutBlock lb;
    lb.type = node.type;
    lb.rect = D2D1::RectF(left, y, right, y + item_height);
    lb.bullet_text = bullet;
    lb.bullet_pos = D2D1::Point2F(bullet_x, y);
    lb.bullet_color = colors_.text;

    if (tlr.layout) {
        TextRun run;
        run.text = std::move(tlr.full_text);
        run.rect = D2D1::RectF(indent, y, right, y + tlr.height);
        run.layout = tlr.layout;
        run.color = colors_.text;
        run.color_ranges = std::move(tlr.color_ranges);
        run.code_bg_rects = std::move(tlr.code_bg_rects);
        lb.text_runs.push_back(std::move(run));

        for (auto& s : tlr.spans) {
            s.rect.left += indent;
            s.rect.right += indent;
            s.rect.top += y;
            s.rect.bottom += y;
            lb.spans.push_back(std::move(s));
        }
    }

    y += item_height + spacing_.paragraph_spacing * 0.5f;
    result_.blocks.push_back(std::move(lb));

    // Layout children (nested lists and continuation paragraphs)
    for (size_t i = (first_child_skip > 0 ? first_child_skip : 0); i < node.children.size(); i++) {
        auto& child = node.children[i];
        if (child.type == BlockType::List)
            layout_list(child, y, left, right, list_depth + 1);
        else if (child.type == BlockType::Paragraph)
            layout_paragraph(child, y, indent, right);
    }
}

// ---------- blockquote ----------

void LayoutEngine::layout_blockquote(const BlockNode& node, float& y, float left, float right) {
    float quote_left = left + spacing_.quote_indent;
    float border_x = left + spacing_.quote_border_width;

    float start_y = y;

    // Layout children (paragraphs, etc.)
    for (auto& child : node.children) {
        switch (child.type) {
        case BlockType::Paragraph:
            layout_paragraph(child, y, quote_left, right);
            break;
        case BlockType::BlockQuote:
            layout_blockquote(child, y, quote_left, right);
            break;
        default:
            layout_block_dispatch(child, y, quote_left, right, 0);
            break;
        }
    }

    // Add a container block with left border
    LayoutBlock lb;
    lb.type = BlockType::BlockQuote;
    lb.rect = D2D1::RectF(left, start_y, right, y);
    lb.has_left_border = true;
    lb.left_border_color = colors_.quote_border;
    result_.blocks.push_back(std::move(lb));
}

// ---------- horizontal rule ----------

void LayoutEngine::layout_hr(float& y, float left, float right) {
    float rule_height = 2.0f;
    y += spacing_.paragraph_spacing;

    LayoutBlock lb;
    lb.type = BlockType::HorizontalRule;
    lb.rect = D2D1::RectF(left, y, right, y + rule_height);
    lb.has_bottom_rule = true;
    lb.bottom_rule_color = colors_.rule;

    y += rule_height + spacing_.paragraph_spacing;
    result_.blocks.push_back(std::move(lb));
}

// ---------- code fence ----------

void LayoutEngine::layout_code_fence(const BlockNode& node, float& y, float left, float right) {
    float padding = spacing_.code_padding;

    // Concatenate code text
    std::wstring code_text;
    for (auto& n : node.inlines)
        code_text += n.text;

    // Remove trailing newline if present
    if (!code_text.empty() && code_text.back() == L'\n')
        code_text.pop_back();

    CodeFenceInput in;
    in.code_text = code_text;
    for (wchar_t wc : node.code_language) in.code_language += static_cast<char>(wc);
    in.default_language = theme_.config().code_default_language;
    in.max_width = right - left - padding * 2;
    in.wrap_code = wrap_code_;
    in.dark_mode = dark_mode_;
    in.core = core_;
    auto res = build_code_fence_layout(dwrite_, code_format_.Get(), in, colors_);
    if (!res.layout) return;

    float block_height = res.height + padding * 2;
    LayoutBlock lb;
    lb.type = BlockType::CodeFence;
    lb.rect = D2D1::RectF(left, y, right, y + block_height);
    lb.has_background = true;
    lb.background_color = colors_.code_bg;
    TextRun run;
    run.text = code_text;
    run.rect = D2D1::RectF(left + padding, y + padding, right - padding, y + padding + res.height);
    run.layout = res.layout;
    run.color = colors_.text;
    run.is_code = true;
    run.color_ranges = std::move(res.color_ranges);
    lb.text_runs.push_back(std::move(run));
    y += block_height + spacing_.paragraph_spacing;
    result_.blocks.push_back(std::move(lb));
}

// ---------- table ----------

void LayoutEngine::layout_table(const BlockNode& node, float& y, float left, float right) {
    if (node.children.empty()) return;

    int col_count = node.table_columns > 0 ? node.table_columns : 1;
    float table_width = right - left;
    float col_width = table_width / static_cast<float>(col_count);

    float start_y = y;

    for (auto& row : node.children) {
        if (row.type != BlockType::TableRow) continue;

        float row_height = 0;
        std::vector<TextLayoutResult> cell_layouts;

        int col = 0;
        for (auto& cell : row.children) {
            if (cell.type != BlockType::TableCell || col >= col_count) break;

            float cell_width = col_width - spacing_.code_padding * 2;
            auto tlr = create_text_layout(cell.inlines, cell_width, colors_.text);

            // Bold header cells
            if (cell.is_header && tlr.layout) {
                DWRITE_TEXT_RANGE all = {0, static_cast<UINT32>(tlr.full_text.size())};
                tlr.layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, all);
                // Re-measure after bolding
                DWRITE_TEXT_METRICS m;
                tlr.layout->GetMetrics(&m);
                tlr.height = m.height;
            }

            // Apply column alignment
            if (tlr.layout) {
                if (cell.cell_align == BlockNode::CellAlign::Center)
                    tlr.layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                else if (cell.cell_align == BlockNode::CellAlign::Right)
                    tlr.layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            }

            if (tlr.height > row_height) row_height = tlr.height;
            cell_layouts.push_back(std::move(tlr));
            col++;
        }

        if (row_height < fonts_.body_size)
            row_height = fonts_.body_size;

        row_height += spacing_.code_padding * 2;

        // Emit cell blocks
        col = 0;
        for (auto& tlr : cell_layouts) {
            float cell_left = left + col * col_width;
            float cell_x = cell_left + spacing_.code_padding;
            float cell_y = y + spacing_.code_padding;

            LayoutBlock lb;
            lb.type = BlockType::TableCell;
            lb.rect = D2D1::RectF(cell_left, y, cell_left + col_width, y + row_height);
            lb.has_bottom_rule = true;
            lb.bottom_rule_color = colors_.rule;

            if (tlr.layout) {
                TextRun run;
                run.text = std::move(tlr.full_text);
                run.rect = D2D1::RectF(cell_x, cell_y,
                                       cell_x + (col_width - spacing_.code_padding * 2),
                                       cell_y + tlr.height);
                run.layout = tlr.layout;
                run.color = colors_.text;
                run.color_ranges = std::move(tlr.color_ranges);
                run.code_bg_rects = std::move(tlr.code_bg_rects);
                lb.text_runs.push_back(std::move(run));
            }

            result_.blocks.push_back(std::move(lb));
            col++;
        }

        y += row_height;
    }

    y += spacing_.paragraph_spacing;
}

}  // namespace wlx::runtime::layout
