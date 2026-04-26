#pragma once

#include "colorizer.h"
#include "layout_engine.h"
#include "theme_service.h"

#include <dwrite.h>
#include <string>

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

// Convert source code lines + color spans into a LayoutDocument the RenderEngine can paint.
//
// NOTE: Color span offsets from ColorizeResult are UTF-8 byte offsets in the original source.
// This implementation assumes ASCII-compatible source (byte offset == wchar offset for BMP chars).
// Non-ASCII files will have slightly misaligned highlights but will still render correctly as text.
LayoutDocument layout_source(IDWriteFactory* dwrite,
                             const std::wstring& source,
                             const std::string& raw_utf8,
                             const ColorizeResult& colors,
                             const ThemeService& theme,
                             bool dark_mode,
                             float viewport_width,
                             const ColorizerDisplayConfig& display);
