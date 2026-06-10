#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/search/search_hud_painter.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>

namespace wlx::runtime::search {


class SearchHud {
public:
    SearchHud(HWND parent,
              ID2D1Factory* d2d_factory,
              IDWriteFactory* dwrite_factory,
              const theme::ThemeService& theme,
              bool dark_mode);
    ~SearchHud();

    SearchHud(const SearchHud&) = delete;
    SearchHud& operator=(const SearchHud&) = delete;

    void update(int current_one_based, int total);
    void clear();
    void on_parent_resize();
    void set_dark_mode(bool dark);

    std::function<void(bool backwards)> on_navigate;

    // Used by the global WndProc dispatcher. Don't call directly.
    // hwnd is passed explicitly: messages sent inside CreateWindowExW
    // (WM_NCCALCSIZE, WM_CREATE) arrive before hwnd_ is assigned.
    LRESULT handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    static void register_class(HMODULE module);
    static void unregister_class(HMODULE module);

private:
    void paint();
    void recreate_target();
    void reposition_to_parent();
    void update_hover(int new_hover);
    int  hit_test(int x, int y) const;

    HWND parent_ = nullptr;
    HWND hwnd_   = nullptr;
    ID2D1Factory* d2d_factory_ = nullptr;
    const theme::ThemeService* theme_ = nullptr;
    bool dark_mode_ = false;

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;

    SearchHudPainter painter_;
    SearchHudState state_;
    SearchHudHitRects rects_;
    bool tracking_mouse_ = false;
};

}  // namespace wlx::runtime::search
