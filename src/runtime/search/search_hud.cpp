#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "runtime/search/search_hud.h"
#include "runtime/diagnostics/wlx_trace.h"

#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace wlx::runtime::search {

using namespace wlx::runtime::theme;

namespace {
constexpr wchar_t kClassName[] = L"WlxListerineSearchHud";
constexpr int    kMargin      = 12;

LRESULT CALLBACK SearchHudWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    auto* self = reinterpret_cast<SearchHud*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->handle_message(hwnd, msg, wp, lp);
}
} // namespace

void SearchHud::register_class(HMODULE module) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SearchHudWndProc;
    wc.hInstance     = module;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        WLX_TRACE(L"SearchHud::register_class failed err=%lu", GetLastError());
    }
}

void SearchHud::unregister_class(HMODULE module) {
    UnregisterClassW(kClassName, module);
}

SearchHud::SearchHud(HWND parent, ID2D1Factory* d2d_factory,
                     IDWriteFactory* dwrite_factory, const ThemeService& theme,
                     bool dark_mode)
    : parent_(parent),
      d2d_factory_(d2d_factory),
      theme_(&theme),
      dark_mode_(dark_mode),
      painter_(dwrite_factory, theme) {
    if (!parent_) return;
    HMODULE mod = reinterpret_cast<HMODULE>(GetWindowLongPtrW(parent_, GWLP_HINSTANCE));

    hwnd_ = CreateWindowExW(
        WS_EX_NOACTIVATE,
        kClassName, L"",
        WS_CHILD | WS_CLIPSIBLINGS,
        0, 0, 1, 1,
        parent_, nullptr, mod, this);
    // hwnd_ may be null — handled gracefully by all subsequent methods.
}

SearchHud::~SearchHud() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    rt_.Reset();
}

void SearchHud::recreate_target() {
    if (!hwnd_ || !d2d_factory_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT32>(std::max<LONG>(1, rc.right - rc.left)),
        static_cast<UINT32>(std::max<LONG>(1, rc.bottom - rc.top)));
    rt_.Reset();
    // Pick up the parent's effective DPI so the painter's DIP-based constants
    // render at the correct physical-pixel size on Per-Monitor V2 displays
    // (e.g. 144 at 150% scaling, 192 at 200%). Falling back to 96 keeps the
    // unscaled defaults if the API is unavailable for any reason.
    UINT dpi = GetDpiForWindow(hwnd_);
    if (dpi == 0) dpi = 96;
    auto props = D2D1::RenderTargetProperties();
    props.dpiX = static_cast<float>(dpi);
    props.dpiY = static_cast<float>(dpi);
    d2d_factory_->CreateHwndRenderTarget(
        props,
        D2D1::HwndRenderTargetProperties(hwnd_, size),
        rt_.GetAddressOf());
}

void SearchHud::reposition_to_parent() {
    if (!hwnd_ || !parent_) return;
    rects_ = painter_.layout(state_);
    // Painter constants are DIPs; the HWND lives in physical pixels. Scale by
    // the parent's DPI so the bar's physical footprint matches the painted
    // surface (otherwise the right-hand chevron clips off-window at >100%).
    UINT dpi = GetDpiForWindow(hwnd_);
    if (dpi == 0) dpi = 96;
    float scale = static_cast<float>(dpi) / 96.0f;
    int w = static_cast<int>(std::ceil(rects_.width  * scale));
    int h = static_cast<int>(std::ceil(rects_.height * scale));
    int margin = static_cast<int>(std::ceil(kMargin * scale));
    RECT pr;
    GetClientRect(parent_, &pr);
    int x = pr.right - w - margin;
    int y = pr.bottom - h - margin;
    SetWindowPos(hwnd_, HWND_TOP, x, y, w, h,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    if (rt_) {
        D2D1_SIZE_U sz = D2D1::SizeU(
            static_cast<UINT32>(std::max(1, w)),
            static_cast<UINT32>(std::max(1, h)));
        rt_->Resize(sz);
    }
}

void SearchHud::update(int current_one_based, int total) {
    state_.current_one_based = current_one_based;
    state_.total             = total;
    state_.hovered_button    = -1;
    if (!hwnd_) return;
    reposition_to_parent();
    if (!IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SearchHud::clear() {
    state_ = SearchHudState{};
    if (hwnd_ && IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_HIDE);
}

void SearchHud::on_parent_resize() {
    if (!hwnd_ || !IsWindowVisible(hwnd_)) return;
    reposition_to_parent();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SearchHud::set_dark_mode(bool dark) {
    if (dark_mode_ == dark) return;
    dark_mode_ = dark;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

int SearchHud::hit_test(int x, int y) const {
    UINT dpi = hwnd_ ? GetDpiForWindow(hwnd_) : 96;
    return hit_test_button(rects_, x, y, dpi);
}

void SearchHud::update_hover(int new_hover) {
    if (state_.hovered_button == new_hover) return;
    state_.hovered_button = new_hover;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void SearchHud::paint() {
    if (!hwnd_) return;
    if (!rt_) recreate_target();
    if (!rt_) {
        ValidateRect(hwnd_, nullptr);
        return;
    }
    // rects_ was populated by the most recent update()/reposition_to_parent;
    // state_ hasn't changed since, so reuse without re-laying-out.
    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(0, 0, 0, 0));
    painter_.paint(rt_.Get(), state_, rects_, dark_mode_);
    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) rt_.Reset();
    ValidateRect(hwnd_, nullptr);
}

LRESULT SearchHud::handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SETCURSOR:
        // Parent's WlxListerineMdView serves an I-beam for text selection;
        // override with the standard arrow over the HUD's clickable surface.
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        paint();
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int hit = hit_test(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (hit < 0 || state_.total == 0) return 0;
        if (on_navigate) on_navigate(hit == 0 /*backwards*/);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int hit = hit_test(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (state_.total == 0) hit = -1;
        update_hover(hit);
        if (!tracking_mouse_) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd_;
            TrackMouseEvent(&tme);
            tracking_mouse_ = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        tracking_mouse_ = false;
        update_hover(-1);
        return 0;
    }
    case WM_DESTROY:
        rt_.Reset();
        return 0;
    }
    // Use the wndproc's hwnd, not hwnd_ — creation-time messages reach this
    // default before the constructor's hwnd_ assignment.
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace wlx::runtime::search
