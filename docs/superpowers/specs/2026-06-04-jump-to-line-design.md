# Jump to Line + Markdown Line-Number Gutter — Design

**Date:** 2026-06-04
**Status:** Designed. Plan: `docs/superpowers/plans/2026-06-04-jump-to-line.md` (TBD).
**Owner:** aleksej.pawlowskij

## Problem

Neither plugin (`wlx-listerine-md`, `wlx-listerine-colorizer`) can navigate to
a specific line. The colorizer renders a line-number gutter but offers no way
to jump to one; the markdown plugin has no line concept at all. Users reading a
long document have only scroll, PgUp/PgDn, and Home/End.

We want a **Ctrl+G "go to line"** affordance in both plugins, and — because the
markdown renderer has no visible line numbers to aim at — a **line-number
gutter for markdown** as well.

## Goals

- **Ctrl+G** in either plugin opens a small inline prompt; type a number, Enter
  jumps, Esc cancels.
- Markdown gains a **line-number gutter** using *rendered logical-line*
  numbering: contiguous `1..N`, **viewport-independent** — soft word-wrap and
  markdown's joined soft-newlines both keep one number; only hard breaks and
  block boundaries advance the count.
- One shared mechanism drives both plugins: a single `line_tops` index powers
  jump *and* gutter; the prompt UI and jump math live in `runtime/`.
- Keep per-plugin code thin (a `WM_KEYDOWN` Ctrl+G case + routing while the
  prompt is open).

## Non-goals (v1)

- **Source-line numbering in markdown.** Numbers are *rendered logical lines*;
  they intentionally do **not** match the `.md` file's line numbers, an editor,
  git, or a compiler. (Decision made during brainstorming: a paragraph built
  from several source lines is one logical line.)
- **Target-line highlight / flash.** The gutter number is the visual reference.
- **Centering the target.** Target lands at the top of the viewport.
- Remembering the last-used line; a separate "go to percent" (TC's
  `lc_setpercent` already covers percent).
- A floating/overlay or modal-dialog input. The prompt is drawn inline by the
  plugin (rationale below).

## Numbering model — "rendered logical line"

**Principle:** every block contributes **at least one** logical line at its
top; a block whose text contains **hard breaks** (`'\n'`) contributes **one per
hard-break segment**; **soft word-wrap never advances the count** (continuation
rows share the number). This makes numbering independent of window width.

In the markdown layout, inline breaks are already normalized
(`layout_engine.cpp:123-137`): `SoftBreak → ' '`, `HardBreak → '\n'`. So
"logical line" = a `'\n'`-delimited segment within a block, or a whole block
with no internal `'\n'`.

| Block type        | Logical lines emitted                                              |
|-------------------|-------------------------------------------------------------------|
| Heading           | 1, at `rect.top`                                                  |
| Paragraph         | 1 per `'\n'`-delimited segment (usually 1; more with hard breaks) |
| List item         | 1 per item (+1 per internal hard break); nested lists recurse     |
| Code fence        | 1 per code line (block text is code lines joined by `'\n'`)       |
| Blockquote        | the logical lines of its contained blocks                         |
| Table             | 1 per row (header + each body row), at the row's top Y            |
| Thematic break    | 1                                                                 |

**Y of each logical line.** For a block with no `'\n'`, the Y is `rect.top`.
For a block with hard breaks, the segment's Y is `rect.top +
HitTestTextPosition(firstCharOfSegment).y` — i.e. exactly where that segment's
first character renders, which already accounts for any earlier soft-wrap rows.
`HitTestTextPosition` is already used in the codebase for links/selection
(`layout_engine.cpp:191-215`, `render_engine.cpp:306-320`); no new
infrastructure.

The colorizer already lays out **one block per source line**
(`colorizer_layout.cpp:270-501`, `blocks[li].rect.top`), so its logical lines
== source lines and its existing gutter (via `bullet_text`/`bullet_pos`) fits
this model unchanged.

**Table rule** is the one judgment call: a multi-line wrapped cell still counts
as one line for its row. Accepted as predictable.

## Architecture

```
Ctrl+G  (WM_KEYDOWN)
  │
  ▼
ViewWndProc (per-plugin)                       md/host_adapter.cpp:658
  │  active=true; route subsequent keys        colorizer_host_adapter.cpp:792
  ▼
runtime/host/goto_line  (shared)
  │  GotoPrompt{active, buffer}
  │  handle_goto_key(prompt, view, msg, wp):
  │     digit → append (cap 7) ; Backspace → pop
  │     Enter → scroll_to_line(view, clamp(n,1,total)) ; close
  │     Esc   → close (no jump)
  ▼
scroll_to_line(view, n):  view.scroll_y =
     clamp(layout.line_tops[n-1], 0, view.max_scroll_y)
  ▼
render_engine::paint(... , const GotoPrompt*)
     draws document + gutter (scroll-aware),
     then the prompt box in screen space (after SetTransform(Identity))
```

### The line index (the unifying primitive)

`LayoutDocument` (`runtime/layout/layout_document.h`) gains:

```cpp
std::vector<float> line_tops;  // line_tops[n-1] = document-space Y of logical line n
                               // size() == total logical-line count
```

- **Colorizer** pushes `blocks[li].rect.top` as each per-line block is built.
- **Markdown** fills it via a new pass (below).
- **Jump** reads `line_tops[n-1]`; **gutter** draws `i+1` at `line_tops[i]`;
  **total** = `line_tops.size()`.

### New shared module: `src/runtime/host/goto_line.{h,cpp}`

```cpp
namespace wlx::runtime::host {

struct GotoPrompt {
    bool         active = false;
    std::wstring buffer;          // digits typed so far
};

// Abstracts the per-plugin ViewState fields the jump needs.
struct GotoView {
    float&                    scroll_y;
    float                     max_scroll_y;
    const std::vector<float>& line_tops;
    HWND                      hwnd;
};

// Clamp n to [1, total], set scroll_y to that line's top, update scrollbar,
// invalidate. No-op if line_tops empty.
void scroll_to_line(GotoView view, int n);

// Returns true if the key was consumed by the prompt. Handles digits,
// Backspace, Enter (jump+close), Esc (close). Caller invalidates on true.
bool handle_goto_key(GotoPrompt& prompt, GotoView view, UINT msg, WPARAM wp);

}  // namespace wlx::runtime::host
```

`update_scrollbar` / clamp reuse the existing `runtime/host/scroll_handler.h`
helpers.

### New shared module: `src/runtime/layout/line_index.{h,cpp}` (markdown)

```cpp
namespace wlx::runtime::layout {
// Walk laid-out blocks, emit one Y per logical line per the numbering model,
// fill doc.line_tops. Uses IDWriteTextLayout::HitTestTextPosition for
// hard-break segment offsets.
void build_line_index(LayoutDocument& doc);
}
```

Called once at the end of markdown layout. Keeps `layout_engine.cpp` focused.

### The gutter (markdown)

- **Reserve a left column.** Today `left = content_padding (16px)`
  (`layout_engine.cpp:228`). Shift content right by a gutter width measured from
  the widest line number — mirroring the colorizer's
  `code_left = left_margin + ln_col_width` (`colorizer_layout.cpp:180`).
- **Paint numbers.** In `render_engine`, after document blocks are drawn (scroll
  transform active), iterate `line_tops`, drawing `i+1` at its Y in `muted`,
  right-aligned in the gutter. Soft-wrap rows get nothing — `line_tops` holds
  one Y per logical line.
- **Config.** New markdown `line_numbers` flag, **default ON**, mirroring the
  colorizer's `ColorizerDisplayConfig::line_numbers`. Off hides the gutter;
  Ctrl+G still works. Parsed in the md TOML load (`theme_service`).

### The inline prompt (both plugins)

Drawn in `render_engine` **after** the document and after resetting the
transform to identity (screen space, scroll-independent) — the point identified
at `render_engine.cpp` (after `SetTransform(Identity)`, before `EndDraw`). A
small box, **bottom-left**:

```
┌────────────────────────────┐
│ Go to line (1–137): 42▍    │
└────────────────────────────┘
```

Shows the valid range and the typed digits with a caret. Theme colors
(background = code_bg/panel, text = body, border = muted) so it tracks
light/dark. `paint(...)` gains one `const GotoPrompt*` parameter; the box draws
only when active. Both plugins call the same path.

### Per-plugin wiring

- md `host_adapter.cpp:658` and colorizer `colorizer_host_adapter.cpp:792`:
  add a `Ctrl+G` (`wp=='G' && GetKeyState(VK_CONTROL)&0x8000`) case that opens
  the prompt; while `prompt.active`, pass keys to `handle_goto_key` first and
  bypass the existing shortcuts (arrows, Ctrl+C, …) until it closes.
- Each `ViewState` gets a `GotoPrompt` member and passes a `GotoView` view of
  its own `scroll_y` / `max_scroll_y` / `layout->line_tops` / `hwnd`.

### Why inline, not overlay/dialog (recap)

The WLX API provides no input box; only *search* is host-integrated. The
plugin's child window already receives keystrokes when focused (it handles
arrows / Ctrl+C today), so an inline prompt needs **no focus acquisition** and
no nested modal loop inside TC's message pump. A modal dialog would seize focus
and cuts against the codebase's non-activating-overlay house style; a
SearchHud-style child window would need focus to be typed into — the worst of
both. Inline drawing matches the native Direct2D/DWrite approach.

## Hotkey delivery

Ctrl+G is a plain `WM_KEYDOWN` (not Alt → no `WM_SYSKEYDOWN`) and is not a known
TC Lister accelerator, so it should reach the plugin directly — unlike F2,
which TC eats before `TranslateAccelerator` and which the plugin intercepts via
a `WH_GETMESSAGE` hook (`host_integration.h`). **Implementation must verify**
Ctrl+G actually arrives at the WndProc; if TC swallows it, fall back to the same
`WH_GETMESSAGE` hook. Treated as an explicit check, not an assumption.

## Edge cases

| Case                                        | Behavior                                                                 |
|---------------------------------------------|--------------------------------------------------------------------------|
| Empty document                              | Layouts guarantee ≥1 line/block → `line_tops` non-empty; jump clamps to 1 |
| Out-of-range / `0` / huge number            | Clamped to `[1, total]`; `0` → line 1                                    |
| Non-digit key while prompt active           | Ignored (only `0–9`, Backspace, Enter, Esc act)                          |
| Enter with empty buffer                     | Closes prompt, no jump                                                   |
| Resize while prompt open                    | Prompt stays open, box re-anchors; `line_tops` rebuilt on relayout, jump targets recompute (viewport-independent) |
| Deep jump near document end                 | `scroll_y` clamps to `max_scroll_y`; lands as close to top as possible   |
| Gutter width vs many digits                 | Width measured from digit count of `total`; never clips                  |
| `line_numbers = false` (md)                 | Gutter hidden, no left column reserved; Ctrl+G still jumps               |
| Ctrl+G with no document loaded (`!layout`)  | Prompt may open but `total == 0` → Enter no-ops                          |

## Error handling

Best-effort, consistent with the codebase. `scroll_to_line` on an empty
`line_tops` is a no-op. Parsing the buffer is digit-only by construction (no
`std::stoi` overflow path beyond the 7-digit cap → clamp). No user-visible
errors; nothing to `WLX_TRACE` beyond existing scroll paths.

## Testing

| Layer                | What                                                                                                   | Where                                                  |
|----------------------|-------------------------------------------------------------------------------------------------------|--------------------------------------------------------|
| Unit (doctest)       | Markdown line index: heading + multi-source-line paragraph + 3-line code fence + 2-item list → exact logical-line count; `<br>` adds a line; soft-wrap/soft-join do not | `tests/runtime/layout/test_line_index.cpp`             |
| Unit                 | Colorizer line index: N source lines → `line_tops.size()==N`, monotonic Y                              | `tests/plugin_colorizer/layout/test_line_index.cpp`    |
| Unit                 | `scroll_to_line`: clamps `0`, `1`, `total`, `total+50` against known `line_tops` + `max_scroll_y`      | `tests/runtime/host/test_goto_line.cpp`                |
| Unit                 | `handle_goto_key`: sequence `4,2,Back,7,Enter` → buffer `"4"→"42"→"4"→"47"`, jump to 47; Esc → no jump; 7-digit cap | `tests/runtime/host/test_goto_line.cpp`                |
| Manual / integration | Ctrl+G actually reaches the WndProc inside TC (the one thing units can't cover); `WH_GETMESSAGE` fallback if not | —                                                      |
| Visual regression    | A markdown golden case with the gutter visible (`test_data/cases/`), per the screenshot-comparison flow | —                                                      |

The line-index unit tests use the real `IDWriteFactory`, as existing layout
tests do. The Ctrl+G-delivery check is the standard unit-untestable cost of
Win32 message routing inside TC's pump.

## Open questions

None at design time. Potential follow-ups (out of scope):

- Target-line highlight/flash; center-on-jump option.
- Optional *source*-line numbering mode for markdown (matches the `.md` file).
- Remember last-used line per window.
