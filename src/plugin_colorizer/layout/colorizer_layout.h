#pragma once

#include "core_dll/colorizer/colorizer.h"
#include "runtime/layout/layout_document.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wlx::plugin_colorizer::layout {

enum class ShowWhitespace { None, All, Boundary };
enum class CppGrammar { Standard, Unreal };

struct ColorizerDisplayConfig {
    bool line_numbers = true;
    bool word_wrap = false;
    int tab_width = 4;
    float line_height_factor = 1.4f;
    ShowWhitespace show_whitespace = ShowWhitespace::Boundary;
    bool show_indent_guides = true;
    bool highlight_trailing = true;
    CppGrammar cpp_grammar = CppGrammar::Standard;
};

// ---- shared per-line build primitives ---------------------------------------
// De-static'd from colorizer_layout.cpp so grid_window.cpp can reuse them. Bodies
// stay in colorizer_layout.cpp; signatures here match the definitions verbatim.

// Decode one line's UTF-8 byte slice [line_byte_start, line_content_end_byte)
// into the original (pre-tab-expansion) wstring, stripping a trailing '\r'.
std::wstring decode_line(const std::string& raw_utf8,
                         int line_byte_start, int line_content_end_byte);

// Expand a single source line (wstring) by converting tabs to spaces. Fills
// out_source_to_expanded[i] = expanded offset for source char i (nullable).
std::wstring expand_tabs(const std::wstring& line, int tab_width,
                         std::vector<int>* out_source_to_expanded);

// A color span already clamped to one line, with offsets relative to the
// (pre-tab-expansion) line text in wchar units.
struct PerLineSpan {
    int wchar_start = 0;
    int wchar_len = 0;
    uint32_t color = 0;
    uint32_t bg_color = 0;
    bool has_bg = false;
    uint8_t modifiers = 0;
};

// Walk `spans` (UTF-8 byte offsets in raw_utf8) and assign each to the line(s)
// it covers, appending PerLineSpans to out_line_spans[line - line_first]. Only
// lines in [line_first, line_last] receive spans; out_line_spans is indexed
// WINDOW-RELATIVE and must be sized line_last - line_first + 1.
void distribute_spans_to_lines(
    const std::string& raw_utf8,
    const std::vector<int>& line_byte_starts,
    int raw_utf8_size,
    const wlx::core::colorizer::ColorizeResult& spans,
    int line_first, int line_last,
    std::vector<std::vector<PerLineSpan>>& out_line_spans);

// Map a line's PerLineSpans (source-wchar offsets) onto tab-expanded wchar
// offsets and emit ColorRanges (in EXPANDED offsets).
std::vector<wlx::runtime::layout::ColorRange> build_color_ranges(
    const std::vector<PerLineSpan>& line_spans,
    const std::vector<int>& source_to_expanded,
    const std::wstring& expanded);

// Per-document context shared by every line's build. Holds the DWrite factory +
// text format and the layout-wide constants the per-line build needs.
struct MaterializeCtx {
    Microsoft::WRL::ComPtr<IDWriteFactory>    dwrite;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    float max_code_width = 1.0f;
    float line_height    = 0.0f;
    float code_left      = 0.0f;
    float code_size      = 0.0f;
    int   tab_width      = 4;
    uint32_t text_color  = 0;
    ShowWhitespace show_whitespace = ShowWhitespace::None;
    bool  show_indent_guides = false;
    bool  highlight_trailing = false;
    uint32_t link_color  = 0;
    uint32_t muted_color = 0;
    std::vector<std::wstring> orig_lines;  // [block_index] -> pre-expansion text
                                           // (old lazy hook only; grid path unused)
};

// Build the IDWriteTextLayout for one line and report its measured height. Syntax
// font modifiers from `color_ranges` are applied BEFORE GetMetrics.
Microsoft::WRL::ComPtr<IDWriteTextLayout> create_line_layout(
    const std::wstring& expanded,
    const std::vector<wlx::runtime::layout::ColorRange>& color_ranges,
    const MaterializeCtx& ctx, float& out_height);

// Compute the viewport-only decorations for one already-laid-out line: URL link
// color range + interactive hit rects, whitespace markers, indent guides, and
// the trailing-whitespace highlight. Requires lb.text_runs[0].layout set and
// lb.rect final.
void apply_line_decorations(
    wlx::runtime::layout::LayoutBlock& lb,
    const std::wstring& expanded,
    const std::vector<int>& source_to_expanded,
    const std::wstring& orig_line,
    const MaterializeCtx& ctx);

// Per-call layout timing breakdown, populated only when a non-null pointer is
// passed to layout_source (diagnostic/bench use). Splits the opaque "layout"
// number into its phases so the per-line CreateTextLayout cost (the lazy-layout
// target) is separable from line splitting, span indexing, and the line index.
struct LayoutTimings {
    double line_split_ms   = 0;  // UTF-8 split into per-line wchar text
    double span_index_ms   = 0;  // map color spans onto lines
    double build_blocks_ms = 0;  // per-line CreateTextLayout + GetMetrics + decorations
    double line_index_ms   = 0;  // build_line_index
};

// Convert source code lines + color spans into a LayoutDocument the RenderEngine can paint.
//
// NOTE: Color span offsets from ColorizeResult are UTF-8 byte offsets in the original source.
// They are converted to per-line wchar offsets via a byte->wchar prefix table built from the
// MultiByteToWideChar-decoded line (pure-ASCII lines take an identity fast path), then mapped
// to tab-expanded offsets for the rendered text.
//
// When `out_line_byte_starts` is non-null it is filled with one entry per built
// block: the UTF-8 byte offset in `raw_utf8` where that block's source line
// begins. The colorizer host keeps this to map a viewport byte range back to the
// blocks it touches for incremental (viewport-scoped) recoloring via
// apply_spans_to_range below. Pass an EMPTY `colors` to build a color-less
// skeleton (blocks + run.text, no color_ranges) and colorize incrementally.
wlx::runtime::layout::LayoutDocument layout_source(
    IDWriteFactory* dwrite,
    const std::string& raw_utf8,
    const wlx::core::colorizer::ColorizeResult& colors,
    const wlx::runtime::theme::ThemeService& theme,
    bool dark_mode,
    float viewport_width,
    const ColorizerDisplayConfig& display,
    LayoutTimings* timings = nullptr,
    std::vector<int>* out_line_byte_starts = nullptr);

// Result of mapping a viewport (in document-Y DIPs) to a source byte range.
// `empty` is true when nothing is visible / there are no blocks; lo/hi are then 0.
struct ByteRange {
    uint32_t lo = 0;
    uint32_t hi = 0;
    bool empty = true;
};

// Map the visible block range to a source UTF-8 byte range. Pure — no COM, no I/O.
// Used by the colorizer host and the screenshot tool's --cached-tree path.
//
// `overscan` extends the window symmetrically (host passes overscan == viewport_h,
// i.e. one screenful on each side). Returns [lo, hi) aligned to block boundaries:
// lo = line_byte_starts[first visible block]; hi = line_byte_starts[last + 1], or
// `raw_size` for the final block. NOTE: hi is plain raw_size, NOT raw_size + 1 (the
// +1 sentinel is internal to apply_spans_to_range's overlap test).
// Returns {empty=true} when there are no blocks / no line_byte_starts / nothing visible.
ByteRange viewport_byte_range(
    const std::vector<wlx::runtime::layout::LayoutBlock>& blocks,
    const std::vector<int>& line_byte_starts,
    int raw_size,
    float scroll_y, float viewport_h, float overscan);

// Decide whether the visible byte range [vlo, vhi) is already covered by the
// already-colored interval [clo, chi), and what interval to record after
// highlighting. Mirrors colorize_viewport's skip / union / reset logic exactly:
//   - skip  iff the colored interval is non-empty (chi > clo) AND
//            vlo >= clo AND vhi <= chi (the window is fully inside it).
//   - otherwise highlight, then update the interval:
//       * colored interval empty (chi <= clo) -> set to [vlo, vhi).
//       * else if it overlaps/abuts (vlo <= chi AND vhi >= clo) -> union:
//         [min(clo, vlo), max(chi, vhi)).
//       * else (disjoint jump) -> reset to [vlo, vhi).
// When skip is true, new_lo/new_hi are left at 0 and MUST be ignored by the caller.
struct ColoredDecision {
    bool skip = false;
    uint32_t new_lo = 0;
    uint32_t new_hi = 0;
};
ColoredDecision colored_interval_update(uint32_t vlo, uint32_t vhi,
                                        uint32_t clo, uint32_t chi);

// Distribute syntax `spans` onto the layout's per-line blocks, REPLACING the
// color_ranges of every block whose source line is touched by the [byte_lo,
// byte_hi) window. Each colorizer block is exactly one source line;
// `line_byte_starts[block]` (from layout_source's out-param) gives each block's
// UTF-8 byte start, and `raw_utf8` is re-read to map byte offsets -> per-line
// wchar -> tab-expanded offsets, identical to the whole-doc build path.
//
// Blocks fully OUTSIDE [byte_lo, byte_hi) are left untouched. Blocks INSIDE the
// window that no span covers get their color_ranges CLEARED (so a re-colorize of
// the same window is clean + idempotent). URL color ranges are NOT managed here
// (they are rebuilt lazily on materialize); only syntax ranges are touched, so a
// re-color preserving URLs requires materialize to re-run — the host clears the
// per-line layout when recoloring an already-materialized block (see host).
//
// The renderer re-applies color_ranges' foreground BRUSHES every paint, so for
// colors callers only InvalidateRect afterwards; no re-materialize is needed
// for an unmaterialized block. Font modifiers (bold/italic/underline/strike)
// are different: they are baked into the IDWriteTextLayout when the block
// materializes, so showing changed modifiers on an already-materialized block
// needs the host's layout-clear on recolor (above) to force a re-materialize.
void apply_spans_to_range(
    wlx::runtime::layout::LayoutDocument& doc,
    const std::string& raw_utf8,
    const std::vector<int>& line_byte_starts,
    const wlx::core::colorizer::ColorizeResult& spans,
    uint32_t byte_lo, uint32_t byte_hi,
    int tab_width);

}  // namespace wlx::plugin_colorizer::layout
