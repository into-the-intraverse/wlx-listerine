#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/host/scroll_handler.h"
#include "runtime/layout/text_position.h"
#include "runtime/search/search_match.h"

#include <dwrite.h>
#include <windows.h>

#include <algorithm>
#include <concepts>
#include <vector>

namespace wlx::runtime::host {

template <typename V>
concept Selectable = requires(V& v) {
    { v.sel_anchor } -> std::same_as<wlx::runtime::layout::TextPosition&>;
    { v.sel_active } -> std::same_as<wlx::runtime::layout::TextPosition&>;
    { v.selecting }  -> std::same_as<bool&>;
};

template <Selectable V>
void clear_selection(V& v) {
    v.sel_anchor = wlx::runtime::layout::TextPosition{};
    v.sel_active = wlx::runtime::layout::TextPosition{};
    v.selecting  = false;
}

template <Scrollable V>
void scroll_to_match(V& v, const wlx::runtime::search::SearchMatch& m) {
    if (!v.layout) return;
    // m.block_index is a PUBLIC source-line index; convert to the window-local
    // blocks slot (subtract first_block_line). Identity when base == 0.
    const int slot = m.block_index - v.layout->first_block_line;
    if (slot < 0 || slot >= static_cast<int>(v.layout->blocks.size())) return;

    const auto& block = v.layout->blocks[static_cast<size_t>(slot)];
    const float viewport_h = v.renderer ? v.renderer->dip_height() : 100.0f;
    const float block_top = block.rect.top;
    const float block_bot = block.rect.bottom;

    if (block_top >= v.scroll_y && block_bot <= v.scroll_y + viewport_h) {
        // Block already fully visible — but match may still be off-screen
        // in a tall block. Fall through to the precision pass below.
    } else {
        // Center the block vertically first.
        const float target = block_top - (viewport_h - (block_bot - block_top)) * 0.5f;
        v.scroll_y = std::clamp(target, 0.0f, v.max_scroll_y);
    }

    // Precision pass: hit-test the match's first rect. If the match is still
    // outside the viewport after centering, place the match ~1/3 from top.
    int cursor = 0;
    for (const auto& run : block.text_runs) {
        const int run_len = static_cast<int>(run.text.size());
        const int run_start = cursor;
        const int run_end   = cursor + run_len;
        cursor = run_end;
        if (m.char_end <= run_start || m.char_start >= run_end) continue;
        if (!run.layout) continue;

        const int local_start = std::max(m.char_start, run_start) - run_start;
        const int local_end   = std::min(m.char_end,   run_end)   - run_start;
        const UINT32 length   = static_cast<UINT32>(local_end - local_start);

        UINT32 required = 0;
        run.layout->HitTestTextRange(
            static_cast<UINT32>(local_start), length,
            run.rect.left, run.rect.top,
            nullptr, 0, &required);
        if (required == 0) break;

        std::vector<DWRITE_HIT_TEST_METRICS> metrics(required);
        UINT32 actual = 0;
        run.layout->HitTestTextRange(
            static_cast<UINT32>(local_start), length,
            run.rect.left, run.rect.top,
            metrics.data(), required, &actual);
        if (actual == 0) break;

        const float match_top = metrics[0].top;
        const float match_bot = metrics[0].top + metrics[0].height;
        if (match_top < v.scroll_y || match_bot > v.scroll_y + viewport_h) {
            v.scroll_y = std::clamp(match_top - viewport_h * 0.33f,
                                     0.0f, v.max_scroll_y);
        }
        break;
    }

    update_scrollbar(v);
}

}  // namespace wlx::runtime::host
