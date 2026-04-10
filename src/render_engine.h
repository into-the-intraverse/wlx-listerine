#pragma once

#include "layout_engine.h"
#include "theme_service.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <unordered_map>

using Microsoft::WRL::ComPtr;

class RenderEngine {
public:
    RenderEngine(ID2D1Factory* d2d_factory, IDWriteFactory* dwrite_factory,
                 const ThemeService& theme, bool dark_mode);

    HRESULT create_device_resources(HWND hwnd);
    HRESULT create_bitmap_resources(IWICImagingFactory* wic_factory, int width, int height);
    HRESULT save_to_png(IWICImagingFactory* wic_factory, const wchar_t* path);
    void discard_device_resources();
    void resize(UINT width, UINT height);

    void paint(const LayoutDocument& layout, float scroll_y);

    void set_dark_mode(bool dark);
    void set_hovered_span(int index) { hovered_span_ = index; }

    bool needs_recreate() const { return needs_recreate_; }
    UINT width() const { return width_; }
    UINT height() const { return height_; }

    // DIP dimensions (account for DPI scaling)
    float dip_width() const;
    float dip_height() const;
    // Convert pixel coordinate to DIP coordinate
    float pixel_to_dip_x(float px) const;
    float pixel_to_dip_y(float py) const;

private:
    ID2D1SolidColorBrush* get_brush(uint32_t color);
    void paint_block_background(const LayoutBlock& block, float offset_y);
    void paint_block_decoration(const LayoutBlock& block, float offset_y);
    void paint_bullet(const LayoutBlock& block, float offset_y);
    void paint_text_runs(const LayoutBlock& block, float offset_y);

    ID2D1Factory* d2d_factory_;
    IDWriteFactory* dwrite_factory_;
    const ThemeService& theme_;
    bool dark_mode_;

    ComPtr<ID2D1RenderTarget> rt_;
    ComPtr<IWICBitmap> wic_bitmap_;
    bool is_hwnd_target_ = false;
    std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>> brush_cache_;

    // Cached text format for bullet rendering
    ComPtr<IDWriteTextFormat> bullet_format_;

    int hovered_span_ = -1;
    bool needs_recreate_ = false;
    UINT width_ = 0;
    UINT height_ = 0;
};
