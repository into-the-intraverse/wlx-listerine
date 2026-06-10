#include "runtime/layout/code_fence_layout.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "wlx_core/text_modifier.h"

#include <algorithm>

namespace wlx::runtime::layout {

Utf8Mapping utf8_with_offsets(const std::wstring& text) {
    Utf8Mapping m;
    m.utf8.reserve(text.size() * 3);              // typical upper bound (pairs: 4 < 2*3)
    m.wchar_to_byte.reserve(text.size() + 1);     // one per wchar + sentinel
    uint32_t byte_pos = 0;
    for (size_t i = 0; i < text.size(); i++) {
        m.wchar_to_byte.push_back(byte_pos);
        uint32_t cp = text[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < text.size() &&
            text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
            // Surrogate pair -> one 4-byte code point (NOT two 3-byte CESU-8
            // units, which tree-sitter would reject as invalid UTF-8). The low
            // unit repeats the pair's start offset so a span boundary at a
            // code-point edge maps to whole pairs.
            cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            i++;
            m.wchar_to_byte.push_back(byte_pos);
        } else if (cp >= 0xD800 && cp < 0xE000) {
            cp = 0xFFFD;  // lone surrogate: invalid UTF-16; emit U+FFFD
        }
        if (cp < 0x80) {
            m.utf8 += static_cast<char>(cp);
            byte_pos += 1;
        } else if (cp < 0x800) {
            m.utf8 += static_cast<char>(0xC0 | (cp >> 6));
            m.utf8 += static_cast<char>(0x80 | (cp & 0x3F));
            byte_pos += 2;
        } else if (cp < 0x10000) {
            m.utf8 += static_cast<char>(0xE0 | (cp >> 12));
            m.utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            m.utf8 += static_cast<char>(0x80 | (cp & 0x3F));
            byte_pos += 3;
        } else {
            m.utf8 += static_cast<char>(0xF0 | (cp >> 18));
            m.utf8 += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            m.utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            m.utf8 += static_cast<char>(0x80 | (cp & 0x3F));
            byte_pos += 4;
        }
    }
    m.wchar_to_byte.push_back(byte_pos);  // sentinel for end
    return m;
}

CodeFenceLayoutResult build_code_fence_layout(
    IDWriteFactory* dwrite,
    IDWriteTextFormat* code_format,
    const CodeFenceInput& in) {

    CodeFenceLayoutResult result;

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    dwrite->CreateTextLayout(in.code_text.c_str(), static_cast<UINT32>(in.code_text.size()),
                             code_format, in.max_width, 100000.0f,
                             layout.GetAddressOf());
    if (!layout) return result;

    // Override wrapping per layout call (code_format default is NO_WRAP)
    if (in.wrap_code)
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    // --- Syntax highlighting ---
    std::vector<ColorRange> color_ranges;
    if (in.core) {
        std::string lang = in.code_language.empty() ? in.default_language : in.code_language;

        if (!lang.empty() && wlx_core_supports(in.core, lang.c_str()) == 1) {
            // UTF-8 source + wchar offset lookup table (wchar index ->
            // cumulative byte offset) in one code-point walk.
            auto map = utf8_with_offsets(in.code_text);

            WlxColorSpan* spans = nullptr;
            uint32_t count = 0;
            if (wlx_core_colorize(in.core, map.utf8.c_str(),
                                  static_cast<uint32_t>(map.utf8.size()),
                                  lang.c_str(), in.dark_mode ? 1 : 0,
                                  0, 0,
                                  &spans, &count) == 0 && count > 0) {
                // For each ABI span, find the wchar range. wchar_to_byte is
                // non-decreasing (surrogate pairs repeat the pair's start), so
                // "first index whose byte offset >= target" is exactly
                // std::lower_bound — O(log n) instead of the old O(n)
                // scans (which were O(spans * chars) overall on big fences). The
                // sentinel (== total bytes) guarantees a hit for any in-range span.
                color_ranges.reserve(count);
                const auto begin = map.wchar_to_byte.begin();
                const auto end = map.wchar_to_byte.end();
                for (uint32_t i = 0; i < count; ++i) {
                    const auto& span = spans[i];
                    uint32_t span_end = span.start + span.length;

                    uint32_t wstart = static_cast<uint32_t>(
                        std::lower_bound(begin, end, span.start) - begin);
                    uint32_t wend = static_cast<uint32_t>(
                        std::lower_bound(begin + wstart, end, span_end) - begin);

                    if (wend > wstart) {
                        color_ranges.push_back({wstart, wend - wstart, span.color,
                                                span.bg_color, span.has_bg != 0,
                                                span.modifiers});
                    }
                }
                wlx_core_free_spans(spans);
            }
        }
    }

    // Apply font modifiers BEFORE measuring: bold/italic change glyph advances,
    // so with wrap_code on styled tokens can rewrap — the measured height must
    // reflect the final styling. paint_text_runs re-applies only the
    // device-bound color brushes per frame, never these.
    for (const auto& cr : color_ranges) {
        if (cr.modifiers == 0) continue;
        DWRITE_TEXT_RANGE range = {cr.start, cr.length};
        if (cr.modifiers & MOD_BOLD)          layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
        if (cr.modifiers & MOD_ITALIC)        layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
        if (cr.modifiers & MOD_UNDERLINE)     layout->SetUnderline(TRUE, range);
        if (cr.modifiers & MOD_STRIKETHROUGH) layout->SetStrikethrough(TRUE, range);
    }

    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    result.height = metrics.height;
    result.color_ranges = std::move(color_ranges);
    result.layout = layout;

    return result;
}

}  // namespace wlx::runtime::layout
