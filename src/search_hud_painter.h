#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "theme_service.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>

struct SearchHudState {
    int current_one_based = 0;
    int total             = 0;
    int hovered_button    = -1;   // -1 none, 0 prev, 1 next
};

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

class SearchHudPainter {
public:
    SearchHudPainter(IDWriteFactory* dwrite, const ThemeService& theme);

    // Lays out the bar at origin (0,0). Caller positions the HWND/anchor
    // separately. Returns rects and total bar size.
    SearchHudHitRects layout(const SearchHudState& s) const;

    // Paints the bar onto rt, using rects from layout(). The render target
    // must already have BeginDraw() called. Caller owns Begin/EndDraw.
    void paint(ID2D1RenderTarget* rt, const SearchHudState& s,
               const SearchHudHitRects& rects, bool dark_mode) const;

private:
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_format_;
    IDWriteFactory* dwrite_   = nullptr;
    const ThemeService* theme_ = nullptr;

    static constexpr float kPadX     = 8.0f;   // horizontal padding inside bar
    static constexpr float kPadY     = 4.0f;   // vertical padding inside bar
    static constexpr float kGap      = 4.0f;   // gap between widgets
    static constexpr float kBarH     = 28.0f;  // total bar height
    static constexpr float kPillH    = 20.0f;
    static constexpr float kPillPadX = 6.0f;   // horizontal padding inside counter pill
    static constexpr float kBtnSize  = 24.0f;
    static constexpr float kCornerR  = 4.0f;
    // Bar corners are square: HwndRenderTarget can't render alpha outside the
    // rounded path, so any radius >0 leaves the parent's bg showing through
    // (or black for an opaque parent) at the corners. Square keeps the chip
    // crisp without needing a layered window.
    static constexpr float kPillCornerR = 0.0f;
    static constexpr float kFontSize = 13.0f;
    static constexpr float kPillAlpha    = 0.85f;
    static constexpr float kHoverAlpha    = 0.18f;
    static constexpr float kDisabledAlpha = 0.3f;
};
