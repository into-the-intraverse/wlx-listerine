# RenderEngine DPI awareness — design

**Date:** 2026-04-28
**Status:** Draft
**Source TODO:** README.md "DPI scaling for the document render target"

## Problem

`RenderEngine::create_device_resources` (`src/render_engine.cpp:31-51`) creates the D2D `HwndRenderTarget` with `D2D1::RenderTargetProperties()` and no explicit DPI. D2D defaults to 96 DPI. On Per-Monitor V2 displays at >100% scaling (125%, 150%, 200%) the document text and chrome render at physical-pixel size, not their intended physical-inch size, and look conspicuously small next to the rest of TC's UI.

`SearchHud::recreate_target` (`src/search_hud.cpp:81-102`) already plumbs `GetDpiForWindow(hwnd)` through and renders at the correct physical size. The HUD and the document currently look right next to each other only at 96 DPI; at every other scale the HUD is visibly larger than the document body.

The downstream pipeline is already DPI-ready:

- `host_adapter.cpp` and `colorizer/colorizer_host_adapter.cpp` route mouse coordinates through `RenderEngine::pixel_to_dip_x/y` before feeding them into hit-test code (verified by grep).
- Viewport math uses `dip_width()` / `dip_height()`.
- `LayoutEngine` consumes the width it's handed and is stateless with respect to DPI.

These helpers are dormant DPI conversions today — they reduce to identity at 96 DPI. Activating the render target's DPI is what flips them on.

## Goal

Make the document render target DPI-aware on initial creation **and** on runtime DPI changes (window dragged across monitors with different scale factors), without disturbing the visual regression suite.

## Non-goals

- Changing `create_bitmap_resources` (the WIC path used by `screenshot_tool`). Visual regression goldens are pinned at 96 DPI.
- Honoring the suggested rect in `WM_DPICHANGED`'s `lParam`. We are a `WS_CHILD` of TC's lister; TC owns our window size via `WM_SIZE`.
- Multi-monitor handling beyond `WM_DPICHANGED`.
- Layout-engine changes. Layout already recomputes against whatever width it's given; the width supplier (`dip_width()`) becomes DPI-correct as a side effect of this fix.

## Design

### Change 1 — `RenderEngine::create_device_resources(HWND)`

Set the render target's DPI from the window's effective DPI before creating the target. Mirror SearchHud's pattern verbatim:

```cpp
UINT dpi = GetDpiForWindow(hwnd);
if (dpi == 0) dpi = 96;
D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
rtProps.dpiX = static_cast<float>(dpi);
rtProps.dpiY = static_cast<float>(dpi);
```

`width_` / `height_` continue to store physical pixels — matches Win32 message coordinates and `Resize(SizeU)` semantics. `dip_width()`, `dip_height()`, `pixel_to_dip_x/y` need no changes; their math against `rt_->GetSize()` was already correct, just gated behind a 96-DPI render target.

`create_bitmap_resources` is untouched. `discard_device_resources()` is the existing escape hatch for "throw the target away so the next paint recreates it"; no signature changes.

### Change 2 — `WM_DPICHANGED` handler in both host adapters

Near-identical body in `src/host_adapter.cpp` and `src/colorizer/colorizer_host_adapter.cpp`. The only difference is the relayout helper name (`do_layout(vs)` in markdown, `relayout(vs)` in colorizer — both one-arg, both already invoked from the local `WM_SIZE` case).

```cpp
case WM_DPICHANGED: {
    if (vs && vs->renderer) {
        vs->renderer->discard_device_resources();
        vs->renderer->create_device_resources(hwnd);  // picks up new DPI
        do_layout(vs);                                 // colorizer: relayout(vs)
        if (vs->hud) vs->hud->on_parent_resize();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
}
```

Why each line:

- `discard_device_resources()` + `create_device_resources(hwnd)` — `discard` does **not** set `needs_recreate_` (the flag is only set after a `D2DERR_RECREATE_TARGET` from `EndDraw`), so the next WM_PAINT will not auto-recreate. We recreate inline so `dip_width()` returns the new DIP width before relayout runs.
- `do_layout(vs)` / `relayout(vs)` — must run *after* the new render target exists. `dip_width()` reads `rt_->GetSize()` when `rt_` is non-null and falls back to `width_` (physical pixels) otherwise; running layout against the fallback would lay out at the old DIP scale.
- `vs->hud->on_parent_resize()` — enough for the HUD. `recreate_target` and `reposition_to_parent` both read `GetDpiForWindow` fresh on every call.
- `InvalidateRect` — schedule the paint.

We ignore `lParam`'s suggested rect: TC owns our `WS_CHILD` size via `WM_SIZE`. We accept the near-duplication between the two adapters; it's the same shape as the existing search-HUD wiring, and a shared helper would have to thread the relayout function pointer through, defeating the simplicity.

### Data flow

Initial open at 150% DPI:

1. `ListLoadW` calls `RenderEngine::create_device_resources(hwnd)`.
2. `GetDpiForWindow(hwnd)` returns 144.
3. D2D target created with `dpiX = dpiY = 144`.
4. `dip_width()` returns `width_ * 96/144` (DIPs).
5. `LayoutEngine` lays out at the smaller DIP width — text wrapping matches what Chrome does at 150%.
6. `RenderEngine::paint` draws into DIP space; D2D scales to 144-DPI physical pixels.
7. Mouse input arrives in physical pixels (Win32 contract); `pixel_to_dip_x/y` converts via `GetSize().width / width_ = 96/144`. Hit-tests land on the DIP-space rects correctly.

Runtime DPI change (drag from 150% monitor to 100% monitor):

1. Win32 sends `WM_DPICHANGED` with the new DPI in `wParam`.
2. Handler discards render target, immediately recreates it at the new DPI, relayouts against the new `dip_width()`, nudges HUD, invalidates.
3. Next paint draws the freshly-laid-out document into the new-DPI target.
4. HUD recreates its own target at 96 on the next `update()` or via the `on_parent_resize()` we just called.

## Error handling

- `GetDpiForWindow(hwnd)` returns 0 if the API is unavailable or `hwnd` is invalid. Fall back to 96, identical to SearchHud's defensive default. The fix becomes a no-op (current behavior); no regression.
- TC running in System-DPI-aware mode (not Per-Monitor V2): `GetDpiForWindow` returns the system DPI; `WM_DPICHANGED` never fires. Initial DPI is still picked up correctly; runtime drags are silently ignored. Acceptable degraded mode.
- D2D `RECREATE_TARGET` already triggers `discard_device_resources` + `needs_recreate_` from the existing paint loop. Nothing new here.

## Testing

### Automated

- Visual regression suite (27 + 1 cases) is unchanged. Uses `create_bitmap_resources`, deliberately pinned at 96 DPI.
- No unit test added for `create_device_resources`. The function needs a real HWND for `GetDpiForWindow`, and the only headlessly-assertable property would be "we passed a non-96 float into D2D" — that doesn't prove the document renders at the right physical size. The pattern that worked for SearchHud (extracting `hit_test_button` as a pure DPI-aware function) doesn't apply: RenderEngine has no analogous pure helper to factor out.

### Manual verification checklist

1. Display scaling at 150%. Open a markdown file in TC. Document text and code blocks render at the same physical size as Chrome at 150%, not half-size.
2. Same at 200%.
3. Drag the TC window between monitors with different scale factors. Document re-renders correctly within one paint cycle of `WM_DPICHANGED`.
4. At 150%, click a link and click a code-block copy button. Hit-tests must land on the visible glyphs. (This is the canary for any code path that bypasses `pixel_to_dip_x/y` and feeds physical pixels into a DIP-space API.)
5. Repeat 1–4 on a `.cpp` file in the colorizer plugin.

## Risks

- **Missed code path feeding physical pixels into a DIP-space API.** Caught by manual checklist item 4. The grep I ran during brainstorming covered both adapters' `WM_*` handlers and showed all mouse paths route through `pixel_to_dip_x/y`; risk feels low.
- **TC not Per-Monitor V2 aware.** Fix degrades to "initial DPI honored, runtime change ignored" — same as today's runtime-change behavior. No regression.
- **`vs->layout.reset()` cost on monitor drag.** Markdown documents are small; relayout is sub-100ms in the worst case. The layout cache would invalidate on viewport-bucket change anyway.

## Files touched

- `src/render_engine.cpp` — DPI in `create_device_resources` (one block, ~6 lines)
- `src/host_adapter.cpp` — `WM_DPICHANGED` case (~10 lines)
- `src/colorizer/colorizer_host_adapter.cpp` — same `WM_DPICHANGED` case (~10 lines)
- `README.md` — drop the now-resolved TODO bullet

No header changes. No new files. No CMake changes.
