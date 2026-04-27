# Search Match Counter HUD Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bottom-right HUD bar showing `current / total` search matches with clickable prev/next buttons.

**Architecture:** Two-layer split — a pure `SearchHudPainter` (D2D paint, no Win32) used by both the runtime and `screenshot_tool`, and a `SearchHud` runtime wrapper that owns a `WS_CHILD` HWND, its render target, and message handling. The painter takes a state struct + render target and produces both layout rects and pixels; the wrapper delegates paint and handles mouse messages, calling back to the host via a `std::function` for navigation.

**Tech Stack:** C++20, Direct2D, DirectWrite, Win32 (WS_CHILD), doctest, CMake/Conan, MSVC. Existing project conventions: `ComPtr<T>` for COM, `WLX_TRACE` for logging, theme palette via `ThemeService`, search via `SearchIndex` + `search_step`.

**Spec:** `docs/superpowers/specs/2026-04-27-search-match-counter-design.md`

---

## Task 1: Pure formatter (`format_counter`)

**Files:**
- Create: `src/search_counter_format.h`
- Create: `tests/test_search_counter_format.cpp`
- Modify: `CMakeLists.txt` (append test source to `add_executable(tests …)`)

- [ ] **Step 1: Write the failing tests**

Create `tests/test_search_counter_format.cpp`:

```cpp
#include <doctest/doctest.h>
#include "search_counter_format.h"

TEST_CASE("format_counter") {
    SUBCASE("normal match") {
        CHECK(format_counter(1, 27) == L"1 / 27");
    }
    SUBCASE("last match") {
        CHECK(format_counter(27, 27) == L"27 / 27");
    }
    SUBCASE("zero results") {
        CHECK(format_counter(0, 0) == L"0 / 0");
    }
    SUBCASE("large counts plain") {
        CHECK(format_counter(142, 1058) == L"142 / 1058");
    }
}
```

- [ ] **Step 2: Wire the test into CMakeLists.txt**

In `CMakeLists.txt`, locate `add_executable(tests` (line ~186) and add `tests/test_search_counter_format.cpp` to the list. Resulting block:

```cmake
add_executable(tests
    tests/test_main.cpp
    tests/test_document_model.cpp
    tests/test_theme_service.cpp
    tests/test_file_service.cpp
    tests/test_markdown_parser.cpp
    tests/test_cache_service.cpp
    tests/test_layout_engine.cpp
    tests/test_text_selection.cpp
    tests/test_search_engine.cpp
    tests/test_search_ops.cpp
    tests/test_wlx_host_common.cpp
    tests/test_search_counter_format.cpp
)
```

- [ ] **Step 3: Reconfigure and verify the test fails to compile**

Run: `cmake --build --preset conan-release --target tests 2>&1 | tail -20`

Expected: compile error — `'search_counter_format.h' file not found` or similar.

- [ ] **Step 4: Write the minimal implementation**

Create `src/search_counter_format.h`:

```cpp
#pragma once

#include <string>

inline std::wstring format_counter(int current_one_based, int total) {
    return std::to_wstring(current_one_based) + L" / " + std::to_wstring(total);
}
```

- [ ] **Step 5: Build and run tests**

Run: `cmake --build --preset conan-release --target tests`
Then: `./build/Release/tests.exe -ts="format_counter"`

Expected: 4 assertions pass, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add src/search_counter_format.h tests/test_search_counter_format.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(search): add format_counter pure formatter for HUD

Header-only `format_counter(current_one_based, total) -> wstring` produces
the `N / M` string the search HUD displays. Doctest cases guard the
format string against accidental edits.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `SearchHudPainter` — pure D2D paint layer

**Files:**
- Create: `src/search_hud_painter.h`
- Create: `src/search_hud_painter.cpp`
- Modify: `CMakeLists.txt` (append source to `add_library(wlx-core STATIC …)`)

- [ ] **Step 1: Write the painter header**

Create `src/search_hud_painter.h`:

```cpp
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
    const ThemeService* theme_ = nullptr;

    static constexpr float kPadX     = 8.0f;   // horizontal padding inside bar
    static constexpr float kPadY     = 4.0f;   // vertical padding inside bar
    static constexpr float kGap      = 4.0f;   // gap between widgets
    static constexpr float kBarH     = 28.0f;  // total bar height
    static constexpr float kPillH    = 20.0f;
    static constexpr float kPillPadX = 6.0f;   // horizontal padding inside counter pill
    static constexpr float kBtnSize  = 24.0f;
    static constexpr float kCornerR  = 4.0f;
    static constexpr float kPillCornerR = 6.0f;
    static constexpr float kFontSize = 13.0f;
    static constexpr float kPillAlpha    = 0.85f;
    static constexpr float kDisabledAlpha = 0.3f;
};
```

- [ ] **Step 2: Write the painter implementation**

Create `src/search_hud_painter.cpp`:

```cpp
#include "search_hud_painter.h"
#include "search_counter_format.h"

#include <algorithm>

using Microsoft::WRL::ComPtr;

SearchHudPainter::SearchHudPainter(IDWriteFactory* dwrite, const ThemeService& theme)
    : theme_(&theme) {
    if (!dwrite) return;
    auto family = theme.fonts().body_family;
    dwrite->CreateTextFormat(
        family.c_str(), nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        kFontSize, L"en-us",
        text_format_.GetAddressOf());
    if (text_format_) {
        text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

static float measure_counter_width(IDWriteFactory* dwrite, IDWriteTextFormat* fmt,
                                   const std::wstring& text) {
    if (!dwrite || !fmt) return 60.0f;
    ComPtr<IDWriteTextLayout> tl;
    HRESULT hr = dwrite->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.size()), fmt,
        1024.0f, 64.0f, tl.GetAddressOf());
    if (FAILED(hr) || !tl) return 60.0f;
    DWRITE_TEXT_METRICS m = {};
    tl->GetMetrics(&m);
    return std::ceil(m.width);
}

SearchHudHitRects SearchHudPainter::layout(const SearchHudState& s) const {
    SearchHudHitRects r;

    std::wstring counter_text = format_counter(s.current_one_based, s.total);

    // Re-measure counter text width with the painter's text format.
    ComPtr<IDWriteFactory> dwrite_factory;
    if (text_format_) text_format_->GetFactory(dwrite_factory.GetAddressOf());
    float text_w = measure_counter_width(dwrite_factory.Get(), text_format_.Get(), counter_text);

    float pill_w = text_w + 2 * kPillPadX;
    float pill_h = kPillH;

    float x = kPadX;
    float pill_y = kPadY + (kBarH - 2 * kPadY - pill_h) * 0.5f;
    r.counter = D2D1::RectF(x, pill_y, x + pill_w, pill_y + pill_h);

    x += pill_w + kGap;
    float btn_y = kPadY + (kBarH - 2 * kPadY - kBtnSize) * 0.5f;
    r.prev = D2D1::RectF(x, btn_y, x + kBtnSize, btn_y + kBtnSize);

    x += kBtnSize + kGap;
    r.next = D2D1::RectF(x, btn_y, x + kBtnSize, btn_y + kBtnSize);

    x += kBtnSize + kPadX;
    r.width  = x;
    r.height = kBarH;
    r.bounds = D2D1::RectF(0, 0, r.width, r.height);
    return r;
}

static void fill_rounded(ID2D1RenderTarget* rt, const D2D1_RECT_F& rect,
                         float radius, ID2D1Brush* brush) {
    D2D1_ROUNDED_RECT rr = {rect, radius, radius};
    rt->FillRoundedRectangle(rr, brush);
}

static ComPtr<ID2D1SolidColorBrush> make_brush(ID2D1RenderTarget* rt, uint32_t rgb, float alpha) {
    ComPtr<ID2D1SolidColorBrush> b;
    rt->CreateSolidColorBrush(ThemeService::to_d2d_color(rgb, alpha), b.GetAddressOf());
    return b;
}

void SearchHudPainter::paint(ID2D1RenderTarget* rt, const SearchHudState& s,
                             const SearchHudHitRects& rects, bool dark_mode) const {
    if (!rt || !theme_) return;
    const auto& colors = theme_->palette(dark_mode);
    const bool disabled = (s.total == 0);

    // Bar background: rounded rect using code_bg at full alpha so the bar reads
    // as one chip rather than three floating elements.
    auto bar_bg = make_brush(rt, colors.code_bg, kPillAlpha);
    fill_rounded(rt, rects.bounds, kPillCornerR, bar_bg.Get());

    // Counter pill — same color tone, slightly inset visually via text.
    auto text_brush = make_brush(rt, colors.text, 1.0f);
    if (text_format_ && text_brush) {
        std::wstring counter_text = format_counter(s.current_one_based, s.total);
        rt->DrawText(counter_text.c_str(), static_cast<UINT32>(counter_text.size()),
                     text_format_.Get(), rects.counter, text_brush.Get(),
                     D2D1_DRAW_TEXT_OPTIONS_NONE);
    }

    auto paint_button = [&](const D2D1_RECT_F& r, bool down_chevron, int idx) {
        const float alpha = disabled ? kDisabledAlpha : 1.0f;
        if (!disabled && s.hovered_button == idx) {
            auto hover_bg = make_brush(rt, colors.code_bg, 0.6f);
            fill_rounded(rt, r, kCornerR, hover_bg.Get());
        }
        auto stroke = make_brush(rt, colors.text, alpha);
        if (!stroke) return;
        const float cx = (r.left + r.right) * 0.5f;
        const float cy = (r.top + r.bottom) * 0.5f;
        const float s2 = 4.0f;   // chevron half-width
        const float vy = 2.5f;   // chevron half-height
        if (down_chevron) {
            // ↓: V shape
            rt->DrawLine({cx - s2, cy - vy}, {cx, cy + vy}, stroke.Get(), 1.5f);
            rt->DrawLine({cx,       cy + vy}, {cx + s2, cy - vy}, stroke.Get(), 1.5f);
        } else {
            // ↑: ^ shape
            rt->DrawLine({cx - s2, cy + vy}, {cx, cy - vy}, stroke.Get(), 1.5f);
            rt->DrawLine({cx,       cy - vy}, {cx + s2, cy + vy}, stroke.Get(), 1.5f);
        }
    };

    paint_button(rects.prev, /*down_chevron=*/false, 0);
    paint_button(rects.next, /*down_chevron=*/true,  1);
}
```

- [ ] **Step 3: Wire painter into wlx-core**

In `CMakeLists.txt` find `add_library(wlx-core STATIC` (line 38) and add the new source. Resulting block:

```cmake
add_library(wlx-core STATIC
    src/file_service.cpp
    src/markdown_parser.cpp
    src/layout_engine.cpp
    src/render_engine.cpp
    src/interaction_engine.cpp
    src/theme_service.cpp
    src/cache_service.cpp
    src/text_selection.cpp
    src/search_engine.cpp
    src/search_hud_painter.cpp
)
```

- [ ] **Step 4: Build the static library**

Run: `cmake --build --preset conan-release --target wlx-core`

Expected: builds clean with no warnings or errors.

- [ ] **Step 5: Commit**

```bash
git add src/search_hud_painter.h src/search_hud_painter.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(search): add SearchHudPainter (pure D2D paint, no Win32)

Pure paint layer for the search HUD: takes a state struct and an
ID2D1RenderTarget, produces the bar pixels (rounded counter pill +
chevron prev/next buttons). Used by both the runtime SearchHud HWND
wrapper and screenshot_tool's bitmap render path.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: `SearchHud` — runtime HWND wrapper

**Files:**
- Create: `src/search_hud.h`
- Create: `src/search_hud.cpp`
- Modify: `CMakeLists.txt` (append source to `add_library(wlx-listerine-md SHARED …)`)

- [ ] **Step 1: Write the HUD header**

Create `src/search_hud.h`:

```cpp
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "search_hud_painter.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <memory>

class SearchHud {
public:
    SearchHud(HWND parent,
              ID2D1Factory* d2d_factory,
              IDWriteFactory* dwrite_factory,
              const ThemeService& theme,
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
    LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp);

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
    IDWriteFactory* dwrite_factory_ = nullptr;
    const ThemeService* theme_ = nullptr;
    bool dark_mode_ = false;

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;

    SearchHudPainter painter_;
    SearchHudState state_;
    SearchHudHitRects rects_;
    bool tracking_mouse_ = false;
};
```

- [ ] **Step 2: Write the HUD implementation**

Create `src/search_hud.cpp`:

```cpp
#include "search_hud.h"

#include <windowsx.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;

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
    return self->handle_message(msg, wp, lp);
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
    RegisterClassExW(&wc);
}

void SearchHud::unregister_class(HMODULE module) {
    UnregisterClassW(kClassName, module);
}

SearchHud::SearchHud(HWND parent, ID2D1Factory* d2d_factory,
                     IDWriteFactory* dwrite_factory, const ThemeService& theme,
                     bool dark_mode)
    : parent_(parent),
      d2d_factory_(d2d_factory),
      dwrite_factory_(dwrite_factory),
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
    d2d_factory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd_, size),
        rt_.GetAddressOf());
}

void SearchHud::reposition_to_parent() {
    if (!hwnd_ || !parent_) return;
    rects_ = painter_.layout(state_);
    int w = static_cast<int>(std::ceil(rects_.width));
    int h = static_cast<int>(std::ceil(rects_.height));
    RECT pr;
    GetClientRect(parent_, &pr);
    int x = pr.right - w - kMargin;
    int y = pr.bottom - h - kMargin;
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
    auto inside = [&](const D2D1_RECT_F& r) {
        return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
    };
    if (inside(rects_.prev)) return 0;
    if (inside(rects_.next)) return 1;
    return -1;
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
    rects_ = painter_.layout(state_);
    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(0, 0, 0, 0));
    painter_.paint(rt_.Get(), state_, rects_, dark_mode_);
    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) rt_.Reset();
    ValidateRect(hwnd_, nullptr);
}

LRESULT SearchHud::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
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
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
```

- [ ] **Step 3: Wire HUD into the plugin SHARED target**

In `CMakeLists.txt` find `add_library(wlx-listerine-md SHARED` (line 80) and add the new source. The full block (current contents below — append `src/search_hud.cpp`):

```cmake
add_library(wlx-listerine-md SHARED
    src/host_adapter.cpp
    src/host_integration.cpp
    src/search_hud.cpp
    plugin.def
)
```

(If the existing block has different sources, just append `src/search_hud.cpp` to whatever list is there.)

- [ ] **Step 4: Build the plugin**

Run: `cmake --build --preset conan-release --target wlx-listerine-md`

Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add src/search_hud.h src/search_hud.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(search): add SearchHud HWND wrapper for the match counter HUD

WS_CHILD HWND with WS_EX_NOACTIVATE that delegates paint to
SearchHudPainter and routes hover / click messages. Posts navigation
intent (prev/next) back to the host via a std::function callback;
neutral to ViewState. Window class registered/unregistered via static
methods called from DllMain.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Host integration

**Files:**
- Modify: `src/host_adapter.cpp` (multiple sites — see steps below)

- [ ] **Step 1: Add `hud` member to `ViewState`**

In `src/host_adapter.cpp`, locate the `ViewState` struct (line ~42) and add the include + member.

Top of file, alongside other includes:

```cpp
#include "search_hud.h"
```

Inside `struct ViewState { ... }`, after the `interaction` line:

```cpp
    std::unique_ptr<RenderEngine> renderer;
    std::unique_ptr<InteractionEngine> interaction;
    std::unique_ptr<SearchHud> hud;
```

- [ ] **Step 2: Register the HUD window class in DllMain**

In `DllMain` (line ~760), in the `DLL_PROCESS_ATTACH` branch, after `g_hModule = hModule;`:

```cpp
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        SearchHud::register_class(hModule);
        break;
```

In `DLL_PROCESS_DETACH`, in the `if (reserved == nullptr)` block alongside `UnregisterClassW(L"WlxListerineMdView", g_hModule);`:

```cpp
        if (reserved == nullptr) {
            g_integration.emergency_cleanup();
            if (g_window_class) {
                UnregisterClassW(L"WlxListerineMdView", g_hModule);
                g_window_class = 0;
            }
            SearchHud::unregister_class(g_hModule);
        }
```

- [ ] **Step 3: Construct the HUD in `ListLoadW`**

In `ListLoadW` (line ~862), after the renderer is constructed and `create_device_resources` is called, before `load_document(vs, FileToLoad)`:

```cpp
    // Create render engine
    vs->renderer = std::make_unique<RenderEngine>(
        g_d2d_factory.Get(), g_dwrite_factory.Get(), g_theme, dark);
    vs->renderer->create_device_resources(hwnd);

    // Create search HUD (initially hidden)
    vs->hud = std::make_unique<SearchHud>(
        hwnd, g_d2d_factory.Get(), g_dwrite_factory.Get(), g_theme, dark);
    vs->hud->on_navigate = [vs](bool backwards) {
        if (vs->matches.empty() || !vs->layout) return;
        SearchQuery q = vs->last_query;
        q.backwards = backwards;
        auto r = search_step(*vs, q, /*findfirst=*/false);
        if (!r.has_match) return;
        scroll_to_match(vs, r.matches[r.cursor]);
        if (vs->renderer) vs->renderer->set_search_matches(r.matches, r.cursor);
        vs->hud->update(r.cursor + 1, static_cast<int>(r.matches.size()));
        InvalidateRect(vs->hwnd, nullptr, FALSE);
    };

    // Load and render document
    load_document(vs, FileToLoad);
```

- [ ] **Step 4: Hook the HUD into the search success path**

In `ListSearchTextW` (line ~1014), at the success path (line ~1041–1042) — after `scroll_to_match` and before `InvalidateRect`:

```cpp
    scroll_to_match(vs, r.matches[r.cursor]);
    if (vs->renderer) vs->renderer->set_search_matches(r.matches, r.cursor);
    if (vs->hud) vs->hud->update(r.cursor + 1, static_cast<int>(r.matches.size()));
    InvalidateRect(vs->hwnd, nullptr, FALSE);
    return LISTPLUGIN_OK;
```

In the no-match path (line ~1037), before `InvalidateRect`:

```cpp
    auto r = search_step(*vs, q, findfirst);
    if (!r.has_match) {
        if (vs->renderer) vs->renderer->set_search_matches({}, -1);
        if (vs->hud) vs->hud->update(0, 0);
        InvalidateRect(vs->hwnd, nullptr, FALSE);
        return LISTPLUGIN_ERROR;
    }
```

- [ ] **Step 5: Hook the HUD into the Esc handler**

In the Esc handler (line ~665, the `else if (!vs->matches.empty())` branch), after the renderer's matches are cleared:

```cpp
            } else if (!vs->matches.empty()) {
                vs->matches.clear();
                vs->current_match = -1;
                if (vs->renderer) vs->renderer->set_search_matches({}, -1);
                if (vs->hud) vs->hud->clear();
                InvalidateRect(vs->hwnd, nullptr, FALSE);
                handled = true;
            }
```

- [ ] **Step 6: Hook the HUD into `ListLoadNextW` and the Esc-without-search path**

In `ListLoadNextW` (line ~911), inside the function, after dark-mode handling and before `load_document`:

```cpp
    if (new_dark != vs->dark_mode) {
        vs->dark_mode = new_dark;
        vs->renderer->set_dark_mode(new_dark);
        if (vs->hud) vs->hud->set_dark_mode(new_dark);
        apply_dark_mode(vs->hwnd, new_dark);
    }
    vs->wrap_text = new_wrap;

    // New file — reset search state
    if (vs->hud) vs->hud->clear();

    load_document(vs, FileToLoad);
```

There is also a *second* dark-mode propagation path in `ListSendCommand`'s `lc_newparams` branch (around line 1006) — TC pushes runtime parameter changes (including dark-mode) through that command rather than `ListLoadNextW`. Add the HUD propagation there too:

```cpp
    case lc_newparams: {
        bool new_dark = (Parameter & lcp_darkmode) != 0;
        ...
        if (new_dark != vs->dark_mode) {
            vs->dark_mode = new_dark;
            vs->renderer->set_dark_mode(new_dark);
            if (vs->hud) vs->hud->set_dark_mode(new_dark);
            need_relayout = true;
        }
        ...
    }
```

Additionally, locate the existing search-clear helper near line ~196 (where `vs->matches.clear()` appears with `vs->current_match = -1` and `vs->renderer->set_search_matches({}, -1)`) and add a HUD clear there too:

```cpp
    vs->matches.clear();
    vs->current_match = -1;
    // Also clear matches in the renderer: its search_matches_ still holds
    // ... (existing comment)
    if (vs->renderer) vs->renderer->set_search_matches({}, -1);
    if (vs->hud) vs->hud->clear();
```

- [ ] **Step 7: Hook `WM_SIZE`**

In the `WM_SIZE` handler (line ~369):

```cpp
    case WM_SIZE: {
        UINT w = LOWORD(lp);
        UINT h = HIWORD(lp);
        if (vs && vs->renderer && w > 0 && h > 0) {
            vs->renderer->resize(w, h);
            do_layout(vs);
            if (vs->hud) vs->hud->on_parent_resize();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
```

- [ ] **Step 8: Build the plugin and verify the existing tests still pass**

Run: `cmake --build --preset conan-release`
Then: `./build/Release/tests.exe`

Expected: all existing tests pass (plus the 4 new formatter tests from Task 1). Total assertion count up by 4 from baseline.

- [ ] **Step 9: Commit**

```bash
git add src/host_adapter.cpp
git commit -m "$(cat <<'EOF'
feat(search): wire SearchHud into per-window state and search lifecycle

ViewState owns a unique_ptr<SearchHud>; ListLoadW constructs it and sets
the on_navigate callback; ListSearchTextW updates it on every step
(including 0/0 for no-match queries); Esc / ListLoadNextW clear it;
WM_SIZE repositions it. Window class registered in DllMain alongside
WlxListerineMdView.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: `screenshot_tool` — `--search` and `--search-step` flags

**Files:**
- Modify: `src/render_engine.h` (add accessor)
- Modify: `src/screenshot_main.cpp`

- [ ] **Step 1: Expose the render target on `RenderEngine`**

In `src/render_engine.h`, in the public section (after `dip_height()` near line 43):

```cpp
    float dip_height() const;
    // Convert pixel coordinate to DIP coordinate
    float pixel_to_dip_x(float px) const;
    float pixel_to_dip_y(float py) const;

    // Underlying D2D target. Used by tools that need to composite an overlay
    // (e.g. screenshot_tool painting the search HUD on top of the document).
    ID2D1RenderTarget* render_target() const { return rt_.Get(); }
```

- [ ] **Step 2: Add `Options` fields and CLI parsing**

In `src/screenshot_main.cpp`, extend the `Options` struct (line ~29):

```cpp
struct Options {
    std::wstring input_path;
    std::wstring config_path = L"config/wlx-listerine-md.toml";
    int width = 800;
    int height = 600;
    float scroll = 0;
    bool full = false;
    bool dark = false;
    bool bench = false;

    std::wstring search;     // empty == no search
    int search_step = 0;     // number of post-findfirst advancements
};
```

In `print_usage()` (line ~93), append two lines before the closing `");`:

```cpp
        "  --dark           Force dark mode\n"
        "  --bench          Print timing and memory stats\n"
        "  --search <term>  Run a search for <term> after layout\n"
        "  --search-step N  Advance the search cursor by N steps (default 0)\n");
```

In `parse_args()` (line ~114), add two `else if` branches before the unknown-option `else`:

```cpp
        } else if (std::strcmp(argv[i], "--bench") == 0) {
            opts.bench = true;
        } else if (std::strcmp(argv[i], "--search") == 0 && i + 1 < argc) {
            opts.search = to_wstring(argv[++i]);
        } else if (std::strcmp(argv[i], "--search-step") == 0 && i + 1 < argc) {
            opts.search_step = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return false;
        }
```

- [ ] **Step 3: Add includes for the search and HUD pieces**

Near the top of `src/screenshot_main.cpp`, alongside other `#include` statements:

```cpp
#include "search_engine.h"
#include "search_hud_painter.h"
```

- [ ] **Step 4: Run the search and paint HUD overlay**

In the rendering pipeline, after `renderer.paint(layout, scroll_y);` (line ~269) and before `if (renderer.needs_recreate()) { … }`, insert:

```cpp
        // Optional: run a search and overlay the HUD.
        if (!opts.search.empty()) {
            SearchIndex idx;
            idx.build(layout);
            SearchQuery q;
            q.needle      = opts.search;
            q.match_case  = false;
            q.whole_words = false;
            q.backwards   = false;
            auto matches = idx.find_all(q);

            int total = static_cast<int>(matches.size());
            int cursor = -1;
            if (total > 0) {
                cursor = std::min(opts.search_step, total - 1);
            }
            renderer.set_search_matches(matches, cursor);

            // Re-paint document with match highlights baked in.
            renderer.paint(layout, scroll_y);

            // Overlay the HUD via the painter.
            ID2D1RenderTarget* rt = renderer.render_target();
            if (rt) {
                SearchHudPainter hud_painter(dwrite_factory.Get(), theme);
                SearchHudState s{};
                s.current_one_based = (total > 0) ? (cursor + 1) : 0;
                s.total             = total;
                auto rects = hud_painter.layout(s);

                rt->BeginDraw();
                D2D1_MATRIX_3X2_F prev_xform;
                rt->GetTransform(&prev_xform);
                float bx = static_cast<float>(bmp_width)  - rects.width  - 12.0f;
                float by = static_cast<float>(bmp_height) - rects.height - 12.0f;
                rt->SetTransform(D2D1::Matrix3x2F::Translation(bx, by));
                hud_painter.paint(rt, s, rects, opts.dark);
                rt->SetTransform(prev_xform);
                HRESULT hr_e = rt->EndDraw();
                if (FAILED(hr_e)) {
                    std::fprintf(stderr, "HUD overlay EndDraw failed: 0x%08lx\n", hr_e);
                }
            }
        }
```

- [ ] **Step 5: Build the screenshot tool**

Run: `cmake --build --preset conan-release --target screenshot_tool`

Expected: builds clean.

- [ ] **Step 6: Smoke test the new flags**

Run: `./build/Release/screenshot_tool.exe test_data/cases/01_headings_atx.md --search heading --full`

Expected: command exits 0 and produces `test_data/cases/01_headings_atx.png` with the search HUD bar visible in the bottom-right corner showing `1 / N` (where N is the number of "heading" matches in that document).

Visual check: open the PNG and confirm the bar appears at the bottom-right with a counter pill and two chevron buttons. (Restore the file afterwards: `git checkout -- test_data/cases/01_headings_atx.png` once the comparison case 28 has its own input.)

- [ ] **Step 7: Commit**

```bash
git add src/render_engine.h src/screenshot_main.cpp
git commit -m "$(cat <<'EOF'
feat(screenshot_tool): --search and --search-step flags with HUD overlay

After document paint, when --search is set, runs SearchIndex against the
layout, advances the cursor by --search-step, paints match highlights,
and composites the SearchHudPainter output onto the same WIC bitmap at
the bottom-right corner. Adds RenderEngine::render_target() accessor so
overlays can share the underlying D2D target.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Visual regression infrastructure (`.flags` + `_golden.png`)

**Files:**
- Modify: `scripts/visual-test.sh`
- Modify: `test_data/compare.py`
- Modify: `scripts/update-goldens.ts`

- [ ] **Step 1: Teach `visual-test.sh` to read `.flags` sidecars**

In `scripts/visual-test.sh`, locate the loop that invokes `screenshot_tool` (line ~25):

```bash
for md_file in "$CASES_DIR"/*.md; do
    name="$(basename "$md_file" .md)"
    if "$SCREENSHOT_TOOL" "$md_file" --full > /dev/null 2>&1; then
        echo "  OK   $name"
        gen_ok=$((gen_ok + 1))
    else
        echo "  ERR  $name"
        gen_fail=$((gen_fail + 1))
    fi
done
```

Replace with:

```bash
for md_file in "$CASES_DIR"/*.md; do
    name="$(basename "$md_file" .md)"
    flags_file="$CASES_DIR/${name}.flags"
    extra_args=()
    if [[ -f "$flags_file" ]]; then
        # shellcheck disable=SC2207
        extra_args=($(cat "$flags_file"))
    fi
    if "$SCREENSHOT_TOOL" "$md_file" --full "${extra_args[@]}" > /dev/null 2>&1; then
        echo "  OK   $name"
        gen_ok=$((gen_ok + 1))
    else
        echo "  ERR  $name"
        gen_fail=$((gen_fail + 1))
    fi
done
```

- [ ] **Step 2: Teach `compare.py` to prefer `_golden.png` and use a stricter threshold**

In `test_data/compare.py`, locate the `main()` loop (line ~85). Replace the per-case logic block (the section that resolves `chrome_path`, checks `if not chrome_path.exists():`, and computes `similarity`):

```python
    for md_file in cases:
        name = md_file.stem
        if filter_name and filter_name not in name:
            continue

        ours_path = CASES_DIR / f"{name}.png"
        golden_path = CASES_DIR / f"{name}_golden.png"
        chrome_path = CASES_DIR / f"{name}_chrome.png"

        if not ours_path.exists():
            print(f"  SKIP  {name} (no tool screenshot)")
            continue

        if golden_path.exists():
            ref_path = golden_path
            threshold = 99.5
        elif chrome_path.exists():
            ref_path = chrome_path
            threshold = 95.0
        else:
            print(f"  SKIP  {name} (no reference image)")
            continue

        similarity, diff_img = compare_images(ours_path, ref_path)
        diff_path = CASES_DIR / f"{name}_diff.png"
        diff_img.save(diff_path)

        status = "PASS" if similarity >= threshold else "WARN" if similarity >= threshold - 15 else "FAIL"
        results.append((name, similarity, status))
        print(f"  {status}  {similarity:5.1f}%  {name}")
```

In the trailing summary, update the threshold message (line ~115) to be threshold-agnostic:

```python
    avg = sum(s for _, s, _ in results) / len(results)
    passes = sum(1 for _, _, st in results if st == "PASS")
    fails = sum(1 for _, _, st in results if st == "FAIL")
    print(f"\n  {passes}/{len(results)} pass, avg similarity: {avg:.1f}%")
```

- [ ] **Step 3: Teach `update-goldens.ts` to handle `.flags` cases**

In `scripts/update-goldens.ts`, after the `main()` `for (const file of files)` loop opens (line ~75), add a `.flags` short-circuit at the very top of the loop body — *before* the Playwright page is created. Use Bun's `$` shell or `Bun.spawn` to invoke the screenshot tool.

Append this import near the top:

```ts
import { spawnSync } from "child_process";
import { copyFileSync } from "fs";
```

Inside the loop, before `const page = await context.newPage();`:

```ts
  for (const file of files) {
    const name = basename(file, ".md");

    // Self-snapshot path: case has a `.flags` sidecar listing extra
    // screenshot_tool args. Skip Playwright; run the tool, copy output
    // to <name>_golden.png.
    const flagsPath = join(CASES_DIR, `${name}.flags`);
    if (existsSync(flagsPath)) {
      const flags = readFileSync(flagsPath, "utf8").trim().split(/\s+/).filter(Boolean);
      const tool = join(ROOT, "build", "Release", "screenshot_tool.exe");
      const mdPath = join(CASES_DIR, file);
      const result = spawnSync(tool, [mdPath, "--full", ...flags], { stdio: "inherit" });
      if (result.status !== 0) {
        console.error(`  FAIL  ${name} (screenshot_tool exited ${result.status})`);
        continue;
      }
      const ourPng = join(CASES_DIR, `${name}.png`);
      const goldenPng = join(CASES_DIR, `${name}_golden.png`);
      copyFileSync(ourPng, goldenPng);
      console.log(`  OK    ${name} -> ${name}_golden.png`);
      continue;
    }

    const page = await context.newPage();
```

- [ ] **Step 4: Smoke test the infra changes against the existing suite**

Run: `./scripts/visual-test.sh`

Expected: existing 27 cases continue to pass (similarity ≥95%, comparing against `_chrome.png` since none have `.flags` yet). The script's threshold-agnostic summary line still appears.

- [ ] **Step 5: Commit**

```bash
git add scripts/visual-test.sh test_data/compare.py scripts/update-goldens.ts
git commit -m "$(cat <<'EOF'
test(visual): support .flags sidecars and self-snapshot _golden.png references

Cases that ship a <name>.flags one-line sidecar get those args appended
to the screenshot_tool invocation. compare.py prefers <name>_golden.png
over <name>_chrome.png when present and uses a stricter 99.5% threshold
for self-snapshots. update-goldens.ts skips Playwright for .flags cases
and copies the tool output to <name>_golden.png. Existing Chrome cases
unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Visual regression case `28_search_counter`

**Files:**
- Create: `test_data/cases/28_search_counter.md`
- Create: `test_data/cases/28_search_counter.flags`
- Create: `test_data/cases/28_search_counter_golden.png` (generated)

- [ ] **Step 1: Write the case input**

Create `test_data/cases/28_search_counter.md`:

```markdown
# The example document

This is the first paragraph. The word "the" appears here several times,
including this one and the next one and the one after that.

## Section the second

A list of the things to remember:

- the first thing
- the second thing
- the third thing

> The blockquote also mentions the term, just to spread the matches
> across multiple block types in the rendered output.

```python
# Even the code block has the keyword: "the"
def the_function():
    return "the value"
```

The final paragraph closes the document with one more occurrence of the
target term.
```

- [ ] **Step 2: Write the flags sidecar**

Create `test_data/cases/28_search_counter.flags` (a single line, no trailing newline issues — content matters, exact whitespace not):

```
--search the --search-step 2
```

- [ ] **Step 3: Build the tool and generate the golden**

Run: `cmake --build --preset conan-release --target screenshot_tool`
Then: `bun run update-goldens -- 28_search_counter`

Expected: tool runs, prints `OK    28_search_counter -> 28_search_counter_golden.png`, and writes:
- `test_data/cases/28_search_counter.png` (transient — the per-run output)
- `test_data/cases/28_search_counter_golden.png` (the committed reference)

- [ ] **Step 4: Eyeball-verify the golden**

Open `test_data/cases/28_search_counter_golden.png` and confirm:

1. The document is rendered with multiple yellow `the` highlights spread across heading / paragraph / list / blockquote / code block.
2. The HUD bar appears at the bottom-right with a counter `3 / N` (where N is the total match count — likely 15+).
3. Two chevron buttons (up = prev, down = next) are visible to the right of the counter.
4. The bar's background is the theme's `code_bg` color, semi-transparent over any underlying content.

If anything looks wrong, fix the underlying issue in earlier tasks (most likely Task 2 painter or Task 5 overlay positioning) and regenerate.

- [ ] **Step 5: Run the visual suite to confirm the golden compares against itself**

Run: `./scripts/visual-test.sh`

Expected: all 27 existing Chrome-comparison cases pass at ≥95%, and the new `28_search_counter` case passes at ≥99.5% (compare.py picks up `_golden.png`).

- [ ] **Step 6: Commit**

```bash
git add test_data/cases/28_search_counter.md \
        test_data/cases/28_search_counter.flags \
        test_data/cases/28_search_counter_golden.png
git commit -m "$(cat <<'EOF'
test(visual): add 28_search_counter visual regression case

First case with a .flags sidecar (--search the --search-step 2) and
a self-snapshot _golden.png reference. Exercises the search HUD in the
bottom-right plus match highlights across heading / paragraph / list /
blockquote / code-block contexts.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Final smoke and integration verification

**Files:** none (verification only)

- [ ] **Step 1: Full clean build**

Run: `cmake --build --preset conan-release`

Expected: every target builds with no warnings.

- [ ] **Step 2: Run all unit tests**

Run: `./build/Release/tests.exe`

Expected: all assertions pass. Compare to the pre-task baseline (CLAUDE.md says 86 tests). New test count: 86 + 4 (the format_counter subcases). Total `[doctest]` summary line should report a number ≥ 90 assertions, all passing.

- [ ] **Step 3: Run the visual regression suite**

Run: `./scripts/visual-test.sh`

Expected: 28 cases compared. 27 against `_chrome.png` (pass at ≥95%), 1 against `_golden.png` (pass at ≥99.5%). No FAILs.

- [ ] **Step 4: Manual TC verification (single round)**

This step requires Total Commander. The plan does not block on it, but flags it as a release-blocker for the user to run before merging:

1. Copy `output/wlx-listerine-md.wlx64` (or wherever the build deposits it) into TC's plugin directory and restart TC if needed.
2. Open a markdown file in lister.
3. Press F7 (or whatever opens TC's find bar) and search for a common word like `the`.
4. Confirm: bottom-right HUD appears with `1 / N`. Press F3 to advance — counter increments. Press Esc — HUD disappears.
5. Click the `↓` chevron in the HUD — counter increments and the renderer advances. TC's find bar should retain keyboard focus (i.e. the next typed character goes back into the find bar, not into nowhere).
6. Search for a string that produces 0 matches — HUD shows `0 / 0`, chevrons greyed out, clicking does nothing.
7. Open a different markdown file (TC's "Next file") — HUD disappears.

If any of those misbehave, file follow-up bugs; do not destructively revert the work.

- [ ] **Step 5: Final no-op commit / verification only**

No commit at this step — it's a checklist gate. Update task status to completed once all checks pass.

---

## Done

When all 8 tasks are committed and verified, the feature is shippable. Update `README.md` TODO list (line 58 in the version at commit `397a6bc`) to remove the "Search match counter" bullet — but only after the manual TC verification passes. That removal is its own small commit:

```bash
# After README.md edit removing the search-counter TODO bullet:
git add README.md
git commit -m "docs: README — drop search match counter TODO (now shipped)"
```
