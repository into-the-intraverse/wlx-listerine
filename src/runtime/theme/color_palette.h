#pragma once

#include <cstdint>

namespace wlx::runtime::theme {


struct ColorPalette {
    uint32_t background;
    uint32_t text;
    uint32_t heading;
    uint32_t muted;
    uint32_t link;
    uint32_t link_hover;
    uint32_t code_bg;
    uint32_t quote_border;
    uint32_t rule;
    uint32_t selection;
    uint32_t search_highlight;
    uint32_t search_highlight_current;
};

}  // namespace wlx::runtime::theme
