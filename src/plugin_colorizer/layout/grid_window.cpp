#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "plugin_colorizer/layout/grid_window.h"
#include "plugin_colorizer/layout/wrap_estimate.h"

#include "runtime/parser/block_node.h"
#include "runtime/theme/color_palette.h"
#include "runtime/theme/font_config.h"

#include <algorithm>
#include <cstdio>

using wlx::runtime::layout::LayoutBlock;
using wlx::runtime::layout::LayoutDocument;
using wlx::runtime::layout::TextRun;
using wlx::runtime::parser::BlockType;
using wlx::runtime::theme::ColorPalette;
using wlx::runtime::theme::FontConfig;
using Microsoft::WRL::ComPtr;

namespace wlx::plugin_colorizer::layout {

// ---- implicit-grid skeleton -------------------------------------------------
// Mirrors layout_source's setup (format/line_height/gutter/margins) but builds
// NO per-line blocks and decodes NO text: only line_byte_starts (a byte scan),
// arithmetic line_tops, and the build context a later slide_grid_window needs.
// NOTE: the format/gutter setup duplicates layout_source's; consolidate when
// the old lazy branch is deleted (M2 cleanup).

LayoutDocument layout_grid_skeleton(
    IDWriteFactory* dwrite,
    const std::string& raw_utf8,
    const wlx::runtime::theme::ThemeService& theme,
    bool dark_mode,
    float viewport_width,
    const ColorizerDisplayConfig& display,
    std::vector<int>* out_line_byte_starts,
    std::shared_ptr<MaterializeCtx>* out_ctx,
    GridGeometry* out_geo) {
    LayoutDocument doc;
    doc.viewport_width = viewport_width;

    if (!dwrite) return doc;

    const ColorPalette& palette = theme.palette(dark_mode);
    const FontConfig& fonts = theme.fonts();

    // Code text format — uniform line spacing (so every unwrapped line is
    // exactly line_height) and NO_WRAP (grid mode is always no-wrap).
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

    // ---- byte-scan source into per-line byte starts (NO decode) ----
    // The whole point of grid mode: skip MultiByteToWideChar entirely. Byte
    // offsets are all the skeleton needs; line text is decoded lazily per
    // viewport line in build_grid_line.
    std::vector<int> line_byte_starts;
    line_byte_starts.reserve(static_cast<size_t>(
        std::count(raw_utf8.begin(), raw_utf8.end(), '\n')) + 1);
    {
        const int u8_len = static_cast<int>(raw_utf8.size());
        line_byte_starts.push_back(0);
        for (int i = 0; i < u8_len; ++i) {
            if (raw_utf8[static_cast<size_t>(i)] == '\n')
                line_byte_starts.push_back(i + 1);
        }
        // A source ending in '\n' produced a trailing start at u8_len: that is a
        // real (empty) final line, matching layout_source's split loop which
        // emits a chunk after the last '\n'. Empty source already has the one
        // [0] entry above.
    }
    const int line_count = static_cast<int>(line_byte_starts.size());
    if (out_line_byte_starts) *out_line_byte_starts = line_byte_starts;

    // ---- line-number gutter sizing (identical to layout_source) ----
    float left_margin = 8.0f;
    float right_margin = 8.0f;
    float ln_col_width = 0.0f;
    if (display.line_numbers) {
        ComPtr<IDWriteTextFormat> ln_fmt;
        dwrite->CreateTextFormat(
            fonts.code_family.c_str(), nullptr,
            DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            fonts.code_size, L"", ln_fmt.GetAddressOf());
        if (ln_fmt) {
            ln_fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            wchar_t sample[16];
            _snwprintf_s(sample, _countof(sample), _TRUNCATE, L"%d", line_count);
            ComPtr<IDWriteTextLayout> sample_layout;
            dwrite->CreateTextLayout(sample, static_cast<UINT32>(wcslen(sample)),
                                     ln_fmt.Get(), 1000.0f, 100.0f,
                                     sample_layout.GetAddressOf());
            if (sample_layout) {
                DWRITE_TEXT_METRICS m;
                sample_layout->GetMetrics(&m);
                ln_col_width = m.width + 16.0f;  // padding
            }
        }
    }

    float code_left = left_margin + ln_col_width;
    float code_right = viewport_width - right_margin;
    float max_code_width = std::max(1.0f, code_right - code_left);

    // ---- build GridGeometry (row_starts populated in wrap mode) ----
    GridGeometry geo{4.0f, line_height, line_count};
    if (display.word_wrap) {
        // One-glyph probe for the monospace advance; the estimate is corrected
        // to measured values by slide_grid_window, so a fallback ratio is fine
        // for proportional fonts.
        float advance = fonts.code_size * 0.6f;
        ComPtr<IDWriteTextLayout> probe;
        dwrite->CreateTextLayout(L"0", 1, fmt.Get(), 1000.0f, 100.0f,
                                 probe.GetAddressOf());
        if (probe) {
            DWRITE_TEXT_METRICS pm{};
            if (SUCCEEDED(probe->GetMetrics(&pm)) && pm.width > 0.0f)
                advance = pm.width;
        }
        const int cols_per_row =
            std::max(1, static_cast<int>(max_code_width / advance));
        geo.row_starts = build_row_starts(estimate_wrap_rows(
            raw_utf8, line_byte_starts, display.tab_width, cols_per_row));
    }

    // ---- fill the grid skeleton ----
    doc.grid_line_count = line_count;
    doc.first_block_line = 0;
    doc.line_tops.reserve(static_cast<size_t>(line_count));
    for (int i = 0; i < line_count; ++i)
        doc.line_tops.push_back(grid_line_top(geo, i));

    doc.total_height = grid_total_height(geo);
    doc.gutter_width = display.line_numbers ? code_left : 0.0f;

    // ---- build context the slide path reuses ----
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
    // orig_lines stays empty: the grid path decodes per line in build_grid_line.

    if (out_ctx) *out_ctx = mctx;
    if (out_geo) *out_geo = std::move(geo);

    return doc;
}

// ---- one line's block -------------------------------------------------------

LayoutBlock build_grid_line(int line, const GridGeometry& geo, MaterializeCtx& ctx,
                            const std::string& raw_utf8,
                            const std::vector<int>& line_byte_starts,
                            const std::vector<PerLineSpan>& spans_for_line) {
    const int raw_size = static_cast<int>(raw_utf8.size());
    const int line_count = static_cast<int>(line_byte_starts.size());
    const int byte_start = line_byte_starts[static_cast<size_t>(line)];
    const int content_end = (line + 1 < line_count)
                                ? line_byte_starts[static_cast<size_t>(line + 1)] - 1
                                : raw_size;  // strip the '\n'
    std::wstring orig = decode_line(raw_utf8, byte_start,
                                    std::max(byte_start, content_end));
    std::vector<int> source_to_expanded;
    std::wstring expanded = expand_tabs(orig, ctx.tab_width, &source_to_expanded);

    LayoutBlock lb;
    lb.type = BlockType::Paragraph;
    const float top = grid_line_top(geo, line);
    lb.rect = D2D1::RectF(ctx.code_left, top,
                          ctx.code_left + ctx.max_code_width, top + geo.line_height);
    // background_color deliberately not set: has_background stays false and the
    // renderer ignores it (matches the eager loop's has_background = false).

    TextRun run;
    run.rect = lb.rect;
    run.color = ctx.text_color;
    run.is_code = true;
    run.color_ranges = build_color_ranges(spans_for_line, source_to_expanded, expanded);
    float h = geo.line_height;
    run.layout = create_line_layout(expanded, run.color_ranges, ctx, h);
    run.text = std::move(expanded);
    lb.text_runs.push_back(std::move(run));
    apply_line_decorations(lb, lb.text_runs[0].text, source_to_expanded, orig, ctx);
    return lb;
}

// ---- slide the materialized window ------------------------------------------

void slide_grid_window(LayoutDocument& doc, const GridGeometry& geo,
                       MaterializeCtx& ctx, const std::string& raw_utf8,
                       const std::vector<int>& line_byte_starts,
                       int first, int last, const ColorsForRange& colors_for) {
    // Clamp into the valid line range: grid_window_lines already clamps, but
    // this is the public API boundary — a future caller must not be able to
    // index line_byte_starts out of bounds.
    first = std::max(0, first);
    last = std::min(last, static_cast<int>(line_byte_starts.size()) - 1);
    if (last < first) {  // empty window (also: empty doc, or first past EOF)
        doc.blocks.clear();
        doc.first_block_line = std::max(0, first);
        return;
    }
    const int old_first = doc.first_block_line;
    const int old_last = old_first + static_cast<int>(doc.blocks.size()) - 1;

    std::vector<LayoutBlock> next;
    next.reserve(static_cast<size_t>(last - first + 1));

    // Color one contiguous entering run [lo_line, hi_line] with a SINGLE
    // colors_for call, then distribute window-relative for that run.
    auto colors_into = [&](int lo_line, int hi_line,
                           std::vector<std::vector<PerLineSpan>>& out) {
        const int raw_size = static_cast<int>(raw_utf8.size());
        const uint32_t blo =
            static_cast<uint32_t>(line_byte_starts[static_cast<size_t>(lo_line)]);
        const uint32_t bhi =
            (hi_line + 1 < static_cast<int>(line_byte_starts.size()))
                ? static_cast<uint32_t>(line_byte_starts[static_cast<size_t>(hi_line + 1)])
                : static_cast<uint32_t>(raw_size);
        auto spans = colors_for(blo, bhi);
        out.assign(static_cast<size_t>(hi_line - lo_line + 1), {});
        distribute_spans_to_lines(raw_utf8, line_byte_starts, raw_size, spans,
                                  lo_line, hi_line, out);
    };

    // Walk the new window; MOVE blocks that overlap the old window (their
    // IDWriteTextLayouts survive untouched), batch-build the contiguous entering
    // ranges around them. doc.blocks.empty() defends the first slide (old window
    // empty: old_last < old_first, so the in-window test below never fires).
    int line = first;
    while (line <= last) {
        if (!doc.blocks.empty() && line >= old_first && line <= old_last) {
            next.push_back(
                std::move(doc.blocks[static_cast<size_t>(line - old_first)]));
            ++line;
            continue;
        }
        // Extend the entering run until the next line is one we can reuse.
        int run_end = line;
        while (run_end + 1 <= last &&
               !(!doc.blocks.empty() && run_end + 1 >= old_first &&
                 run_end + 1 <= old_last))
            ++run_end;
        std::vector<std::vector<PerLineSpan>> line_spans;
        colors_into(line, run_end, line_spans);
        for (int l = line; l <= run_end; ++l)
            next.push_back(build_grid_line(l, geo, ctx, raw_utf8, line_byte_starts,
                                           line_spans[static_cast<size_t>(l - line)]));
        line = run_end + 1;
    }

    doc.blocks = std::move(next);  // leaving blocks destroyed here = eviction
    doc.first_block_line = first;
}

// ---- grid selection text ----------------------------------------------------

std::wstring extract_selected_text_grid(const std::string& raw_utf8,
                                        const std::vector<int>& line_byte_starts,
                                        int tab_width,
                                        wlx::runtime::layout::TextPosition lo,
                                        wlx::runtime::layout::TextPosition hi) {
    std::wstring out;
    const int line_count = static_cast<int>(line_byte_starts.size());
    const int raw_size = static_cast<int>(raw_utf8.size());
    const int first = std::max(0, lo.block_index);
    for (int line = first; line <= hi.block_index && line < line_count; ++line) {
        const int bs = line_byte_starts[static_cast<size_t>(line)];
        const int be = (line + 1 < line_count)
                           ? line_byte_starts[static_cast<size_t>(line) + 1] - 1
                           : raw_size;
        std::wstring expanded =
            expand_tabs(decode_line(raw_utf8, bs, std::max(bs, be)), tab_width, nullptr);
        int from = (line == lo.block_index) ? lo.char_offset : 0;
        int to = (line == hi.block_index) ? hi.char_offset
                                          : static_cast<int>(expanded.size());
        from = std::clamp(from, 0, static_cast<int>(expanded.size()));
        to = std::clamp(to, from, static_cast<int>(expanded.size()));
        if (line != first) out.push_back(L'\n');
        out.append(expanded, static_cast<size_t>(from), static_cast<size_t>(to - from));
    }
    return out;
}

}  // namespace wlx::plugin_colorizer::layout
