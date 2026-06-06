#pragma once

#include "core_dll/colorizer/colorizer.h"
#include "runtime/layout/layout_document.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
#include <string>

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
wlx::runtime::layout::LayoutDocument layout_source(
    IDWriteFactory* dwrite,
    const std::wstring& source,
    const std::string& raw_utf8,
    const wlx::core::colorizer::ColorizeResult& colors,
    const wlx::runtime::theme::ThemeService& theme,
    bool dark_mode,
    float viewport_width,
    const ColorizerDisplayConfig& display,
    LayoutTimings* timings = nullptr);

}  // namespace wlx::plugin_colorizer::layout
