#pragma once

#include "runtime/theme/color_palette.h"
#include "runtime/theme/font_config.h"
#include "runtime/theme/spacing_config.h"

#include <string>
#include <vector>

namespace wlx::runtime::theme {


struct ThemeConfig {
    int version = 2;
    std::wstring detect_string = L"EXT=\"MD\" | EXT=\"MARKDOWN\"";
    std::vector<std::wstring> extensions = {L"md", L"markdown", L"mdown", L"mkd", L"mkdn"};
    FontConfig fonts;
    SpacingConfig spacing;
    ColorPalette light;
    ColorPalette dark;

    // Code highlighting
    std::string code_default_language;  // empty = no highlighting for untagged blocks

    // Markdown line-number gutter (rendered logical lines). Default on.
    bool line_numbers = true;
};

}  // namespace wlx::runtime::theme
