#pragma once

#include "core_dll/colorizer/colorizer.h"
#include "runtime/layout/layout_document.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
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
// This implementation assumes ASCII-compatible source (byte offset == wchar offset for BMP chars).
// Non-ASCII files will have slightly misaligned highlights but will still render correctly as text.
//
// When `out_line_byte_starts` is non-null it is filled with one entry per built
// block: the UTF-8 byte offset in `raw_utf8` where that block's source line
// begins. The colorizer host keeps this to map a viewport byte range back to the
// blocks it touches for incremental (viewport-scoped) recoloring via
// apply_spans_to_range below. Pass an EMPTY `colors` to build a color-less
// skeleton (blocks + run.text, no color_ranges) and colorize incrementally.
wlx::runtime::layout::LayoutDocument layout_source(
    IDWriteFactory* dwrite,
    const std::wstring& source,
    const std::string& raw_utf8,
    const wlx::core::colorizer::ColorizeResult& colors,
    const wlx::runtime::theme::ThemeService& theme,
    bool dark_mode,
    float viewport_width,
    const ColorizerDisplayConfig& display,
    LayoutTimings* timings = nullptr,
    std::vector<int>* out_line_byte_starts = nullptr);

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
// The renderer applies color_ranges every paint, so callers only InvalidateRect
// afterwards; no re-materialize is needed for an unmaterialized block.
void apply_spans_to_range(
    wlx::runtime::layout::LayoutDocument& doc,
    const std::string& raw_utf8,
    const std::vector<int>& line_byte_starts,
    const wlx::core::colorizer::ColorizeResult& spans,
    uint32_t byte_lo, uint32_t byte_hi,
    int tab_width);

}  // namespace wlx::plugin_colorizer::layout
