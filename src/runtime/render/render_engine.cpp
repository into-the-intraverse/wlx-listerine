#include "runtime/render/render_engine.h"

#include <algorithm>

namespace wlx::runtime::render {

using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;
using namespace wlx::runtime::search;
using namespace wlx::runtime::theme;

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

void RenderEngine::set_search_matches(const std::vector<SearchMatch>& matches, int current_index) {
    search_matches_ = matches;
    // Clamp current out-of-range to -1 so an invalid index doesn't silently
    // hide the "current" highlight — it becomes an obvious no-current instead.
    search_current_ = (current_index >= 0 && current_index < static_cast<int>(matches.size()))
                    ? current_index : -1;
}

HRESULT RenderEngine::create_device_resources(HWND hwnd) {
    if (rt_) return S_OK;

    RECT rc;
    GetClientRect(hwnd, &rc);
    width_ = static_cast<UINT>(rc.right - rc.left);
    height_ = static_cast<UINT>(rc.bottom - rc.top);

    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;

    D2D1_SIZE_U size = D2D1::SizeU(width_, height_);
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.dpiX = static_cast<float>(dpi);
    rtProps.dpiY = static_cast<float>(dpi);
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
    if (FAILED(rt_->CreateSolidColorBrush(ThemeService::to_d2d_color(color), brush.GetAddressOf())))
        return nullptr;  // don't cache a transient failure as a permanent null
    auto* ptr = brush.Get();
    brush_cache_[color] = std::move(brush);
    return ptr;
}

ID2D1SolidColorBrush* RenderEngine::get_brush(uint32_t color, float alpha) {
    uint32_t alpha_byte = static_cast<uint32_t>(alpha * 255.0f) & 0xFF;
    uint32_t key = (alpha_byte << 24) | (color & 0x00FFFFFF);

    auto it = brush_cache_.find(key);
    if (it != brush_cache_.end())
        return it->second.Get();

    if (!rt_) return nullptr;

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt_->CreateSolidColorBrush(ThemeService::to_d2d_color(color, alpha), brush.GetAddressOf())))
        return nullptr;  // don't cache a transient failure as a permanent null
    auto* ptr = brush.Get();
    brush_cache_[key] = std::move(brush);
    return ptr;
}

HRESULT RenderEngine::paint(LayoutDocument& layout, float scroll_y,
                             TextPosition sel_start, TextPosition sel_end,
                             const std::wstring* goto_input, int goto_total) {
    if (!rt_) return E_FAIL;

    const auto& colors = theme_.palette(dark_mode_);
    float viewport_h = dip_height();

    rt_->BeginDraw();
    rt_->Clear(ThemeService::to_d2d_color(colors.background));

    if (sel_start.valid() && sel_end.valid() && sel_end < sel_start)
        std::swap(sel_start, sel_end);

    // Apply scroll transform
    rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, -scroll_y));

    size_t search_cursor = 0;
    // Block TOPS are emitted in ascending Y (blocks stack non-overlapping, with
    // table rows sharing a Y span), so the visible window is a contiguous run.
    // Block BOTTOMS are non-decreasing too, EXCEPT blockquote border containers
    // (emitted before their children with a rect spanning the whole quote) —
    // the lower_bound seek below requires non-decreasing bottoms, so it can
    // land past a still-visible container (never past a text block: all
    // non-container bottoms ascend). The container pass below repaints those.
    // Binary-search the first block whose bottom edge reaches the viewport,
    // then stop at the first block past the bottom — O(visible + log N)
    // instead of O(N) per frame. paint_search_highlights re-seeds its own
    // cursor, so starting mid-vector is safe.
    auto first_visible = std::lower_bound(
        layout.blocks.begin(), layout.blocks.end(), scroll_y,
        [](const LayoutBlock& b, float sy) { return b.rect.bottom < sy; });
    const int first_idx = static_cast<int>(first_visible - layout.blocks.begin());

    // Blockquote border containers the seek landed past but whose rect still
    // crosses the viewport. They carry no text — only the left-border
    // decoration (plus the whole-rect selection fill an empty-text block gets
    // when a selection spans it). Containers at/after first_idx are painted by
    // the main loop; border_containers is tiny (one entry per blockquote).
    for (int idx : layout.border_containers) {
        if (idx >= first_idx) break;  // ascending indices
        auto& block = layout.blocks[idx];
        if (block.rect.bottom >= scroll_y && block.rect.top - scroll_y <= viewport_h) {
            paint_selection_highlight(block, idx, 0, sel_start, sel_end);
            paint_block_decoration(block, 0);
        }
    }

    for (int block_idx = first_idx;
         block_idx < static_cast<int>(layout.blocks.size()); block_idx++) {
        auto& block = layout.blocks[block_idx];
        // Visibility culling
        float block_top = block.rect.top - scroll_y;
        float block_bottom = block.rect.bottom - scroll_y;
        if (block_bottom < 0) continue;          // above viewport (defensive)
        if (block_top > viewport_h) break;       // past viewport — Y-sorted, done

        // Lazy layout: build this visible block's IDWriteTextLayout +
        // decorations on first paint (no-op for fully-eager documents).
        // WARNING: unlike md_materialize::materialize_viewport, this raw call
        // does NO height-delta reflow. Hosts with lazy MD layouts (estimated
        // block heights) MUST run materialize_viewport with this same scroll_y
        // before paint — this call is then a no-op safety net. The colorizer
        // skips that pre-pass legitimately: its materializer never changes
        // block heights (fixed line grid).
        if (layout.materialize_block)
            layout.materialize_block(block, block_idx);

        paint_block_background(block, 0);
        paint_trailing_ws(block, 0);
        paint_inline_code_bg(block, 0);
        paint_span_backgrounds(block, 0);
        paint_selection_highlight(block, block_idx, 0, sel_start, sel_end);
        paint_search_highlights(block, block_idx, 0, search_cursor);
        paint_block_decoration(block, 0);
        paint_indent_guides(block, 0);
        paint_bullet(block, 0);
        paint_text_runs(block, 0);
        paint_whitespace_markers(block, 0);
        paint_copy_button(block, block_idx, 0);
    }

    if (layout.gutter_width > 0.0f)
        paint_line_numbers(layout, scroll_y);

    rt_->SetTransform(D2D1::Matrix3x2F::Identity());

    if (goto_input)
        paint_goto_prompt(*goto_input, goto_total);

    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        discard_device_resources();
        needs_recreate_ = true;
    }
    return hr;
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

    // Selection offsets come from hit-testing run 0 (hit_test_position), and
    // every layout builder emits at most one run per block — run 0 IS the block.
    int text_len = static_cast<int>(run.text.size());

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

void RenderEngine::paint_search_highlights(const LayoutBlock& block, int block_index,
                                            float offset_y, size_t& match_cursor) {
    if (search_matches_.empty() || !rt_) return;

    // Skip matches belonging to earlier blocks; they've already been handled
    // (or skipped) by prior paint_search_highlights calls in this frame.
    while (match_cursor < search_matches_.size()
           && search_matches_[match_cursor].block_index < block_index) {
        ++match_cursor;
    }

    const auto& pal = theme_.palette(dark_mode_);

    for (size_t i = match_cursor; i < search_matches_.size(); i++) {
        const auto& m = search_matches_[i];
        if (m.block_index > block_index) break;

        int cursor = 0;
        for (const auto& run : block.text_runs) {
            const int run_len = static_cast<int>(run.text.size());
            const int run_start = cursor;
            const int run_end   = cursor + run_len;
            cursor = run_end;

            if (m.char_end <= run_start || m.char_start >= run_end) continue;
            if (!run.layout) continue;

            const int local_start = std::max(m.char_start, run_start) - run_start;
            const int local_end   = std::min(m.char_end,   run_end)   - run_start;
            const UINT32 length   = static_cast<UINT32>(local_end - local_start);

            UINT32 required = 0;
            run.layout->HitTestTextRange(
                static_cast<UINT32>(local_start), length,
                run.rect.left, run.rect.top + offset_y,
                nullptr, 0, &required);
            if (required == 0) continue;

            std::vector<DWRITE_HIT_TEST_METRICS> metrics(required);
            UINT32 actual = 0;
            run.layout->HitTestTextRange(
                static_cast<UINT32>(local_start), length,
                run.rect.left, run.rect.top + offset_y,
                metrics.data(), required, &actual);

            const bool is_current = (static_cast<int>(i) == search_current_);
            const uint32_t color = is_current ? pal.search_highlight_current
                                              : pal.search_highlight;
            const float alpha = is_current ? 0.60f : 0.30f;
            auto* brush = get_brush(color, alpha);
            if (!brush) continue;

            for (UINT32 j = 0; j < actual; j++) {
                const auto& mm = metrics[j];
                const D2D1_RECT_F r = { mm.left, mm.top,
                                        mm.left + mm.width, mm.top + mm.height };
                rt_->FillRectangle(r, brush);
            }
        }
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

void RenderEngine::paint_inline_code_bg(const LayoutBlock& block, float offset_y) {
    for (auto& run : block.text_runs) {
        if (run.code_bg_rects.empty()) continue;
        auto* code_bg_brush = get_brush(theme_.palette(dark_mode_).code_bg);
        if (!code_bg_brush) continue;
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

void RenderEngine::paint_text_runs(const LayoutBlock& block, float offset_y) {
    for (auto& run : block.text_runs) {
        if (!run.layout) continue;

        auto* brush = get_brush(run.color);
        if (!brush) continue;

        // Apply per-range foreground brushes: device-bound, so they must be
        // (re)applied each frame. Font modifiers (bold/italic/underline/
        // strikethrough) are NOT applied here — every layout builder sets them
        // at IDWriteTextLayout creation time, before height measurement, so
        // styled glyphs never rewrap or shift after the block was measured.
        for (auto& cr : run.color_ranges) {
            DWRITE_TEXT_RANGE range = {cr.start, cr.length};
            if (auto* cr_brush = get_brush(cr.color)) {
                run.layout->SetDrawingEffect(cr_brush, range);
            }
        }

        D2D1_POINT_2F origin = D2D1::Point2F(run.rect.left, run.rect.top + offset_y);

        rt_->DrawTextLayout(
            origin, run.layout.Get(), brush,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
}

void RenderEngine::paint_copy_button(const LayoutBlock& block, int block_index, float offset_y) {
    if (block.type != BlockType::CodeFence) return;

    const auto& colors = theme_.palette(dark_mode_);
    D2D1_RECT_F btn = copy_button_rect(block);
    btn.top += offset_y;
    btn.bottom += offset_y;
    float btn_size = btn.right - btn.left;
    float bx = btn.left;
    float by = btn.top;

    bool copied = (copied_code_block_ == block_index);
    bool hovered = (hovered_code_block_ == block_index);
    float icon_opacity = copied ? 1.0f : hovered ? 1.0f : 0.3f;

    // Button background
    uint32_t bg_color = copied ? colors.link : colors.code_bg;
    auto* bg_brush = get_brush(bg_color);
    if (bg_brush) {
        D2D1_ROUNDED_RECT rr = {btn, 4.0f, 4.0f};
        rt_->FillRoundedRectangle(rr, bg_brush);
    }

    // Icon
    auto* icon_brush = get_brush(colors.text);
    if (!icon_brush) return;

    // Apply opacity for non-hovered state
    ID2D1SolidColorBrush* draw_brush = icon_brush;
    if (icon_opacity < 1.0f) {
        if (auto* faded = get_brush(colors.text, icon_opacity))
            draw_brush = faded;
    }

    float cx = bx + btn_size * 0.5f;
    float cy = by + btn_size * 0.5f;

    if (copied) {
        // Checkmark
        auto* white_brush = get_brush(colors.background);
        if (white_brush) {
            D2D1_POINT_2F p1 = {cx - 5.0f, cy};
            D2D1_POINT_2F p2 = {cx - 1.5f, cy + 4.0f};
            D2D1_POINT_2F p3 = {cx + 5.0f, cy - 3.5f};
            rt_->DrawLine(p1, p2, white_brush, 1.5f);
            rt_->DrawLine(p2, p3, white_brush, 1.5f);
        }
    } else {
        // Copy icon — two overlapping rectangles
        float s = 5.0f;
        D2D1_RECT_F back = D2D1::RectF(cx - s + 1.5f, cy - s - 1.0f,
                                         cx + s + 1.5f, cy + s - 1.0f);
        D2D1_RECT_F front = D2D1::RectF(cx - s - 1.5f, cy - s + 1.0f,
                                          cx + s - 1.5f, cy + s + 1.0f);
        rt_->DrawRectangle(back, draw_brush, 1.0f);
        rt_->DrawRectangle(front, draw_brush, 1.0f);
    }
}

void RenderEngine::paint_trailing_ws(const LayoutBlock& block, float offset_y) {
    if (!block.has_trailing_ws) return;

    auto* brush = get_brush(block.trailing_ws_color, 0.25f);
    if (!brush) return;

    D2D1_RECT_F r = block.trailing_ws_rect;
    r.top += offset_y;
    r.bottom += offset_y;
    rt_->FillRectangle(&r, brush);
}

void RenderEngine::paint_span_backgrounds(const LayoutBlock& block, float offset_y) {
    for (auto& run : block.text_runs) {
        if (!run.layout) continue;
        for (auto& cr : run.color_ranges) {
            if (!cr.has_bg) continue;
            auto* brush = get_brush(cr.bg_color, 0.25f);
            if (!brush) continue;

            // Get rect from text layout for this range
            UINT32 actual_count = 0;
            run.layout->HitTestTextRange(cr.start, cr.length, 0, 0,
                                         nullptr, 0, &actual_count);
            if (actual_count == 0) continue;

            std::vector<DWRITE_HIT_TEST_METRICS> metrics(actual_count);
            run.layout->HitTestTextRange(cr.start, cr.length, 0, 0,
                                         metrics.data(), actual_count, &actual_count);

            for (UINT32 m = 0; m < actual_count; m++) {
                D2D1_RECT_F r = D2D1::RectF(
                    run.rect.left + metrics[m].left,
                    run.rect.top + offset_y + metrics[m].top,
                    run.rect.left + metrics[m].left + metrics[m].width,
                    run.rect.top + offset_y + metrics[m].top + metrics[m].height);
                rt_->FillRectangle(&r, brush);
            }
        }
    }
}

void RenderEngine::paint_whitespace_markers(const LayoutBlock& block, float offset_y) {
    if (block.ws_markers.empty() || block.text_runs.empty()) return;

    auto* brush = get_brush(block.ws_marker_color, 0.35f);
    if (!brush) return;

    auto& run = block.text_runs[0];
    float origin_x = run.rect.left;
    float origin_y = run.rect.top + offset_y;
    float font_size = theme_.fonts().code_size;

    for (auto& wm : block.ws_markers) {
        float cx = origin_x + wm.x;
        float cy = origin_y + wm.y + font_size * 0.5f;

        if (wm.is_tab) {
            float arrow_len = font_size * 0.5f;
            float arrow_h = font_size * 0.15f;
            float ax = cx + font_size * 0.1f;

            rt_->DrawLine(
                D2D1::Point2F(ax, cy),
                D2D1::Point2F(ax + arrow_len, cy),
                brush, 1.0f);
            rt_->DrawLine(
                D2D1::Point2F(ax + arrow_len, cy),
                D2D1::Point2F(ax + arrow_len - arrow_h, cy - arrow_h),
                brush, 1.0f);
            rt_->DrawLine(
                D2D1::Point2F(ax + arrow_len, cy),
                D2D1::Point2F(ax + arrow_len - arrow_h, cy + arrow_h),
                brush, 1.0f);
        } else {
            float r = 1.2f;
            float dx = cx + font_size * 0.35f;
            D2D1_ELLIPSE ellipse = {{dx, cy}, r, r};
            rt_->FillEllipse(ellipse, brush);
        }
    }
}

void RenderEngine::paint_indent_guides(const LayoutBlock& block, float offset_y) {
    if (block.indent_guides.empty()) return;

    auto* brush = get_brush(block.indent_guide_color, 0.2f);
    if (!brush) return;

    float top = block.rect.top + offset_y;
    float bottom = block.rect.bottom + offset_y;

    for (float gx : block.indent_guides) {
        rt_->DrawLine(
            D2D1::Point2F(gx, top),
            D2D1::Point2F(gx, bottom),
            brush, 1.0f);
    }
}

void RenderEngine::paint_line_numbers(const LayoutDocument& layout, float scroll_y) {
    if (!rt_ || layout.line_tops.empty()) return;

    if (!line_number_format_) {
        dwrite_factory_->CreateTextFormat(
            theme_.fonts().code_family.c_str(), nullptr,
            DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            theme_.fonts().code_size, L"", line_number_format_.GetAddressOf());
        if (line_number_format_) {
            line_number_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            line_number_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    if (!line_number_format_) return;

    auto* brush = get_brush(theme_.palette(dark_mode_).muted);
    if (!brush) return;

    float viewport_h = dip_height();
    float line_h = theme_.fonts().code_size * 1.6f;
    float right = layout.gutter_width - 8.0f;  // small right gap before the text column

    // line_tops is ascending — binary-search the first line whose bottom edge
    // reaches the viewport instead of scanning from line 0 every frame
    // (mirrors the block loop in paint()).
    auto first_visible = std::lower_bound(
        layout.line_tops.begin(), layout.line_tops.end(), scroll_y - line_h);
    for (size_t i = static_cast<size_t>(first_visible - layout.line_tops.begin());
         i < layout.line_tops.size(); ++i) {
        float y = layout.line_tops[i];
        if (y - scroll_y + line_h < 0.0f) continue;       // above viewport (defensive)
        if (y - scroll_y > viewport_h) break;             // below viewport (line_tops ascending)

        wchar_t buf[16];
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%zu", i + 1);
        D2D1_RECT_F r = D2D1::RectF(0.0f, y, right, y + line_h);
        rt_->DrawText(buf, static_cast<UINT32>(wcslen(buf)),
                      line_number_format_.Get(), r, brush);
    }
}

void RenderEngine::paint_goto_prompt(const std::wstring& input, int total) {
    if (!rt_) return;

    if (!prompt_format_) {
        dwrite_factory_->CreateTextFormat(
            theme_.fonts().body_family.c_str(), nullptr,
            DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            theme_.fonts().body_size, L"", prompt_format_.GetAddressOf());
        if (prompt_format_)
            prompt_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (!prompt_format_) return;

    const auto& colors = theme_.palette(dark_mode_);
    float box_w = 260.0f;
    float box_h = theme_.fonts().body_size * 2.2f;
    float vh = dip_height();
    float x0 = 12.0f;
    float y0 = vh - box_h - 12.0f;
    D2D1_RECT_F box = D2D1::RectF(x0, y0, x0 + box_w, y0 + box_h);

    if (auto* bg = get_brush(colors.code_bg)) rt_->FillRectangle(box, bg);
    if (auto* border = get_brush(colors.muted)) rt_->DrawRectangle(box, border, 1.0f);

    wchar_t head[64];
    _snwprintf_s(head, _countof(head), _TRUNCATE, L"Go to line (1-%d): ", total);
    std::wstring text = std::wstring(head) + input + L"▏";  // trailing caret bar

    if (auto* fg = get_brush(colors.text)) {
        D2D1_RECT_F tr = D2D1::RectF(x0 + 8.0f, y0, x0 + box_w - 8.0f, y0 + box_h);
        rt_->DrawText(text.c_str(), static_cast<UINT32>(text.size()),
                      prompt_format_.Get(), tr, fg);
    }
}

}  // namespace wlx::runtime::render
