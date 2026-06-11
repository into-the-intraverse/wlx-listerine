#include "runtime/layout/inline_layout.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

namespace wlx::runtime::layout {

using namespace wlx::runtime::parser;
using namespace wlx::runtime::theme;

InlineLayoutResult build_inline_layout(
    IDWriteFactory* dwrite,
    const std::vector<InlineNode>& inlines,
    float max_width,
    uint32_t /*default_color*/,  // unused — see the header note
    IDWriteTextFormat* format,
    bool force_bold,
    const FontConfig& fonts,
    const ColorPalette& colors,
    DWRITE_TEXT_ALIGNMENT alignment) {

    InlineLayoutResult result;

    // Concatenate all inline text
    std::wstring full_text;
    struct InlineRange {
        size_t start;
        size_t length;
        const InlineNode* node;
    };
    std::vector<InlineRange> ranges;
    ranges.reserve(inlines.size());  // each inline contributes at most one range

    for (auto& n : inlines) {
        if (n.type == InlineType::SoftBreak) {
            size_t start = full_text.size();
            full_text += L' ';
            ranges.push_back({start, 1, &n});
        } else if (n.type == InlineType::HardBreak) {
            size_t start = full_text.size();
            full_text += L'\n';
            ranges.push_back({start, 1, &n});
        } else {
            size_t start = full_text.size();
            full_text += n.text;
            if (!n.text.empty())
                ranges.push_back({start, n.text.size(), &n});
        }
    }

    if (full_text.empty())
        return result;  // result.full_text is already empty-initialized

    // Create text layout from caller-supplied format
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = dwrite->CreateTextLayout(
        full_text.c_str(), static_cast<UINT32>(full_text.size()),
        format, max_width, 100000.0f,
        layout.GetAddressOf());

    if (FAILED(hr) || !layout) return result;

    // Apply per-range formatting
    for (auto& r : ranges) {
        DWRITE_TEXT_RANGE drange = {static_cast<UINT32>(r.start), static_cast<UINT32>(r.length)};

        if (r.node->bold)
            layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, drange);
        if (r.node->italic)
            layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, drange);
        if (r.node->strikethrough)
            layout->SetStrikethrough(TRUE, drange);
        if (r.node->code) {
            layout->SetFontFamilyName(fonts.code_family.c_str(), drange);
            layout->SetFontSize(fonts.code_size, drange);
        }
        if (r.node->link.has_value()) {
            layout->SetUnderline(TRUE, drange);
            result.color_ranges.push_back({drange.startPosition, drange.length, colors.link});
        }
    }

    // Headings bold the whole run. Apply it BEFORE measuring and hit-testing so
    // span/code-background rects land on the final (bold) glyph positions —
    // otherwise bold widens the preceding text and the rects end up shifted left
    // of the code.
    if (force_bold) {
        DWRITE_TEXT_RANGE all = {0, static_cast<UINT32>(full_text.size())};
        layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, all);
    }

    // Center/right table-cell alignment shifts glyph positions too, so it must
    // also be set before the hit-testing below — otherwise the rects stay at
    // their pre-alignment (left-aligned) positions.
    if (alignment != DWRITE_TEXT_ALIGNMENT_LEADING)
        layout->SetTextAlignment(alignment);

    // full_text is not read past this point (the hit-test loop below uses range
    // offsets, not the text) — hand it to the result instead of copying it.
    result.full_text = std::move(full_text);

    // Measure
    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    result.layout = layout;
    result.width = metrics.width;
    result.height = metrics.height;

    // Collect interactive spans for links and background rects for inline code
    for (auto& r : ranges) {
        bool is_link = r.node->link.has_value();
        bool is_code = r.node->code;
        if (!is_link && !is_code) continue;

        UINT32 hit_count = 0;
        layout->HitTestTextRange(
            static_cast<UINT32>(r.start), static_cast<UINT32>(r.length),
            0, 0, nullptr, 0, &hit_count);

        if (hit_count > 0) {
            std::vector<DWRITE_HIT_TEST_METRICS> hits(hit_count);
            layout->HitTestTextRange(
                static_cast<UINT32>(r.start), static_cast<UINT32>(r.length),
                0, 0, hits.data(), hit_count, &hit_count);

            for (auto& h : hits) {
                if (is_link) {
                    InteractiveSpan span;
                    span.target = *r.node->link;
                    span.rect = D2D1::RectF(h.left, h.top, h.left + h.width, h.top + h.height);
                    result.spans.push_back(std::move(span));
                }
                if (is_code) {
                    float pad = 2.0f;
                    CodeBgRect bg;
                    bg.rect = D2D1::RectF(h.left - pad, h.top, h.left + h.width + pad, h.top + h.height);
                    result.code_bg_rects.push_back(bg);
                }
            }
        }
    }

    return result;
}

}  // namespace wlx::runtime::layout
