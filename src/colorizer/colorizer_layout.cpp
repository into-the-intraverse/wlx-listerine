#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "colorizer_layout.h"

#include <algorithm>
#include <cstring>
#include <vector>

// ---- helpers ----------------------------------------------------------------

// Convert a UTF-8 byte sequence to the number of UTF-16 code units it produces.
// Used to map ColorSpan byte offsets (UTF-8) -> wchar_t offsets.
static int utf8_bytes_to_wchar_count(const char* utf8, int byte_count) {
    if (byte_count <= 0) return 0;
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, byte_count, nullptr, 0);
    return len > 0 ? len : byte_count; // fallback: assume ASCII on error
}

// Expand a single source line (wstring) by converting tabs to spaces.
// Returns the expanded line, and fills out_tab_map[i] = expanded offset for source char i.
static std::wstring expand_tabs(const std::wstring& line, int tab_width,
                                 std::vector<int>* out_source_to_expanded) {
    std::wstring result;
    result.reserve(line.size() + 16);
    if (out_source_to_expanded) {
        out_source_to_expanded->reserve(line.size() + 1);
    }
    int col = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        if (out_source_to_expanded) {
            out_source_to_expanded->push_back(col);
        }
        if (line[i] == L'\t') {
            int spaces = tab_width - (col % tab_width);
            for (int s = 0; s < spaces; ++s) result += L' ';
            col += spaces;
        } else {
            result += line[i];
            col++;
        }
    }
    // sentinel: expanded offset just past last char
    if (out_source_to_expanded) {
        out_source_to_expanded->push_back(col);
    }
    return result;
}

// ---- main entry point -------------------------------------------------------

LayoutDocument layout_source(IDWriteFactory* dwrite,
                             const std::wstring& source,
                             const std::string& raw_utf8,
                             const ColorizeResult& colors,
                             const ThemeService& theme,
                             bool dark_mode,
                             float viewport_width,
                             const ColorizerDisplayConfig& display) {
    LayoutDocument doc;
    doc.viewport_width = viewport_width;

    if (!dwrite) return doc;

    const ColorPalette& palette = theme.palette(dark_mode);
    const FontConfig& fonts = theme.fonts();

    // Create code text format
    ComPtr<IDWriteTextFormat> fmt;
    dwrite->CreateTextFormat(
        fonts.code_family.c_str(), nullptr,
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fonts.code_size, L"", fmt.GetAddressOf());
    if (!fmt) return doc;

    float line_height = fonts.code_size * display.line_height_factor;
    fmt->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                        line_height, line_height * 0.8f);
    fmt->SetWordWrapping(display.word_wrap ? DWRITE_WORD_WRAPPING_WRAP
                                           : DWRITE_WORD_WRAPPING_NO_WRAP);

    // ---- split source into lines (wchar) ----
    // Also track byte offset of each line's start in raw_utf8 for span mapping.
    struct LineInfo {
        std::wstring text;       // original (pre-tab-expansion)
        int utf8_byte_start = 0; // byte offset in raw_utf8 where this line begins
    };
    std::vector<LineInfo> lines;
    {
        int byte_pos = 0;
        int wchar_pos = 0;
        int line_start_byte = 0;
        int line_start_wchar = 0;

        // Walk source wchar by wchar, tracking byte offsets via the UTF-8 string
        // Simple approach: the nth wchar in source corresponds to a UTF-8 byte boundary.
        // We scan raw_utf8 for newlines to split, then convert each chunk.
        const char* u8 = raw_utf8.c_str();
        int u8_len = static_cast<int>(raw_utf8.size());

        int chunk_start = 0;
        for (int i = 0; i <= u8_len; ++i) {
            bool is_end = (i == u8_len);
            bool is_newline = (!is_end && u8[i] == '\n');
            if (is_newline || is_end) {
                // Chunk: [chunk_start, i)
                int chunk_len = i - chunk_start;
                // Strip trailing \r
                int effective_len = chunk_len;
                if (effective_len > 0 && u8[chunk_start + effective_len - 1] == '\r')
                    effective_len--;

                LineInfo li;
                li.utf8_byte_start = chunk_start;
                if (effective_len > 0) {
                    int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                                   u8 + chunk_start, effective_len,
                                                   nullptr, 0);
                    if (wlen > 0) {
                        li.text.resize(static_cast<size_t>(wlen));
                        MultiByteToWideChar(CP_UTF8, 0,
                                            u8 + chunk_start, effective_len,
                                            li.text.data(), wlen);
                    }
                }
                lines.push_back(std::move(li));
                chunk_start = i + 1; // skip '\n'
            }
        }
        // If source is empty, add one empty line so we display at least a blank
        if (lines.empty()) {
            LineInfo li;
            li.utf8_byte_start = 0;
            lines.push_back(std::move(li));
        }
    }

    // ---- line number column width ----
    float left_margin = 8.0f;
    float right_margin = 8.0f;
    float ln_col_width = 0.0f;
    ComPtr<IDWriteTextFormat> ln_fmt;

    if (display.line_numbers) {
        dwrite->CreateTextFormat(
            fonts.code_family.c_str(), nullptr,
            DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            fonts.code_size, L"", ln_fmt.GetAddressOf());
        if (ln_fmt) {
            ln_fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            // Measure the widest line number
            wchar_t sample[16];
            int total = static_cast<int>(lines.size());
            _snwprintf_s(sample, _countof(sample), _TRUNCATE, L"%d", total);
            ComPtr<IDWriteTextLayout> sample_layout;
            dwrite->CreateTextLayout(sample, static_cast<UINT32>(wcslen(sample)),
                                     ln_fmt.Get(), 1000.0f, 100.0f,
                                     sample_layout.GetAddressOf());
            if (sample_layout) {
                DWRITE_TEXT_METRICS m;
                sample_layout->GetMetrics(&m);
                ln_col_width = m.width + 16.0f; // padding
            }
        }
    }

    float code_left = left_margin + ln_col_width;
    float code_right = viewport_width - right_margin;
    float max_code_width = std::max(1.0f, code_right - code_left);

    // ---- index color spans by starting line ----
    // ColorSpan start/length are UTF-8 byte offsets in raw_utf8.
    // We need to map them to per-line wchar offsets in the tab-expanded text.

    // Pre-compute cumulative UTF-8 byte start of each line (already stored in LineInfo).
    // Spans are sorted by start; we assign each span to its starting line.
    struct PerLineSpan {
        int line_idx = 0;
        int wchar_start = 0;  // in original (pre-expansion) line text
        int wchar_len = 0;
        uint32_t color = 0;
    };
    std::vector<std::vector<PerLineSpan>> line_spans(lines.size());

    for (const ColorSpan& sp : colors.spans) {
        if (sp.length == 0) continue;
        uint32_t sp_end = sp.start + sp.length;

        // Binary search for the line that contains sp.start
        // lines are in order; find the last line whose utf8_byte_start <= sp.start
        int lo = 0, hi = static_cast<int>(lines.size()) - 1;
        int line_idx = 0;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (static_cast<uint32_t>(lines[mid].utf8_byte_start) <= sp.start) {
                line_idx = mid;
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }

        // Walk through lines until we exhaust the span
        uint32_t remaining_start = sp.start;
        uint32_t remaining_end = sp_end;

        for (int li = line_idx; li < static_cast<int>(lines.size()) && remaining_start < remaining_end; ++li) {
            int line_byte_start = lines[li].utf8_byte_start;
            // byte end of this line's text = next line's byte start (or eof)
            int next_line_byte_start = (li + 1 < static_cast<int>(lines.size()))
                                        ? lines[li + 1].utf8_byte_start
                                        : static_cast<int>(raw_utf8.size()) + 1;
            // Effective end of line content in bytes (excludes the '\n' separator)
            // The line's text is [line_byte_start, next_line_byte_start - 1) (the -1 is '\n')
            int line_content_end_byte = next_line_byte_start - 1; // points to the '\n'
            if (line_content_end_byte < line_byte_start)
                line_content_end_byte = line_byte_start;

            // Clamp the span to this line's content
            uint32_t seg_start = std::max(remaining_start, static_cast<uint32_t>(line_byte_start));
            uint32_t seg_end   = std::min(remaining_end, static_cast<uint32_t>(line_content_end_byte));

            if (seg_start < seg_end) {
                // Convert byte offsets relative to line start -> wchar offsets
                int rel_start_bytes = static_cast<int>(seg_start) - line_byte_start;
                int rel_end_bytes   = static_cast<int>(seg_end)   - line_byte_start;

                const char* line_u8 = raw_utf8.c_str() + line_byte_start;
                int wstart = (rel_start_bytes > 0)
                             ? utf8_bytes_to_wchar_count(line_u8, rel_start_bytes)
                             : 0;
                int wend   = utf8_bytes_to_wchar_count(line_u8, rel_end_bytes);
                int wlen   = wend - wstart;

                if (wlen > 0 && wstart >= 0 &&
                    wstart < static_cast<int>(lines[li].text.size())) {
                    wlen = std::min(wlen, static_cast<int>(lines[li].text.size()) - wstart);
                    PerLineSpan pls;
                    pls.line_idx    = li;
                    pls.wchar_start = wstart;
                    pls.wchar_len   = wlen;
                    pls.color       = sp.color;
                    line_spans[li].push_back(pls);
                }
            }

            remaining_start = static_cast<uint32_t>(next_line_byte_start);
        }
    }

    // ---- build LayoutBlocks ----
    float y = 4.0f; // top padding

    for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
        const std::wstring& orig_line = lines[li].text;

        // Tab-expand
        std::vector<int> source_to_expanded;
        std::wstring expanded = expand_tabs(orig_line, display.tab_width, &source_to_expanded);

        // Map per-line spans from source wchar offsets -> expanded wchar offsets
        std::vector<ColorRange> color_ranges;
        for (const PerLineSpan& pls : line_spans[li]) {
            int exp_start = (pls.wchar_start < static_cast<int>(source_to_expanded.size()))
                            ? source_to_expanded[pls.wchar_start]
                            : static_cast<int>(expanded.size());
            int end_src = pls.wchar_start + pls.wchar_len;
            int exp_end = (end_src < static_cast<int>(source_to_expanded.size()))
                          ? source_to_expanded[end_src]
                          : static_cast<int>(expanded.size());
            int exp_len = exp_end - exp_start;
            if (exp_len > 0) {
                ColorRange cr;
                cr.start  = static_cast<uint32_t>(exp_start);
                cr.length = static_cast<uint32_t>(exp_len);
                cr.color  = pls.color;
                color_ranges.push_back(cr);
            }
        }

        // Create IDWriteTextLayout for this line
        const std::wstring& layout_text = expanded.empty() ? std::wstring(L" ") : expanded;
        ComPtr<IDWriteTextLayout> text_layout;
        dwrite->CreateTextLayout(
            layout_text.c_str(), static_cast<UINT32>(layout_text.size()),
            fmt.Get(), max_code_width, line_height * 2.0f,
            text_layout.GetAddressOf());

        float block_height = line_height;
        if (text_layout) {
            DWRITE_TEXT_METRICS m;
            text_layout->GetMetrics(&m);
            block_height = std::max(line_height, m.height);
        }

        LayoutBlock lb;
        lb.type = BlockType::Paragraph; // closest generic type; renderer handles color ranges
        lb.rect = D2D1::RectF(code_left, y, viewport_width - right_margin, y + block_height);
        lb.background_color = palette.code_bg;
        lb.has_background = false; // no per-line bg; background is painted by host

        TextRun run;
        run.text          = expanded.empty() ? std::wstring() : expanded;
        run.rect          = lb.rect;
        run.layout        = text_layout;
        run.color         = palette.text;
        run.is_code       = true;
        run.color_ranges  = std::move(color_ranges);
        lb.text_runs.push_back(std::move(run));

        // Line number bullet
        if (display.line_numbers && ln_fmt) {
            wchar_t ln_buf[16];
            _snwprintf_s(ln_buf, _countof(ln_buf), _TRUNCATE, L"%d", li + 1);
            lb.bullet_text  = ln_buf;
            lb.bullet_pos   = D2D1::Point2F(left_margin, y);
            lb.bullet_color = palette.muted;
        }

        y += block_height;
        doc.blocks.push_back(std::move(lb));
    }

    y += 4.0f; // bottom padding
    doc.total_height = y;
    return doc;
}
