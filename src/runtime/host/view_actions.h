#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/host/clipboard.h"
#include "runtime/host/hit_test.h"
#include "runtime/host/selection_helpers.h"
#include "runtime/interaction/text_selection.h"
#include "runtime/layout/text_position.h"

#include <algorithm>
#include <windows.h>

namespace wlx::runtime::host {

// Copy the current selection to the clipboard. Returns true if anything
// was copied; false if there is no selection or no layout.
template <Selectable V>
bool copy_selection(V& v, HWND hwnd) {
    if (!v.layout) return false;
    if (!v.sel_anchor.valid()) return false;
    if (v.sel_anchor == v.sel_active) return false;
    auto lo = std::min(v.sel_anchor, v.sel_active);
    auto hi = std::max(v.sel_anchor, v.sel_active);
    auto text = wlx::runtime::interaction::extract_selected_text(*v.layout, lo, hi);
    return copy_to_clipboard(hwnd, text);
}

// Set sel_anchor/sel_active to span the entire document. Returns true if
// the selection was changed; false if the layout is null or empty.
template <Selectable V>
bool select_all(V& v) {
    if (!v.layout || v.layout->blocks.empty()) return false;
    v.sel_anchor = wlx::runtime::layout::TextPosition{0, 0};
    int last = static_cast<int>(v.layout->blocks.size()) - 1;
    v.sel_active = wlx::runtime::layout::TextPosition{
        last, block_text_length(v.layout->blocks[last])};
    v.selecting = false;
    return true;
}

}  // namespace wlx::runtime::host
