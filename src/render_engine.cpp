#include "render_engine.h"

#include <algorithm>

RenderEngine::RenderEngine(ID2D1Factory* d2d_factory, IDWriteFactory* dwrite_factory,
                           const ThemeService& theme, bool dark_mode)
    : d2d_factory_(d2d_factory)
    , dwrite_factory_(dwrite_factory)
    , theme_(theme)
    , dark_mode_(dark_mode) {

    dwrite_factory_->CreateTextFormat(
        theme_.fonts().body_family.c_str(), nullptr,
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        theme_.fonts().body_size, L"", bullet_format_.GetAddressOf());
}

void RenderEngine::set_dark_mode(bool dark) {
    dark_mode_ = dark;
    brush_cache_.clear();
}

HRESULT RenderEngine::create_device_resources(HWND hwnd) {
    if (rt_) return S_OK;

    RECT rc;
    GetClientRect(hwnd, &rc);
    width_ = static_cast<UINT>(rc.right - rc.left);
    height_ = static_cast<UINT>(rc.bottom - rc.top);

    D2D1_SIZE_U size = D2D1::SizeU(width_, height_);
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(hwnd, size);

    ComPtr<ID2D1HwndRenderTarget> hwnd_rt;
    HRESULT hr = d2d_factory_->CreateHwndRenderTarget(rtProps, hwndProps, hwnd_rt.GetAddressOf());
    if (SUCCEEDED(hr)) {
        rt_ = hwnd_rt;
        is_hwnd_target_ = true;
    }
    needs_recreate_ = false;
    return hr;
}

HRESULT RenderEngine::create_bitmap_resources(IWICImagingFactory* wic_factory, int width, int height) {
    if (rt_) return S_OK;

    width_ = static_cast<UINT>(width);
    height_ = static_cast<UINT>(height);

    HRESULT hr = wic_factory->CreateBitmap(
        width_, height_,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapCacheOnDemand,
        wic_bitmap_.GetAddressOf());
    if (FAILED(hr)) return hr;

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    hr = d2d_factory_->CreateWicBitmapRenderTarget(
        wic_bitmap_.Get(), rtProps, rt_.GetAddressOf());
    if (FAILED(hr)) {
        wic_bitmap_.Reset();
        return hr;
    }

    is_hwnd_target_ = false;
    needs_recreate_ = false;
    return hr;
}

HRESULT RenderEngine::save_to_png(IWICImagingFactory* wic_factory, const wchar_t* path) {
    if (!wic_bitmap_) return E_FAIL;

    ComPtr<IWICStream> stream;
    HRESULT hr = wic_factory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapEncoder> encoder;
    hr = wic_factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->SetSize(width_, height_);
    if (FAILED(hr)) return hr;

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) return hr;

    hr = frame->WriteSource(wic_bitmap_.Get(), nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->Commit();
    if (FAILED(hr)) return hr;

    return encoder->Commit();
}

void RenderEngine::discard_device_resources() {
    rt_.Reset();
    wic_bitmap_.Reset();
    brush_cache_.clear();
    is_hwnd_target_ = false;
}

void RenderEngine::resize(UINT width, UINT height) {
    width_ = width;
    height_ = height;
    if (rt_ && is_hwnd_target_) {
        ComPtr<ID2D1HwndRenderTarget> hwnd_rt;
        if (SUCCEEDED(rt_.As(&hwnd_rt))) {
            hwnd_rt->Resize(D2D1::SizeU(width, height));
        }
    }
}

float RenderEngine::dip_width() const {
    if (rt_) {
        D2D1_SIZE_F size = rt_->GetSize();
        return size.width;
    }
    return static_cast<float>(width_);
}

float RenderEngine::dip_height() const {
    if (rt_) {
        D2D1_SIZE_F size = rt_->GetSize();
        return size.height;
    }
    return static_cast<float>(height_);
}

float RenderEngine::pixel_to_dip_x(float px) const {
    if (rt_ && width_ > 0) {
        D2D1_SIZE_F size = rt_->GetSize();
        return px * size.width / static_cast<float>(width_);
    }
    return px;
}

float RenderEngine::pixel_to_dip_y(float py) const {
    if (rt_ && height_ > 0) {
        D2D1_SIZE_F size = rt_->GetSize();
        return py * size.height / static_cast<float>(height_);
    }
    return py;
}

ID2D1SolidColorBrush* RenderEngine::get_brush(uint32_t color) {
    auto it = brush_cache_.find(color);
    if (it != brush_cache_.end())
        return it->second.Get();

    if (!rt_) return nullptr;

    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(ThemeService::to_d2d_color(color), brush.GetAddressOf());
    auto* ptr = brush.Get();
    brush_cache_[color] = std::move(brush);
    return ptr;
}

void RenderEngine::paint(const LayoutDocument& layout, float scroll_y,
                          TextPosition sel_start, TextPosition sel_end) {
    if (!rt_) return;

    const auto& colors = theme_.palette(dark_mode_);
    float viewport_h = dip_height();

    rt_->BeginDraw();
    rt_->Clear(ThemeService::to_d2d_color(colors.background));

    if (sel_start.valid() && sel_end.valid() && sel_end < sel_start)
        std::swap(sel_start, sel_end);

    // Apply scroll transform
    rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, -scroll_y));

    for (int block_idx = 0; block_idx < static_cast<int>(layout.blocks.size()); block_idx++) {
        auto& block = layout.blocks[block_idx];
        // Visibility culling
        float block_top = block.rect.top - scroll_y;
        float block_bottom = block.rect.bottom - scroll_y;
        if (block_bottom < 0) continue;
        if (block_top > viewport_h) continue;

        paint_block_background(block, 0);
        paint_selection_highlight(block, block_idx, 0, sel_start, sel_end);
        paint_block_decoration(block, 0);
        paint_bullet(block, 0);
        paint_text_runs(block, 0);
    }

    rt_->SetTransform(D2D1::Matrix3x2F::Identity());

    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        discard_device_resources();
        needs_recreate_ = true;
    }
}

void RenderEngine::paint_selection_highlight(const LayoutBlock& block, int block_index,
                                              float offset_y, TextPosition sel_start,
                                              TextPosition sel_end) {
    if (!sel_start.valid() || !sel_end.valid()) return;
    if (sel_start == sel_end) return;
    if (block_index < sel_start.block_index || block_index > sel_end.block_index) return;

    const auto& colors = theme_.palette(dark_mode_);
    auto* brush = get_brush(colors.selection);
    if (!brush) return;

    bool fully_inside = (block_index > sel_start.block_index && block_index < sel_end.block_index);

    if (fully_inside || block.text_runs.empty()) {
        D2D1_RECT_F r = block.rect;
        r.top += offset_y;
        r.bottom += offset_y;
        rt_->FillRectangle(r, brush);
        return;
    }

    auto& run = block.text_runs[0];
    if (!run.layout) return;

    int text_len = 0;
    for (auto& tr : block.text_runs) text_len += static_cast<int>(tr.text.size());

    int from = 0;
    int to = text_len;

    if (block_index == sel_start.block_index)
        from = std::clamp(sel_start.char_offset, 0, text_len);
    if (block_index == sel_end.block_index)
        to = std::clamp(sel_end.char_offset, 0, text_len);

    if (from >= to) return;

    UINT32 range_start = static_cast<UINT32>(from);
    UINT32 range_len = static_cast<UINT32>(to - from);

    UINT32 count = 0;
    run.layout->HitTestTextRange(range_start, range_len, run.rect.left, run.rect.top + offset_y,
                                  nullptr, 0, &count);
    if (count == 0) return;

    std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
    run.layout->HitTestTextRange(range_start, range_len, run.rect.left, run.rect.top + offset_y,
                                  metrics.data(), count, &count);

    for (UINT32 i = 0; i < count; i++) {
        D2D1_RECT_F r = D2D1::RectF(
            metrics[i].left, metrics[i].top,
            metrics[i].left + metrics[i].width,
            metrics[i].top + metrics[i].height);
        rt_->FillRectangle(r, brush);
    }
}

void RenderEngine::paint_block_background(const LayoutBlock& block, float offset_y) {
    if (!block.has_background) return;

    auto* brush = get_brush(block.background_color);
    if (!brush) return;

    D2D1_RECT_F r = block.rect;
    r.top += offset_y;
    r.bottom += offset_y;

    // Rounded rect for code blocks
    if (block.type == BlockType::CodeFence) {
        D2D1_ROUNDED_RECT rr = {r, 4.0f, 4.0f};
        rt_->FillRoundedRectangle(rr, brush);
    } else {
        rt_->FillRectangle(r, brush);
    }
}

void RenderEngine::paint_block_decoration(const LayoutBlock& block, float offset_y) {
    // Left border (blockquote)
    if (block.has_left_border) {
        auto* brush = get_brush(block.left_border_color);
        if (brush) {
            float bw = theme_.spacing().quote_border_width;
            D2D1_RECT_F border_rect = D2D1::RectF(
                block.rect.left, block.rect.top + offset_y,
                block.rect.left + bw, block.rect.bottom + offset_y);
            rt_->FillRectangle(border_rect, brush);
        }
    }

    // Bottom rule (HR, H1/H2, table cells)
    if (block.has_bottom_rule) {
        auto* brush = get_brush(block.bottom_rule_color);
        if (brush) {
            float rule_y = block.rect.bottom + offset_y;
            rt_->DrawLine(
                D2D1::Point2F(block.rect.left, rule_y),
                D2D1::Point2F(block.rect.right, rule_y),
                brush, 1.0f);
        }
    }
}

void RenderEngine::paint_bullet(const LayoutBlock& block, float offset_y) {
    if (block.bullet_text.empty()) return;

    auto* brush = get_brush(block.bullet_color);
    if (!brush || !bullet_format_) return;

    D2D1_POINT_2F origin = block.bullet_pos;
    origin.y += offset_y;

    // Use DrawText for the bullet character
    D2D1_RECT_F rect = D2D1::RectF(
        origin.x, origin.y,
        origin.x + theme_.spacing().list_indent, origin.y + theme_.fonts().body_size * 2);

    rt_->DrawText(
        block.bullet_text.c_str(),
        static_cast<UINT32>(block.bullet_text.size()),
        bullet_format_.Get(),
        rect, brush);
}

void RenderEngine::paint_text_runs(const LayoutBlock& block, float offset_y) {
    for (auto& run : block.text_runs) {
        if (!run.layout) continue;

        auto* brush = get_brush(run.color);
        if (!brush) continue;

        // Draw inline code backgrounds
        if (!run.code_bg_rects.empty()) {
            auto* code_bg_brush = get_brush(theme_.palette(dark_mode_).code_bg);
            if (code_bg_brush) {
                for (auto& bg : run.code_bg_rects) {
                    D2D1_ROUNDED_RECT rr;
                    rr.rect = D2D1::RectF(
                        run.rect.left + bg.rect.left,
                        run.rect.top + offset_y + bg.rect.top,
                        run.rect.left + bg.rect.right,
                        run.rect.top + offset_y + bg.rect.bottom);
                    rr.radiusX = 3.0f;
                    rr.radiusY = 3.0f;
                    rt_->FillRoundedRectangle(rr, code_bg_brush);
                }
            }
        }

        // Apply per-range color overrides (e.g., link color)
        for (auto& cr : run.color_ranges) {
            auto* cr_brush = get_brush(cr.color);
            if (cr_brush) {
                DWRITE_TEXT_RANGE range = {cr.start, cr.length};
                run.layout->SetDrawingEffect(cr_brush, range);
            }
        }

        D2D1_POINT_2F origin = D2D1::Point2F(run.rect.left, run.rect.top + offset_y);

        rt_->DrawTextLayout(
            origin, run.layout.Get(), brush,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
}
