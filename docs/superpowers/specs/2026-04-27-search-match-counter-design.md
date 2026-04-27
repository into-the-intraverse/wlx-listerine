# Search Match Counter HUD

## Problem

The markdown plugin supports incremental search via Total Commander's `ListSearchTextW` callback (see `src/host_adapter.cpp:1014`–`1045`). Matches get yellow highlight bands; the current match is rendered with a stronger color via `RenderEngine::set_search_matches(matches, current_index)`. But the user has no way to see *how many* matches the query produced, or which numbered match the cursor is on. With non-trivial documents and ambiguous queries (e.g. searching for `the`), this leaves the user advancing through matches blindly.

The README has tracked this as a TODO since `397a6bc`:

> **Search match counter** — show a `current / total` indicator in the bottom-right corner while a search is active.

A second user-stated requirement extends scope: the indicator should sit alongside clickable **prev / next** buttons so the user can step through matches without keyboard, and the same surface should host a future cursor-position display (Ln/Col).

## Goals

1. Show a `current / total` counter in the bottom-right of the WLX client area whenever a search has been issued, including the zero-result case (`0 / 0`).
2. Provide clickable `↑` / `↓` buttons next to the counter that advance to the previous/next match using the existing `search_step` code path.
3. Architect the surface as a reusable status bar so a future cursor-position widget (Ln/Col) can be added without redesigning the host.
4. Keep the keyboard search experience unchanged: TC's own find bar continues to drive `ListSearchTextW`; the new HUD is a passive read-out + optional mouse navigation aid.
5. No regression to the existing visual regression suite; one new visual case covers the HUD itself.

## Non-goals

- **Cursor position (Ln/Col) display.** Out of scope for this spec. Architected for, not built. Reasons: (a) the plugin has selection but no caret, so "cursor position" needs a separate definition (visual lines vs. source lines, code-block source vs. wrapped paragraph lines); (b) bundling would tie the HUD's lifetime to the harder feature.
- **Configurable HUD position.** Bottom-right only. The TODO specifies it; widget order inside is fixed.
- **Auto-hide / fade timers.** Lifetime is sticky — bar appears with the first search and stays until matches are cleared (Esc, file reload, or window close). Mirrors the highlight-band lifetime.
- **Keyboard-driven prev/next from the HUD.** The HUD does not take focus; arrow buttons are mouse-only. Keyboard prev/next remains TC's responsibility (F3 / Shift+F3 driven through `ListSearchTextW`).
- **Configurable colors / fonts for the HUD.** Reuses the existing theme palette (`code_bg`, `text`) and body font. No new TOML keys.
- **Counter formatting beyond `N / M`.** No thousands separators, no `M+` cap. Markdown documents that produce thousands of matches of a single term are rare enough that the bar widening to fit is acceptable.
- **Visibility while TC's find bar is open vs. closed.** We have no reliable signal for TC's bar opening/closing; lifetime is governed by match state, not bar state.

## Approach summary

The HUD is split into two layers so the same paint code can be exercised from two render contexts (the live HWND and the offscreen `screenshot_tool` bitmap):

- **`SearchHudPainter`** — pure D2D paint logic. Given any `ID2D1RenderTarget`, theme palette, font, current state (counter text, hover index, disabled flag), and an anchor rect, it lays out and draws the bar. No HWND, no `WS_CHILD`, no message handling.
- **`SearchHud`** — runtime wrapper. Owns a `WS_CHILD` HWND positioned at the bottom-right of the WLX client area, owns the `ID2D1HwndRenderTarget`, handles `WM_PAINT` / mouse messages, and delegates the actual drawing to `SearchHudPainter`. Exposes update/clear/`on_navigate`. Owned by `ViewState` alongside `RenderEngine`.

`screenshot_tool` does not host a real WLX window; it renders directly to a WIC bitmap (`render_engine.cpp` / `screenshot_main.cpp`). After painting the document it instantiates a `SearchHudPainter` and paints the HUD onto the same bitmap when the new `--search` flag is set. Unit tests exercise `SearchHudPainter` only when needed; the formatter test exercises `format_counter` directly.

**Why a child HWND for runtime** (over renderer-painted overlay, the alternative considered):
- Hover state on prev/next would otherwise force a full document repaint per mouse move — wasteful.
- Win32 routes clicks naturally; no need to extend `InteractionEngine`'s hit-testing model with chrome elements.
- Independent invalidation surface: the HUD repaints on hover/state changes without touching the document render target.
- Stable home for the future cursor-position widget — adding a left-anchored slot to an existing bar is much smaller than retrofitting a second floating element later.
- Cost (a second D2D target, parent-resize plumbing, focus discipline) is bounded and matches the project's existing Win32 idioms.

**Why split painter from HWND wrapper** (over a single class):
- Without the split, the visual regression case would need to host a real WLX window and capture it via `PrintWindow(PW_RENDERFULLCONTENT)`, doubling the screenshot tool's complexity.
- The split lets `screenshot_tool` keep its current bitmap render path and just composite the HUD on top.
- Painter is trivially unit-testable from a memory-DC bitmap if a future regression motivates it.

**Why a separate pure formatter** (`format_counter`): the format-string contract is the only part with logic worth unit-testing in isolation. Everything else is integration that the new visual regression case exercises end-to-end.

## Architecture

### New / modified files

```
src/search_counter_format.h               NEW: pure formatter format_counter(current_one_based, total)
                                               -> wstring; no Win32 deps so doctest includes cleanly.
src/search_hud_painter.h                  NEW: SearchHudPainter class declaration; pure D2D paint,
                                               no HWND. Used by both SearchHud and screenshot_tool.
src/search_hud_painter.cpp                NEW: SearchHudPainter implementation; pill + chevron paint,
                                               hit-rect computation, theme color lookups.
src/search_hud.h                          NEW: SearchHud class declaration (runtime HWND wrapper).
src/search_hud.cpp                        NEW: SearchHud implementation; window class registration in
                                               DllMain; WndProc for WM_PAINT / WM_LBUTTONDOWN /
                                               WM_MOUSEMOVE / WM_MOUSELEAVE; delegates paint to
                                               SearchHudPainter; tracks hover/disabled state.

src/host_adapter.cpp                      CHG: ViewState gains std::unique_ptr<SearchHud> hud;
                                               create in ListLoadW, destroy in ListCloseWindow,
                                               call update/clear/on_parent_resize/set_dark_mode at
                                               the integration sites.
src/screenshot_main.cpp                   CHG: parse --search <term> and --search-step <N> flags;
                                               after document paint, instantiate SearchHudPainter
                                               and paint HUD overlay onto the same WIC bitmap target
                                               at the bottom-right anchor; HUD state is computed by
                                               running the existing search code path against the
                                               built layout (no WLX export call).
CMakeLists.txt                            CHG: append search_hud_painter.cpp to the wlx-core
                                               STATIC lib (so screenshot_tool / tests pick it up via
                                               the existing link line); append search_hud.cpp to the
                                               wlx-listerine-md SHARED target (runtime HWND wrapper,
                                               not needed by screenshot_tool); append the new test
                                               source to the existing `tests` add_executable list.

tests/test_search_counter_format.cpp      NEW: doctest cases for format_counter.

test_data/cases/28_search_counter.md      NEW: ~30-line markdown input with several occurrences of
                                               a high-frequency term across multiple block types.
test_data/cases/28_search_counter.flags   NEW: one-line sidecar listing extra screenshot_tool args
                                               (e.g. `--search the --search-step 2`). Picked up by
                                               visual-test.sh and update-goldens.ts.
test_data/cases/28_search_counter_golden.png NEW: self-snapshot reference. Generated by
                                                   `bun run update-goldens -- 28_search_counter`,
                                                   eyeball-verified, committed.

scripts/visual-test.sh                    CHG: when a case has a `.flags` sidecar, append its
                                               contents to the screenshot_tool invocation.
test_data/compare.py                      CHG: prefer `<case>_golden.png` over `<case>_chrome.png`
                                               when present; threshold ≥99.5% for golden cases,
                                               existing 95% threshold for Chrome cases.
scripts/update-goldens.ts                 CHG: when a case has a `.flags` sidecar, skip the
                                               Playwright path and instead invoke screenshot_tool
                                               with those flags + `--full`, writing the result to
                                               `<case>_golden.png` (not `<case>_chrome.png`).
```

### `SearchHudPainter` public surface

```cpp
struct SearchHudState {
    int  current_one_based = 0;
    int  total             = 0;
    int  hovered_button    = -1;   // -1 none, 0 prev, 1 next
};

struct SearchHudHitRects {
    D2D1_RECT_F counter;
    D2D1_RECT_F prev;
    D2D1_RECT_F next;
    D2D1_RECT_F bounds;            // full bar rect (for the HWND wrapper to size itself)
};

class SearchHudPainter {
public:
    SearchHudPainter(IDWriteFactory* dwrite, const ThemeService& theme);

    // Lays out the bar; returns the rects the HWND wrapper / hit-tester needs.
    SearchHudHitRects layout(const SearchHudState& s, D2D1_POINT_2F bottom_right_anchor);

    // Paints the bar at the rects produced by `layout`. Render target can be
    // either an HwndRenderTarget or a WicBitmapRenderTarget.
    void paint(ID2D1RenderTarget* rt, const SearchHudState& s,
               const SearchHudHitRects& rects, bool dark_mode);
};
```

No HWND, no D2D factory, no message handling. Layout and paint are pure functions of state + theme + render target.

### `SearchHud` public surface

```cpp
class SearchHud {
public:
    SearchHud(HWND parent,
              ID2D1Factory* d2d_factory,
              IDWriteFactory* dwrite_factory,
              const ThemeService& theme,
              bool dark_mode);
    ~SearchHud();

    void update(int current_one_based, int total);   // shows bar, repaints
    void clear();                                    // hides bar
    void on_parent_resize();                         // repositions bottom-right
    void set_dark_mode(bool dark);

    std::function<void(bool backwards)> on_navigate; // called by ↑ / ↓ click
};
```

The class knows nothing about `ViewState`, `SearchIndex`, or `search_step`. Wiring back to the host happens through the `on_navigate` callback set at construction. Internally it owns a `SearchHudPainter`, an `ID2D1HwndRenderTarget`, the cached `SearchHudHitRects`, and the current `SearchHudState`.

### `format_counter`

```cpp
// search_counter_format.h
#pragma once
#include <string>

inline std::wstring format_counter(int current_one_based, int total) {
    return std::to_wstring(current_one_based) + L" / " + std::to_wstring(total);
}
```

Pure, no Win32, no DWrite, no theme. Used by `SearchHud` and tested directly.

## Data flow & integration points

`host_adapter.cpp` is the single integration site. Concretely:

| Site | Existing behavior | New call |
|---|---|---|
| `ListLoadW` (after main HWND created, after `ViewState::renderer` constructed) | constructs `RenderEngine` | also constructs `vs->hud`; sets `vs->hud->on_navigate = ...` |
| `ListCloseWindow` | destroys `ViewState` | `vs->hud.reset()` runs before parent HWND destruction |
| `ListSearchTextW` success path (~line 1041–1042) | scrolls + sets renderer matches | also `vs->hud->update(r.cursor + 1, static_cast<int>(r.matches.size()))` |
| `ListSearchTextW` no-match path (~line 1037) | clears renderer matches, returns `LISTPLUGIN_ERROR` | also `vs->hud->update(0, 0)` (sticky `0 / 0`) |
| Esc handler (~line 670) | clears `vs->matches`, clears renderer matches | also `vs->hud->clear()` |
| `ListLoadNextW` (file reload) | resets parsed/layout state | also `vs->hud->clear()` |
| Parent `WM_SIZE` | resizes `RenderEngine` | also `vs->hud->on_parent_resize()` |
| Theme / dark-mode change | repaints | also `vs->hud->set_dark_mode(dark)` |

### Click → navigation callback

```cpp
vs->hud->on_navigate = [vs](bool backwards) {
    if (vs->matches.empty() || !vs->layout) return;   // disabled-state safety
    SearchQuery q = vs->last_query;
    q.backwards = backwards;
    auto r = search_step(*vs, q, /*findfirst=*/false);
    if (r.has_match) {
        scroll_to_match(vs, r.matches[r.cursor]);
        if (vs->renderer) vs->renderer->set_search_matches(r.matches, r.cursor);
        vs->hud->update(r.cursor + 1, static_cast<int>(r.matches.size()));
        InvalidateRect(vs->hwnd, nullptr, FALSE);
    }
};
```

This re-uses the existing `search_step` template path (no parallel logic). `vs->last_query` is already maintained by `search_step`.

### Disabled state

When `total == 0`, prev/next are visually disabled (0.3 alpha, no hover effect) and the HUD's own click handler returns before invoking `on_navigate`. The callback also has an `if (vs->matches.empty()) return;` early-out as belt-and-suspenders.

When `total == 1`, prev/next remain enabled and clickable; clicking cycles back to the same single match (no visual change, but the cursor advancement code path runs unchanged). Disabling them in the single-match case would require a separate UI state with no real benefit.

## Internal layout & rendering

### Bar layout (left-to-right)

```
+------------------------------------------------+
| pad | counter pill | gap | ↑ btn | gap | ↓ btn | pad |
+------------------------------------------------+
```

- **Counter pill** — rounded rect, `colors.code_bg` background at ~85% alpha, `colors.text` foreground, ~13px Segoe UI. Width = measured text width + 12px horizontal padding. Height ~20px.
- **Buttons** — 24×24 square (mirroring `paint_copy_button` in `render_engine.cpp:479`), 4px corner radius, transparent background normally, `colors.code_bg` on hover, icon stroked in `colors.text`. Disabled state: 0.3 alpha, no hover effect, click ignored.
- **Icons** — chevron-up `↑` (two strokes meeting at top) and chevron-down `↓` (two strokes meeting at bottom). Drawn with `DrawLine` like the copy-button checkmark — no font glyph dependency.
- **Sizing** — bar dimensions recomputed on each `update(...)` call (digit count drives width). `SetWindowPos` resizes + moves the HUD HWND in one call.
- **Position** — bottom-right of parent client area, 12px margin both axes.

### Win32 wiring

- Window class `WlxListerineSearchHud`, registered once in `DllMain` alongside the existing `WlxListerineMdView` class.
- Style: `WS_CHILD | WS_CLIPSIBLINGS`. Not visible until first `update()` call (`ShowWindow(SW_SHOWNOACTIVATE)`).
- Extended style: `WS_EX_NOACTIVATE` (insurance against focus-steal even with `MA_NOACTIVATE` not handled).
- Messages handled: `WM_PAINT`, `WM_LBUTTONDOWN`, `WM_MOUSEMOVE` + `TrackMouseEvent` for `WM_MOUSELEAVE`, `WM_DESTROY`. Everything else `DefWindowProc`.
- `WM_MOUSEACTIVATE` returns `MA_NOACTIVATE` so clicks never move keyboard focus away from TC's find bar.

### Hit testing

After each layout pass, the HUD caches three rects in client coordinates: `counter_rect_`, `prev_rect_`, `next_rect_`.

- `WM_MOUSEMOVE` walks the button rects; `hovered_button_` (-1 / 0 / 1) updates and `InvalidateRect(self, nullptr, FALSE)` runs only if changed.
- `WM_LBUTTONDOWN` resolves to the same rects; if a button is hit and not disabled, it calls `on_navigate(button == 0 /*prev*/)`. The HUD does not call `SetCapture` — clicks are point events, drag is meaningless here.

### D2D / DPI

- Owns its `ID2D1HwndRenderTarget` created from the global `d2d_factory_`. Standard `D2DERR_RECREATE_TARGET` recreate-on-paint pattern, mirroring `RenderEngine`.
- DPI: child inherits parent's Per-Monitor V2 awareness. Render target uses `GetDpiForWindow(hwnd)`-derived DPI, same convention as `RenderEngine::create_device_resources`.

### Theme / dark-mode reload

- `set_dark_mode(bool)` toggles a flag and invalidates. Theme palette is held by reference, so palette lookups are always current.
- Full theme/font reload (when `theme_service.cpp` rereads the TOML): host adapter destroys + recreates `vs->hud` in the same path that recreates `RenderEngine`. Simpler than per-resource invalidation.

### DLL teardown discipline

Per the project's `feedback_dll_detach` rule (never `Release()` COM in `DLL_PROCESS_DETACH`): per-`ViewState` HUDs are destroyed in `ListCloseWindow`, which runs *before* detach. The window class is unregistered (or leaked, consistent with the existing pattern) in detach. No COM teardown in detach.

## Tests

### Unit — `tests/test_search_counter_format.cpp`

```cpp
TEST_CASE("format_counter") {
    SUBCASE("normal match")        CHECK(format_counter(1, 27)     == L"1 / 27");
    SUBCASE("last match")          CHECK(format_counter(27, 27)    == L"27 / 27");
    SUBCASE("zero results")        CHECK(format_counter(0, 0)      == L"0 / 0");
    SUBCASE("large counts plain")  CHECK(format_counter(142, 1058) == L"142 / 1058");
}
```

That's the entire formatter contract. Guards the format string against accidental edits (e.g. someone "improving" it with thousands separators or different spacing).

### Visual regression — flat-file layout, consistent with cases 01–27

The existing test infrastructure stores cases as flat files in `test_data/cases/`:
- `<case>.md` — input markdown (driven by `screenshot_tool`).
- `<case>.png` — fresh output from the latest `screenshot_tool` run.
- `<case>_chrome.png` — Playwright/Chrome reference. `compare.py` checks ≥95% similarity.

Case 28 is the first that needs both (a) extra args passed to `screenshot_tool` and (b) a self-snapshot reference instead of a Chrome reference. Three small additions:

- **`28_search_counter.md`** — ~30 lines of markdown with several deliberate occurrences of a high-frequency term (e.g. `the`), spread across heading / paragraph / code-block contexts so highlights render in multiple block types.
- **`28_search_counter.flags`** — one-line sidecar listing extra args, e.g. `--search the --search-step 2`. Picked up by both `visual-test.sh` and `update-goldens.ts`. Cases without a `.flags` file run as today.
- **`28_search_counter_golden.png`** — self-snapshot reference. Generated by `bun run update-goldens -- 28_search_counter` (which detects the `.flags` file and routes to `screenshot_tool` instead of Playwright), eyeball-verified, committed.

### `screenshot_tool` changes (`src/screenshot_main.cpp`)

- New CLI flags:
  - `--search <term>` — after parsing + layout, runs the existing `SearchIndex::build` + `SearchIndex::find_all` against the layout (same code path used by `search_step`) with default flags (case-insensitive, partial-word, forwards). No `ListSearchTextW` call — `screenshot_tool` doesn't go through the WLX export path.
  - `--search-step <N>` (default 0) — pre-advances the match cursor by `N`.
- After document paint, when `--search` is set, instantiates `SearchHudPainter`, calls `layout(...)` with the bottom-right anchor of the bitmap, then `paint(rt, ...)` against the same `IWICBitmap`-backed render target the document was drawn into. The HUD overlay composites cleanly because both the document and the HUD use the same target.
- Highlight rendering for matches inside the document body is already handled by `RenderEngine::set_search_matches(...)` — `screenshot_tool` calls that with the matches/cursor before painting.
- No HWND, no `PrintWindow`, no Win32 message loop changes.

### `scripts/visual-test.sh` changes

- After resolving the case's `.md` path, also probe for a `<case>.flags` file. If present, append its contents (one line of CLI args) to the `screenshot_tool` invocation.
- Otherwise unchanged — Chrome-comparison cases continue to work.

### `test_data/compare.py` changes

- For each case, prefer `<case>_golden.png` over `<case>_chrome.png` when both exist.
- When comparing against `_golden.png`, use a stricter ≥99.5% similarity threshold (self-snapshots have no font-rendering disagreement with Chrome to absorb).
- When comparing against `_chrome.png`, keep the existing 95% threshold.
- If neither exists, skip with the existing "no chrome screenshot" message updated to "no reference image".

### `scripts/update-goldens.ts` changes

- Before launching Playwright for a case, probe for a `<case>.flags` file.
- If `.flags` exists: invoke `screenshot_tool <case>.md <flags-content> --full`, copy the resulting `<case>.png` to `<case>_golden.png`. Skip Playwright for this case.
- Otherwise: unchanged Playwright path producing `<case>_chrome.png`.

### Out-of-scope test work

- Hover-state and click routing — verified manually on first integration; further regressions caught by the visual case (which renders a hovered state would require two cases, deemed not worth the maintenance).
- Focus-steal — verified manually with TC's find bar open. If `MA_NOACTIVATE` proves insufficient in practice, the implementation also has `WS_EX_NOACTIVATE` as a fallback.
- DLL teardown — covered by the existing `feedback_dll_detach` rule plus a manual TC reload pass.

## Risks & open questions

- **TC's find bar focus.** `MA_NOACTIVATE` + `WS_EX_NOACTIVATE` should preserve focus on TC's bar across HUD clicks, but this is environment-dependent and only verifiable with a real TC instance. If it misbehaves, the next mitigation is to skip mouse capture entirely on `WM_LBUTTONDOWN` and resolve the click off `WM_LBUTTONUP` in `WM_PARENTNOTIFY` — but only if needed.
- **Visual jitter on golden.** Self-snapshot goldens are sensitive to GPU/driver-level subpixel differences across machines. The 99.5% similarity threshold should absorb that; if it does not, the threshold drops to 98% before we fall back to a render-engine-level pixel test.
- **Painter parity with HWND wrapper.** Because the painter is exercised in two contexts, a regression in either path could go unnoticed if the visual case only covers one. Mitigation: the visual case uses the painter via `screenshot_tool`, and the runtime HUD uses the same painter — there is exactly one paint code path, so a regression appears in both contexts simultaneously.

## Acceptance

- Counter shows `current / total` in the bottom-right whenever a search has been issued; `0 / 0` for no-match queries; sticky until Esc / file reload / window close.
- `↑` and `↓` advance the cursor through matches via the existing `search_step` path; renderer's current-match highlight updates accordingly.
- Buttons disabled (visually + click-ignored) when total is 0.
- TC's find bar retains keyboard focus when HUD buttons are clicked.
- New formatter tests pass; new visual regression case passes.
- Existing 86 doctest tests and 27 visual cases unchanged.
