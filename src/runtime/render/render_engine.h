#pragma once

#include "runtime/layout/layout_block.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/text_position.h"
#include "runtime/search/search_match.h"
#include "runtime/theme/theme_service.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace wlx::runtime::render {

using Microsoft::WRL::ComPtr;

class RenderEngine {
public:
    RenderEngine(ID2D1Factory* d2d_factory, IDWriteFactory* dwrite_factory,
                 const theme::ThemeService& theme, bool dark_mode);

    HRESULT create_device_resources(HWND hwnd);
    HRESULT create_bitmap_resources(IWICImagingFactory* wic_factory, int width, int height);
    HRESULT save_to_png(IWICImagingFactory* wic_factory, const wchar_t* path);
    void discard_device_resources();
    void resize(UINT width, UINT height);

    void paint(const layout::LayoutDocument& layout, float scroll_y,
               layout::TextPosition sel_start = {}, layout::TextPosition sel_end = {},
               const std::wstring* goto_input = nullptr, int goto_total = 0);

    void set_dark_mode(bool dark);
    void set_hovered_span(int index) { hovered_span_ = index; }
    void set_hovered_code_block(int index) { hovered_code_block_ = index; }
    void set_copied_code_block(int index) { copied_code_block_ = index; }
    void set_search_matches(const std::vector<search::SearchMatch>& matches, int current_index);

    bool needs_recreate() const { return needs_recreate_; }
    UINT width() const { return width_; }
    UINT height() const { return height_; }

    // DIP dimensions (account for DPI scaling)
    float dip_width() const;
    float dip_height() const;
    // Convert pixel coordinate to DIP coordinate
    float pixel_to_dip_x(float px) const;
    float pixel_to_dip_y(float py) const;

    // Underlying D2D target. Used by tools that need to composite an overlay
    // (e.g. screenshot_tool painting the search HUD on top of the document).
    ID2D1RenderTarget* render_target() const { return rt_.Get(); }

private:
    ID2D1SolidColorBrush* get_brush(uint32_t color);
    ID2D1SolidColorBrush* get_brush(uint32_t color, float alpha);
    void paint_block_background(const layout::LayoutBlock& block, float offset_y);
    void paint_inline_code_bg(const layout::LayoutBlock& block, float offset_y);
    void paint_block_decoration(const layout::LayoutBlock& block, float offset_y);
    void paint_bullet(const layout::LayoutBlock& block, float offset_y);
    void paint_line_numbers(const layout::LayoutDocument& layout, float scroll_y);
    void paint_goto_prompt(const std::wstring& input, int total);
    void paint_text_runs(const layout::LayoutBlock& block, float offset_y);
    void paint_selection_highlight(const layout::LayoutBlock& block, int block_index,
                                   float offset_y, layout::TextPosition sel_start, layout::TextPosition sel_end);
    void paint_search_highlights(const layout::LayoutBlock& block, int block_index,
                                 float offset_y, size_t& match_cursor);
    void paint_copy_button(const layout::LayoutBlock& block, int block_index, float offset_y);
    void paint_whitespace_markers(const layout::LayoutBlock& block, float offset_y);
    void paint_indent_guides(const layout::LayoutBlock& block, float offset_y);
    void paint_trailing_ws(const layout::LayoutBlock& block, float offset_y);
    void paint_span_backgrounds(const layout::LayoutBlock& block, float offset_y);

    ID2D1Factory* d2d_factory_;
    IDWriteFactory* dwrite_factory_;
    const theme::ThemeService& theme_;
    bool dark_mode_;

    ComPtr<ID2D1RenderTarget> rt_;
    ComPtr<IWICBitmap> wic_bitmap_;
    bool is_hwnd_target_ = false;
    std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>> brush_cache_;

    // Cached text format for bullet rendering
    ComPtr<IDWriteTextFormat> bullet_format_;
    ComPtr<IDWriteTextFormat> line_number_format_;
    ComPtr<IDWriteTextFormat> prompt_format_;

    int hovered_span_ = -1;
    int hovered_code_block_ = -1;
    int copied_code_block_ = -1;
    std::vector<search::SearchMatch> search_matches_;
    int search_current_ = -1;
    bool needs_recreate_ = false;
    UINT width_ = 0;
    UINT height_ = 0;
};

}  // namespace wlx::runtime::render
