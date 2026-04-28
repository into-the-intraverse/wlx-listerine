#include "search_hud_painter.h"
#include "search_counter_format.h"

#include <cmath>

using Microsoft::WRL::ComPtr;

SearchHudPainter::SearchHudPainter(IDWriteFactory* dwrite, const ThemeService& theme)
    : dwrite_(dwrite), theme_(&theme) {
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

    float text_w = measure_counter_width(dwrite_, text_format_.Get(), counter_text);

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

    // Counter text — drawn over the bar background.
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
            // Use text color tinted at low alpha so the highlight contrasts
            // with the bar's code_bg background in both light and dark themes
            // (text is always the opposite-pole hue from code_bg).
            auto hover_bg = make_brush(rt, colors.text, kHoverAlpha);
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
