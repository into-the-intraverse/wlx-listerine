#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "plugin_colorizer/layout/colorizer_layout.h"
#include "runtime/parser/block_node.h"
#include "runtime/interaction/url_scanner.h"
#include "runtime/layout/interactive_span.h"
#include "runtime/layout/line_index.h"
#include "runtime/layout/utf8_offset_map.h"
#include "wlx_core/text_modifier.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <vector>

using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;
using namespace wlx::runtime::theme;
using Microsoft::WRL::ComPtr;

namespace wlx::plugin_colorizer::layout {

// ---- helpers ----------------------------------------------------------------

// The wchar -> UTF-8 byte-offset prefix table for one decoded line comes from
// the shared utf8_offsets/byte_to_wchar primitives (runtime/layout/
// utf8_offset_map.h). Built ONCE per non-ASCII line so mapping ColorSpan byte
// offsets -> wchar offsets is O(log n) per span instead of a
// MultiByteToWideChar prefix conversion (O(line length)) per span end — the
// old per-span conversion was a multi-second hang on long minified
// single-line files.

// Expand a single source line (wstring) by converting tabs to spaces.
// Returns the expanded line, and fills out_tab_map[i] = expanded offset for source char i.
std::wstring expand_tabs(const std::wstring& line, int tab_width,
                         std::vector<int>* out_source_to_expanded) {
    // tab_width comes from unvalidated TOML config: 0 would divide-by-zero in
    // the column math below and negatives produce garbage, so clamp here.
    if (tab_width < 1) tab_width = 1;
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

// Decode one line's UTF-8 byte slice [line_byte_start, line_content_end_byte)
// into the original (pre-tab-expansion) wstring, stripping a trailing '\r'.
// Mirrors the per-line decode in layout_source's line-splitting loop so the
// incremental (apply_spans_to_range) path produces byte-identical line text.
std::wstring decode_line(const std::string& raw_utf8,
                         int line_byte_start, int line_content_end_byte) {
    int effective_len = line_content_end_byte - line_byte_start;
    if (effective_len > 0 &&
        raw_utf8[static_cast<size_t>(line_byte_start + effective_len - 1)] == '\r')
        effective_len--;
    std::wstring text;
    if (effective_len > 0) {
        const char* u8 = raw_utf8.c_str() + line_byte_start;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, u8, effective_len, nullptr, 0);
        if (wlen > 0) {
            text.resize(static_cast<size_t>(wlen));
            MultiByteToWideChar(CP_UTF8, 0, u8, effective_len, text.data(), wlen);
        }
    }
    return text;
}

// PerLineSpan (a color span clamped to one line, in source-wchar offsets) is
// declared in colorizer_layout.h — produced by clamp_span_to_line below.

// Clamp one color span (UTF-8 byte offsets in raw_utf8) to the byte range of a
// single line and convert it to per-line wchar offsets. Appends a PerLineSpan to
// `out` when the span overlaps the line's content. `orig_line` is the decoded
// (pre-expansion) line text, used only to bound wlen; `wchar_to_byte` is its
// prefix table (nullptr for a pure-ASCII line).
static void clamp_span_to_line(int line_byte_start,
                               const std::wstring& orig_line,
                               const std::vector<uint32_t>* wchar_to_byte,
                               uint32_t seg_start, uint32_t seg_end,
                               const wlx::core::colorizer::ColorSpan& sp,
                               std::vector<PerLineSpan>& out) {
    if (seg_start >= seg_end) return;
    int rel_start_bytes = static_cast<int>(seg_start) - line_byte_start;
    int rel_end_bytes   = static_cast<int>(seg_end)   - line_byte_start;

    int wstart = (rel_start_bytes > 0)
                 ? static_cast<int>(byte_to_wchar(
                       wchar_to_byte, static_cast<uint32_t>(rel_start_bytes)))
                 : 0;
    int wend   = static_cast<int>(byte_to_wchar(
                     wchar_to_byte, static_cast<uint32_t>(rel_end_bytes)));
    int wlen   = wend - wstart;

    if (wlen > 0 && wstart >= 0 && wstart < static_cast<int>(orig_line.size())) {
        wlen = std::min(wlen, static_cast<int>(orig_line.size()) - wstart);
        PerLineSpan pls;
        pls.wchar_start = wstart;
        pls.wchar_len   = wlen;
        pls.color       = sp.color;
        pls.bg_color    = sp.bg_color;
        pls.has_bg      = sp.has_bg;
        pls.modifiers   = sp.modifiers;
        out.push_back(pls);
    }
}

// Walk `spans` (UTF-8 byte offsets in raw_utf8) and assign each to the line(s)
// it covers, appending PerLineSpans to out_line_spans[line - line_first]. Only
// lines whose index is in [line_first, line_last] receive spans; out_line_spans
// is indexed WINDOW-RELATIVE and must be sized line_last - line_first + 1. Pass
// [0, line_count-1] for the whole document. `line_byte_starts[i]` is the byte
// start of line i; `raw_utf8_size` is the total source length for the last
// line's content-end. Shared by layout_source (whole-doc) and apply_spans_to_range
// (a byte window) so both produce identical per-line mappings.
void distribute_spans_to_lines(
    const std::string& raw_utf8,
    const std::vector<int>& line_byte_starts,
    int raw_utf8_size,
    const wlx::core::colorizer::ColorizeResult& spans,
    int line_first, int line_last,
    std::vector<std::vector<PerLineSpan>>& out_line_spans) {
    const int line_count = static_cast<int>(line_byte_starts.size());
    if (line_count == 0) return;

    // Per in-window line state (window-relative, like out_line_spans), computed
    // at most once on first span hit: the decoded line text (clamp_span_to_line
    // only needs its wchar length to bound wlen) and its byte->wchar prefix
    // table — left empty for pure-ASCII lines, where byte offsets == wchar
    // offsets and no table is needed.
    const size_t window = static_cast<size_t>(line_last - line_first + 1);
    std::vector<std::wstring> decoded(window);
    std::vector<std::vector<uint32_t>> byte_tables(window);
    std::vector<char> decoded_done(window, 0);
    auto line_text = [&](size_t wi, int line_byte_start, int content_end) -> const std::wstring& {
        if (!decoded_done[wi]) {
            decoded[wi] = decode_line(raw_utf8, line_byte_start, content_end);
            bool ascii = true;
            for (wchar_t wc : decoded[wi]) {
                if (wc >= 0x80) { ascii = false; break; }
            }
            if (!ascii) byte_tables[wi] = utf8_offsets(decoded[wi]);
            decoded_done[wi] = 1;
        }
        return decoded[wi];
    };

    for (const wlx::core::colorizer::ColorSpan& sp : spans.spans) {
        if (sp.length == 0) continue;
        uint32_t sp_end = sp.start + sp.length;

        // Binary search: last line whose byte start <= sp.start.
        int lo = 0, hi = line_count - 1;
        int line_idx = 0;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (static_cast<uint32_t>(line_byte_starts[mid]) <= sp.start) {
                line_idx = mid;
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }

        // Jump straight to the first in-window line the span can touch: for a
        // span beginning before line_first, seg_start clamps to that line's
        // byte start, so skipping the pre-window lines is exact. Lines past
        // line_last never emit, so stop there too.
        const int li0 = std::max(line_idx, line_first);
        uint32_t remaining_start = std::max(
            sp.start, static_cast<uint32_t>(line_byte_starts[li0]));
        const uint32_t remaining_end = sp_end;

        for (int li = li0; li <= line_last && remaining_start < remaining_end; ++li) {
            int line_byte_start = line_byte_starts[li];
            int next_line_byte_start = (li + 1 < line_count)
                                        ? line_byte_starts[li + 1]
                                        : raw_utf8_size + 1;
            int line_content_end_byte = next_line_byte_start - 1; // the '\n'
            if (line_content_end_byte < line_byte_start)
                line_content_end_byte = line_byte_start;

            uint32_t seg_start = std::max(remaining_start,
                                          static_cast<uint32_t>(line_byte_start));
            uint32_t seg_end   = std::min(remaining_end,
                                          static_cast<uint32_t>(line_content_end_byte));
            const size_t wi = static_cast<size_t>(li - line_first);
            const std::wstring& orig_line =
                line_text(wi, line_byte_start, line_content_end_byte);
            const std::vector<uint32_t>* table =
                byte_tables[wi].empty() ? nullptr : &byte_tables[wi];
            clamp_span_to_line(line_byte_start, orig_line, table,
                               seg_start, seg_end, sp, out_line_spans[wi]);

            remaining_start = static_cast<uint32_t>(next_line_byte_start);
        }
    }
}

// Map a line's PerLineSpans (source-wchar offsets) onto tab-expanded wchar
// offsets and emit ColorRanges. `source_to_expanded` is the tab-expansion map
// from expand_tabs; `expanded` is the tab-expanded line (for end clamping).
std::vector<ColorRange> build_color_ranges(
    const std::vector<PerLineSpan>& line_spans,
    const std::vector<int>& source_to_expanded,
    const std::wstring& expanded) {
    std::vector<ColorRange> ranges;
    for (const PerLineSpan& pls : line_spans) {
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
            cr.start     = static_cast<uint32_t>(exp_start);
            cr.length    = static_cast<uint32_t>(exp_len);
            cr.color     = pls.color;
            cr.bg_color  = pls.bg_color;
            cr.has_bg    = pls.has_bg;
            cr.modifiers = pls.modifiers;
            ranges.push_back(cr);
        }
    }
    return ranges;
}

// ---- lazy materialization ---------------------------------------------------

// MaterializeCtx (per-document build context) is declared in colorizer_layout.h
// so grid_window.cpp shares it. It is heap-allocated and captured by the
// LayoutDocument::materialize_block closure so it outlives layout_source().

// Build the IDWriteTextLayout for one line and report its measured height
// (== line_height for the no-wrap grid; can exceed it when word-wrap is on).
// Syntax font modifiers from `color_ranges` are applied here, BEFORE
// GetMetrics, so the measured height reflects the final styling (bold can
// rewrap when word-wrap is on) and paint never mutates the layout's fonts.
ComPtr<IDWriteTextLayout> create_line_layout(
    const std::wstring& expanded, const std::vector<ColorRange>& color_ranges,
    const MaterializeCtx& ctx, float& out_height) {
    // Both ternary operands must be the same lvalue type, else the result is a
    // prvalue and this "const ref" silently copies `expanded` on every line.
    static const std::wstring kSpacePlaceholder = L" ";
    const std::wstring& layout_text = expanded.empty() ? kSpacePlaceholder : expanded;
    ComPtr<IDWriteTextLayout> text_layout;
    if (ctx.dwrite) {
        ctx.dwrite->CreateTextLayout(
            layout_text.c_str(), static_cast<UINT32>(layout_text.size()),
            ctx.fmt.Get(), ctx.max_code_width, ctx.line_height * 2.0f,
            text_layout.GetAddressOf());
    }
    out_height = ctx.line_height;
    if (text_layout) {
        for (const ColorRange& cr : color_ranges) {
            if (cr.modifiers == 0) continue;
            DWRITE_TEXT_RANGE range = {cr.start, cr.length};
            if (cr.modifiers & MOD_BOLD)          text_layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
            if (cr.modifiers & MOD_ITALIC)        text_layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
            if (cr.modifiers & MOD_UNDERLINE)     text_layout->SetUnderline(TRUE, range);
            if (cr.modifiers & MOD_STRIKETHROUGH) text_layout->SetStrikethrough(TRUE, range);
        }
        DWRITE_TEXT_METRICS m;
        text_layout->GetMetrics(&m);
        out_height = std::max(ctx.line_height, m.height);
    }
    return text_layout;
}

// Compute the viewport-only decorations for one already-laid-out line: URL link
// color range + interactive hit rects, whitespace markers, indent guides, and
// the trailing-whitespace highlight. Requires lb.text_runs[0].layout to be set
// and lb.rect to hold the block's final position. Mirrors the original per-line
// tail so the lazy and eager paths are pixel-identical.
void apply_line_decorations(
    LayoutBlock& lb,
    const std::wstring& expanded,
    const std::vector<int>& source_to_expanded,
    const std::wstring& orig_line,
    const MaterializeCtx& ctx) {
    if (lb.text_runs.empty()) return;
    auto& run = lb.text_runs[0];
    IDWriteTextLayout* text_layout = run.layout.Get();
    if (!text_layout) return;

    // ---- URL detection ----
    // Scan the expanded line for URLs; emit a ColorRange overriding foreground
    // to link color + underline, plus one InteractiveSpan per pixel-row covered.
    if (!expanded.empty() && expanded.find(L':') != std::wstring::npos) {
        auto url_matches = wlx::runtime::interaction::scan_urls(expanded);
        for (const auto& um : url_matches) {
            int len = um.end - um.start;
            if (len <= 0) continue;

            wlx::runtime::layout::ColorRange link_cr;
            link_cr.start     = static_cast<uint32_t>(um.start);
            link_cr.length    = static_cast<uint32_t>(len);
            link_cr.color     = ctx.link_color;
            link_cr.has_bg    = false;
            link_cr.modifiers = MOD_UNDERLINE;
            run.color_ranges.push_back(link_cr);
            // Decorations live on the layout (paint only re-applies brushes),
            // and URL ranges are appended after create_line_layout ran — set
            // the underline here.
            DWRITE_TEXT_RANGE url_range = {link_cr.start, link_cr.length};
            text_layout->SetUnderline(TRUE, url_range);

            UINT32 hit_count = 0;
            text_layout->HitTestTextRange(
                static_cast<UINT32>(um.start), static_cast<UINT32>(len),
                0, 0, nullptr, 0, &hit_count);
            if (hit_count > 0) {
                std::vector<DWRITE_HIT_TEST_METRICS> hits(hit_count);
                text_layout->HitTestTextRange(
                    static_cast<UINT32>(um.start), static_cast<UINT32>(len),
                    0, 0, hits.data(), hit_count, &hit_count);

                std::wstring url(expanded.data() + um.start, static_cast<size_t>(len));
                for (auto& h : hits) {
                    wlx::runtime::layout::InteractiveSpan span;
                    span.target.kind = wlx::runtime::parser::LinkKind::ExternalUrl;
                    span.target.url  = url;
                    // Offset local layout rects to document coords.
                    span.rect = D2D1::RectF(h.left + ctx.code_left,
                                            h.top + lb.rect.top,
                                            h.left + ctx.code_left + h.width,
                                            h.top + lb.rect.top + h.height);
                    lb.spans.push_back(std::move(span));
                }
            }
        }
    }

    // ---- Whitespace markers ----
    if (ctx.show_whitespace != ShowWhitespace::None && !orig_line.empty()) {
        lb.ws_marker_color = ctx.muted_color;
        int leading_end = 0;
        int trailing_start = static_cast<int>(orig_line.size());
        if (ctx.show_whitespace == ShowWhitespace::Boundary) {
            while (leading_end < static_cast<int>(orig_line.size()) &&
                   (orig_line[leading_end] == L' ' || orig_line[leading_end] == L'\t'))
                leading_end++;
            while (trailing_start > 0 &&
                   (orig_line[trailing_start - 1] == L' ' || orig_line[trailing_start - 1] == L'\t'))
                trailing_start--;
        }
        for (int ci = 0; ci < static_cast<int>(orig_line.size()); ci++) {
            wchar_t ch = orig_line[ci];
            if (ch != L' ' && ch != L'\t') continue;
            bool in_boundary = (ci < leading_end || ci >= trailing_start);
            if (ctx.show_whitespace == ShowWhitespace::Boundary && !in_boundary)
                continue;
            int exp_pos = (ci < static_cast<int>(source_to_expanded.size()))
                          ? source_to_expanded[ci] : static_cast<int>(expanded.size());
            DWRITE_HIT_TEST_METRICS htm = {};
            float px = 0, py = 0;
            text_layout->HitTestTextPosition(
                static_cast<UINT32>(exp_pos), FALSE, &px, &py, &htm);
            LayoutBlock::WhitespaceMarker wm;
            wm.x = px;
            wm.y = py;
            wm.is_tab = (ch == L'\t');
            lb.ws_markers.push_back(wm);
        }
    }

    // ---- Indent guides ----
    if (ctx.show_indent_guides) {
        lb.indent_guide_color = ctx.muted_color;
        int leading_cols = 0;
        for (wchar_t c : expanded) {
            if (c == L' ') leading_cols++;
            else break;
        }
        float char_width = ctx.code_size * 0.6f;  // approximate monospace width
        if (!expanded.empty()) {
            DWRITE_HIT_TEST_METRICS htm = {};
            float px1 = 0, py1 = 0;
            text_layout->HitTestTextPosition(0, FALSE, &px1, &py1, &htm);
            if (htm.width > 0) char_width = htm.width;
        }
        for (int col = ctx.tab_width; col < leading_cols; col += ctx.tab_width) {
            float guide_x = ctx.code_left + col * char_width;
            lb.indent_guides.push_back(guide_x);
        }
    }

    // ---- Trailing whitespace highlight ----
    if (ctx.highlight_trailing && !expanded.empty()) {
        int trailing_start = static_cast<int>(expanded.size());
        while (trailing_start > 0 && expanded[trailing_start - 1] == L' ')
            trailing_start--;
        if (trailing_start < static_cast<int>(expanded.size())) {
            DWRITE_HIT_TEST_METRICS htm_start = {};
            float px_start = 0, py_start = 0;
            text_layout->HitTestTextPosition(
                static_cast<UINT32>(trailing_start), FALSE,
                &px_start, &py_start, &htm_start);
            DWRITE_HIT_TEST_METRICS htm_end = {};
            float px_end = 0, py_end = 0;
            text_layout->HitTestTextPosition(
                static_cast<UINT32>(expanded.size() - 1), TRUE,
                &px_end, &py_end, &htm_end);
            lb.has_trailing_ws = true;
            lb.trailing_ws_rect = D2D1::RectF(
                lb.rect.left + px_start, lb.rect.top,
                lb.rect.left + px_end, lb.rect.bottom);
            lb.trailing_ws_color = 0xFF4444; // light red
        }
    }
}

// ---- main entry point -------------------------------------------------------

wlx::runtime::layout::LayoutDocument layout_source(
    IDWriteFactory* dwrite,
    const std::string& raw_utf8,
    const wlx::core::colorizer::ColorizeResult& colors,
    const wlx::runtime::theme::ThemeService& theme,
    bool dark_mode,
    float viewport_width,
    const ColorizerDisplayConfig& display,
    LayoutTimings* timings,
    std::vector<int>* out_line_byte_starts) {
    LayoutDocument doc;
    doc.viewport_width = viewport_width;

    if (!dwrite) return doc;

    using _clk = std::chrono::steady_clock;
    auto _ms = [](_clk::time_point a, _clk::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    auto _t0 = _clk::now();

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
    lines.reserve(static_cast<size_t>(
        std::count(raw_utf8.begin(), raw_utf8.end(), '\n')) + 1);  // exact line count
    {
        // Scan raw_utf8 for newlines to split, then convert each chunk.
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

    auto _t_split = _clk::now();

    // ---- index color spans by starting line ----
    // ColorSpan start/length are UTF-8 byte offsets in raw_utf8. distribute_spans_to_lines
    // (shared with the incremental apply_spans_to_range path) maps each span to its
    // line(s) as per-line wchar offsets in the original (pre-expansion) text.
    // Empty `colors` (the skeleton/viewport-incremental case) yields no per-line
    // spans, so every block gets empty color_ranges — colored later on demand.
    std::vector<int> line_byte_starts;
    line_byte_starts.reserve(lines.size());
    for (const auto& li : lines) line_byte_starts.push_back(li.utf8_byte_start);
    if (out_line_byte_starts) *out_line_byte_starts = line_byte_starts;

    std::vector<std::vector<PerLineSpan>> line_spans(lines.size());
    distribute_spans_to_lines(raw_utf8, line_byte_starts,
                              static_cast<int>(raw_utf8.size()), colors,
                              0, static_cast<int>(lines.size()) - 1, line_spans);

    auto _t_spanindex = _clk::now();

    // ---- build LayoutBlocks ----
    // Geometry (block rects, total_height, line_tops) is computed up front for
    // every line — exact arithmetic because UNIFORM line spacing forces each
    // unwrapped line to exactly line_height. The heavy per-line work
    // (CreateTextLayout + decorations) is deferred to first paint of each
    // visible block via doc.materialize_block, UNLESS word-wrap is on (which
    // needs real measured heights, so we build eagerly).
    // Tool-only path since M2: the plugin's no-wrap route uses layout_grid_skeleton;
    // the eager tool route (whole-file colorize bench/smokes) still lands here with
    // word_wrap=false.
    const bool lazy = !display.word_wrap;

    auto mctx = std::make_shared<MaterializeCtx>();
    mctx->dwrite             = dwrite;
    mctx->fmt                = fmt;
    mctx->max_code_width     = max_code_width;
    mctx->line_height        = line_height;
    mctx->code_left          = code_left;
    mctx->code_size          = fonts.code_size;
    mctx->text_color         = palette.text;
    // Unvalidated config: 0 would stall the indent-guide stride loop forever
    // (expand_tabs clamps its own copy internally).
    mctx->tab_width          = std::max(1, display.tab_width);
    mctx->show_whitespace    = display.show_whitespace;
    mctx->show_indent_guides = display.show_indent_guides;
    mctx->highlight_trailing = display.highlight_trailing;
    mctx->link_color         = palette.link;
    mctx->muted_color        = palette.muted;
    if (lazy) mctx->orig_lines.reserve(lines.size());

    // Accumulate y in double: float quantizes above ~16.7M DIPs (visible
    // jitter near the bottom of ~1M-line files); cast per block.
    double y = 4.0; // top padding

    for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
        const std::wstring& orig_line = lines[li].text;

        // Tab-expand. The offset map is read only by build_color_ranges and the
        // eager decorations; a lazy block with no spans needs neither
        // (materialize rebuilds its own map), so skip building it there.
        std::vector<int> source_to_expanded;
        const bool need_map = !lazy || !line_spans[li].empty();
        std::wstring expanded = expand_tabs(orig_line, display.tab_width,
                                            need_map ? &source_to_expanded : nullptr);

        // Map per-line spans from source wchar offsets -> expanded wchar offsets
        std::vector<ColorRange> color_ranges =
            build_color_ranges(line_spans[li], source_to_expanded, expanded);

        LayoutBlock lb;
        lb.type = BlockType::Paragraph; // closest generic type; renderer handles color ranges
        lb.rect = D2D1::RectF(code_left, static_cast<float>(y),
                              viewport_width - right_margin,
                              static_cast<float>(y + line_height));
        lb.background_color = palette.code_bg;
        lb.has_background = false; // no per-line bg; background is painted by host

        TextRun run;
        // Lazy path: `expanded` is dead after this assignment, so move it. Eager
        // path re-reads `expanded` below (create_line_layout + decorations), so
        // it must keep a copy. (A moved-from wstring is empty — same as the old
        // empty-case branch.)
        if (lazy) run.text = std::move(expanded);
        else      run.text = expanded;
        run.rect         = lb.rect;
        run.color        = palette.text;
        run.is_code      = true;
        run.color_ranges = std::move(color_ranges);
        lb.text_runs.push_back(std::move(run));

        // The heavy per-line work — CreateTextLayout + the URL / whitespace /
        // indent / trailing decorations (all of which hit-test the layout) — is
        // deferred to first paint of this block via doc.materialize_block when
        // word-wrap is off, since the no-wrap grid has a fixed line_height. With
        // word-wrap on we build eagerly so block_height reflects the real
        // (possibly multi-row) measured height.
        float block_height = line_height;
        if (lazy) {
            // Lazy path: orig_line (== lines[li].text) is not read again this
            // iteration. Tab-free lines (the common case) are stored as an
            // EMPTY marker — their pre-expansion text equals run.text, which
            // the materialize closure falls back to — so only tabbed lines pay
            // for a second stored copy. NOTE: move lines[li].text directly —
            // std::move(orig_line) is a const& and would still copy.
            if (orig_line.find(L'\t') == std::wstring::npos)
                mctx->orig_lines.emplace_back();
            else
                mctx->orig_lines.push_back(std::move(lines[li].text));
        } else {
            float h = line_height;
            lb.text_runs[0].layout = create_line_layout(
                expanded, lb.text_runs[0].color_ranges, *mctx, h);
            block_height = h;
            if (block_height != line_height) {
                lb.rect.bottom              = static_cast<float>(y + block_height);
                lb.text_runs[0].rect.bottom = static_cast<float>(y + block_height);
            }
            apply_line_decorations(lb, expanded, source_to_expanded, orig_line, *mctx);
        }

        // Line numbers are rendered by the shared gutter renderer
        // (RenderEngine::paint_line_numbers, driven by doc.gutter_width +
        // doc.line_tops below) — NOT the markdown list-bullet path, which drew
        // them in a fixed 24px wrapping box (4-digit numbers collapsed) with the
        // body font and left alignment (wasted space on small files).

        y += block_height;
        doc.blocks.push_back(std::move(lb));
    }

    y += 4.0; // bottom padding
    doc.total_height = static_cast<float>(y);

    // Reserve the gutter column and build the per-line index so the shared
    // RenderEngine::paint_line_numbers draws right-aligned, NO_WRAP, code-font
    // numbers sized to the digit count. Populated here (not only in the host)
    // so the screenshot tool / any caller renders the gutter too.
    doc.gutter_width = display.line_numbers ? code_left : 0.0f;

    // Install the lazy-materialization hook: the renderer calls this per
    // visible block to build its IDWriteTextLayout + decorations on demand.
    // build_line_index below stays correct because each colorizer line is its
    // own block with no embedded '\n', so it only reads run.rect.top (eager).
    if (lazy) {
        auto ctx = mctx;
        doc.materialize_block =
            [ctx](LayoutBlock& lb, int idx) {
                if (lb.text_runs.empty() || lb.text_runs[0].layout) return;
                if (idx < 0 || idx >= static_cast<int>(ctx->orig_lines.size())) return;
                // Tab-free lines were stored as an empty marker: their
                // pre-expansion text is identical to run.text, so fall back to
                // it instead of a duplicate stored copy.
                const std::wstring& stored = ctx->orig_lines[static_cast<size_t>(idx)];
                const std::wstring& orig_line =
                    stored.empty() ? lb.text_runs[0].text : stored;
                std::vector<int> source_to_expanded;
                std::wstring expanded =
                    expand_tabs(orig_line, ctx->tab_width, &source_to_expanded);
                float h = ctx->line_height;
                lb.text_runs[0].layout = create_line_layout(
                    expanded, lb.text_runs[0].color_ranges, *ctx, h);
                apply_line_decorations(lb, expanded, source_to_expanded, orig_line, *ctx);
            };
    }

    auto _t_blocks = _clk::now();
    build_line_index(doc);
    auto _t_index = _clk::now();

    if (timings) {
        timings->line_split_ms   = _ms(_t0, _t_split);
        timings->span_index_ms   = _ms(_t_split, _t_spanindex);
        timings->build_blocks_ms = _ms(_t_spanindex, _t_blocks);
        timings->line_index_ms   = _ms(_t_blocks, _t_index);
    }

    return doc;
}

}  // namespace wlx::plugin_colorizer::layout
