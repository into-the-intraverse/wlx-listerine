#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <concepts>

namespace wlx::runtime::host {

// State the scroll helpers need from a per-plugin view struct. Both md's
// ViewState and the colorizer's ColorViewState satisfy this — the
// ->layout->total_height and ->renderer->dip_height() chains are duck-typed
// at instantiation rather than constrained here, since they go through
// shared_ptr / unique_ptr indirection.
template <typename V>
concept Scrollable = requires(V& v) {
    { v.hwnd }         -> std::convertible_to<HWND>;
    { v.scroll_y }     -> std::same_as<float&>;
    { v.max_scroll_y } -> std::same_as<float&>;
    v.layout;
    v.renderer;
};

// SCROLLINFO fields are int-based; clamp the float layout heights before
// casting (float -> int conversion of a value > INT_MAX is UB).
inline constexpr float kMaxScrollUnits = 2.0e9f;  // < INT_MAX

template <Scrollable V>
void update_scrollbar(V& v) {
    if (!v.layout || !v.hwnd) return;

    // Use DIP dimensions — layout and D2D coordinates are in DIPs, not physical pixels
    float viewport_h = v.renderer ? v.renderer->dip_height() : 1.0f;
    v.max_scroll_y = std::max(0.0f, v.layout->total_height - viewport_h);
    v.scroll_y = std::clamp(v.scroll_y, 0.0f, v.max_scroll_y);

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = static_cast<int>(std::min(v.layout->total_height, kMaxScrollUnits));
    si.nPage = static_cast<UINT>(viewport_h);
    si.nPos = static_cast<int>(std::min(v.scroll_y, kMaxScrollUnits));
    SetScrollInfo(v.hwnd, SB_VERT, &si, TRUE);
}

template <Scrollable V>
void handle_scroll(V& v, float delta) {
    if (!v.layout) return;

    float old_y = v.scroll_y;
    v.scroll_y = std::clamp(v.scroll_y + delta, 0.0f, v.max_scroll_y);

    if (v.scroll_y != old_y) {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_POS;
        si.nPos = static_cast<int>(std::min(v.scroll_y, kMaxScrollUnits));
        SetScrollInfo(v.hwnd, SB_VERT, &si, TRUE);
        InvalidateRect(v.hwnd, nullptr, FALSE);
    }
}

}  // namespace wlx::runtime::host
