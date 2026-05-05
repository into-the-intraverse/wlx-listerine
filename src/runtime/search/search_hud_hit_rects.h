#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d2d1.h>

struct SearchHudHitRects {
    D2D1_RECT_F counter   = {};
    D2D1_RECT_F prev      = {};
    D2D1_RECT_F next      = {};
    D2D1_RECT_F bounds    = {};   // full bar rect (origin 0,0)
    float       width     = 0;
    float       height    = 0;
};

// Hit-test a click against the painter's button rects. Returns 0 for prev,
// 1 for next, -1 for no hit (counter pill is intentionally not a click target).
//
// (x_phys, y_phys) are HWND-client-relative mouse coords in *physical* pixels
// — i.e. the values that arrive in WM_MOUSEMOVE / WM_LBUTTONDOWN under
// Per-Monitor V2 DPI awareness. `rects` is in DIPs (as returned by
// SearchHudPainter::layout). `dpi` is the parent window's effective DPI;
// 0 falls back to 96 (no scaling).
inline int hit_test_button(const SearchHudHitRects& rects,
                           int x_phys, int y_phys, unsigned dpi) {
    if (dpi == 0) dpi = 96;
    float scale = static_cast<float>(dpi) / 96.0f;
    float fx = static_cast<float>(x_phys) / scale;
    float fy = static_cast<float>(y_phys) / scale;
    auto inside = [&](const D2D1_RECT_F& r) {
        return fx >= r.left && fx < r.right && fy >= r.top && fy < r.bottom;
    };
    if (inside(rects.prev)) return 0;
    if (inside(rects.next)) return 1;
    return -1;
}
