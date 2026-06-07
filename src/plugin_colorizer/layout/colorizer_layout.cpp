#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "plugin_colorizer/layout/colorizer_layout.h"
#include "runtime/parser/block_node.h"
#include "runtime/interaction/url_scanner.h"
#include "runtime/layout/interactive_span.h"
#include "runtime/layout/line_index.h"
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

// ---- lazy materialization ---------------------------------------------------

// Per-document context shared by every block for on-demand (viewport) layout.
// Heap-allocated and captured by the LayoutDocument::materialize_block closure
// so it outlives layout_source(). Holds the DWrite factory + text format and
// the layout-wide constants the per-line build needs, plus the original
// (pre-tab-expansion) source line for each block (indexed by block index).
struct MaterializeCtx {
    ComPtr<IDWriteFactory>    dwrite;
    ComPtr<IDWriteTextFormat> fmt;
    float max_code_width = 1.0f;
    float line_height    = 0.0f;
    float code_left      = 0.0f;
    float code_size      = 0.0f;
    int   tab_width      = 4;
    ShowWhitespace show_whitespace = ShowWhitespace::None;
    bool  show_indent_guides = false;
    bool  highlight_trailing = false;
    uint32_t link_color  = 0;
    uint32_t muted_color = 0;
    std::vector<std::wstring> orig_lines;  // [block_index] -> pre-expansion text
};

// Build the IDWriteTextLayout for one line and report its measured height
// (== line_height for the no-wrap grid; can exceed it when word-wrap is on).
static ComPtr<IDWriteTextLayout> create_line_layout(
    const std::wstring& expanded, const MaterializeCtx& ctx, float& out_height) {
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
static void apply_line_decorations(
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
    const std::wstring& source,
    const std::string& raw_utf8,
    const wlx::core::colorizer::ColorizeResult& colors,
    const wlx::runtime::theme::ThemeService& theme,
    bool dark_mode,
    float viewport_width,
    const ColorizerDisplayConfig& display,
    LayoutTimings* timings) {
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

    auto _t_split = _clk::now();

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
        uint32_t bg_color = 0;
        bool has_bg = false;
        uint8_t modifiers = 0;
    };
    std::vector<std::vector<PerLineSpan>> line_spans(lines.size());

    for (const wlx::core::colorizer::ColorSpan& sp : colors.spans) {
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
                    pls.bg_color    = sp.bg_color;
                    pls.has_bg      = sp.has_bg;
                    pls.modifiers   = sp.modifiers;
                    line_spans[li].push_back(pls);
                }
            }

            remaining_start = static_cast<uint32_t>(next_line_byte_start);
        }
    }

    auto _t_spanindex = _clk::now();

    // ---- build LayoutBlocks ----
    // Geometry (block rects, total_height, line_tops) is computed up front for
    // every line — exact arithmetic because UNIFORM line spacing forces each
    // unwrapped line to exactly line_height. The heavy per-line work
    // (CreateTextLayout + decorations) is deferred to first paint of each
    // visible block via doc.materialize_block, UNLESS word-wrap is on (which
    // needs real measured heights, so we build eagerly).
    const bool lazy = !display.word_wrap;

    auto mctx = std::make_shared<MaterializeCtx>();
    mctx->dwrite             = dwrite;
    mctx->fmt                = fmt;
    mctx->max_code_width     = max_code_width;
    mctx->line_height        = line_height;
    mctx->code_left          = code_left;
    mctx->code_size          = fonts.code_size;
    mctx->tab_width          = display.tab_width;
    mctx->show_whitespace    = display.show_whitespace;
    mctx->show_indent_guides = display.show_indent_guides;
    mctx->highlight_trailing = display.highlight_trailing;
    mctx->link_color         = palette.link;
    mctx->muted_color        = palette.muted;
    if (lazy) mctx->orig_lines.reserve(lines.size());

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
                cr.start     = static_cast<uint32_t>(exp_start);
                cr.length    = static_cast<uint32_t>(exp_len);
                cr.color     = pls.color;
                cr.bg_color  = pls.bg_color;
                cr.has_bg    = pls.has_bg;
                cr.modifiers = pls.modifiers;
                color_ranges.push_back(cr);
            }
        }

        LayoutBlock lb;
        lb.type = BlockType::Paragraph; // closest generic type; renderer handles color ranges
        lb.rect = D2D1::RectF(code_left, y, viewport_width - right_margin, y + line_height);
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
            // iteration, so move it into the materialize context instead of
            // copying every source line a second time. NOTE: move lines[li].text
            // directly — std::move(orig_line) is a const& and would still copy.
            mctx->orig_lines.push_back(std::move(lines[li].text));
        } else {
            float h = line_height;
            lb.text_runs[0].layout = create_line_layout(expanded, *mctx, h);
            block_height = h;
            if (block_height != line_height) {
                lb.rect.bottom              = y + block_height;
                lb.text_runs[0].rect.bottom = y + block_height;
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

    y += 4.0f; // bottom padding
    doc.total_height = y;

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
                const std::wstring& orig_line = ctx->orig_lines[static_cast<size_t>(idx)];
                std::vector<int> source_to_expanded;
                std::wstring expanded =
                    expand_tabs(orig_line, ctx->tab_width, &source_to_expanded);
                float h = ctx->line_height;
                lb.text_runs[0].layout = create_line_layout(expanded, *ctx, h);
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
