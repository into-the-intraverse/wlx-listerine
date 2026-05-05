#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/layout/interactive_span.h"
#include "runtime/layout/text_run.h"
#include "runtime/parser/block_node.h"

#include <d2d1.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wlx::runtime::layout {


struct LayoutBlock {
    parser::BlockType type = parser::BlockType::Paragraph;
    D2D1_RECT_F rect = {};
    std::vector<TextRun> text_runs;
    std::vector<InteractiveSpan> spans;

    // Heading
    int heading_level = 0;

    // Decorations
    bool has_left_border = false;
    uint32_t left_border_color = 0;
    bool has_bottom_rule = false;
    uint32_t bottom_rule_color = 0;
    uint32_t background_color = 0;
    bool has_background = false;

    // Bullet/number prefix
    std::wstring bullet_text;
    D2D1_POINT_2F bullet_pos = {};
    uint32_t bullet_color = 0;

    // Whitespace markers (positions relative to text run origin)
    struct WhitespaceMarker {
        float x = 0;         // x position relative to text run left
        float y = 0;         // y position relative to text run top
        bool is_tab = false;
    };
    std::vector<WhitespaceMarker> ws_markers;
    uint32_t ws_marker_color = 0;

    // Indent guides (x positions for vertical lines spanning this block)
    std::vector<float> indent_guides;  // absolute x positions
    uint32_t indent_guide_color = 0;

    // Trailing whitespace highlight
    bool has_trailing_ws = false;
    D2D1_RECT_F trailing_ws_rect = {};
    uint32_t trailing_ws_color = 0;
};

}  // namespace wlx::runtime::layout
