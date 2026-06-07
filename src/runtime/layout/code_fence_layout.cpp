#include "runtime/layout/code_fence_layout.h"
#include "core_dll/colorizer/colorizer.h"  // ColorSpan / ColorizeResult definitions

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>

using namespace wlx::core::colorizer;

namespace wlx::runtime::layout {

CodeFenceLayoutResult build_code_fence_layout(
    IDWriteFactory* dwrite,
    IDWriteTextFormat* code_format,
    const CodeFenceInput& in,
    const theme::ColorPalette& colors) {

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
            // Convert wstring to UTF-8
            std::string utf8_source;
            utf8_source.reserve(in.code_text.size() * 3);  // upper bound: <=3 bytes/wchar here
            for (wchar_t wc : in.code_text) {
                if (wc < 0x80) {
                    utf8_source += static_cast<char>(wc);
                } else if (wc < 0x800) {
                    utf8_source += static_cast<char>(0xC0 | (wc >> 6));
                    utf8_source += static_cast<char>(0x80 | (wc & 0x3F));
                } else {
                    utf8_source += static_cast<char>(0xE0 | (wc >> 12));
                    utf8_source += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
                    utf8_source += static_cast<char>(0x80 | (wc & 0x3F));
                }
            }

            ColorizeResult cr;
            WlxColorSpan* spans = nullptr;
            uint32_t count = 0;
            if (wlx_core_colorize(in.core, utf8_source.c_str(),
                                  static_cast<uint32_t>(utf8_source.size()),
                                  lang.c_str(), in.dark_mode ? 1 : 0,
                                  0, 0,
                                  &spans, &count) == 0 && count > 0) {
                cr.spans.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    const auto& s = spans[i];
                    ColorSpan cs;
                    cs.start = s.start; cs.length = s.length;
                    cs.color = s.color; cs.bg_color = s.bg_color;
                    cs.has_bg = s.has_bg != 0; cs.modifiers = s.modifiers;
                    cr.spans.push_back(cs);
                }
                wlx_core_free_spans(spans);
            }

            // Build wchar offset lookup table (wchar index -> cumulative byte offset)
            std::vector<uint32_t> wchar_to_byte;
            wchar_to_byte.reserve(in.code_text.size() + 1);  // one per wchar + sentinel
            uint32_t byte_pos = 0;
            for (size_t i = 0; i < in.code_text.size(); i++) {
                wchar_to_byte.push_back(byte_pos);
                wchar_t wc = in.code_text[i];
                if (wc < 0x80) byte_pos += 1;
                else if (wc < 0x800) byte_pos += 2;
                else byte_pos += 3;
            }
            wchar_to_byte.push_back(byte_pos);  // sentinel for end

            // For each color span, find the wchar range. wchar_to_byte is
            // strictly increasing, so "first index whose byte offset >= target"
            // is exactly std::lower_bound — O(log n) instead of the old O(n)
            // scans (which were O(spans * chars) overall on big fences). The
            // sentinel (== total bytes) guarantees a hit for any in-range span.
            for (auto& span : cr.spans) {
                uint32_t span_end = span.start + span.length;

                const auto begin = wchar_to_byte.begin();
                const auto end = wchar_to_byte.end();
                uint32_t wstart = static_cast<uint32_t>(
                    std::lower_bound(begin, end, span.start) - begin);
                uint32_t wend = static_cast<uint32_t>(
                    std::lower_bound(begin + wstart, end, span_end) - begin);

                if (wend > wstart) {
                    color_ranges.push_back({wstart, wend - wstart, span.color, 0, false, span.modifiers});
                }
            }
        }
    }

    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    result.height = metrics.height;
    result.color_ranges = std::move(color_ranges);
    result.layout = layout;

    return result;
}

}  // namespace wlx::runtime::layout
