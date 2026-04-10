# Text Selection & Copy

## Goal

Add mouse-driven text selection, keyboard copy (Ctrl+C), select-all (Ctrl+A), and code block copy buttons to the WLX markdown lister plugin. Integrate with Total Commander's `lc_copy` and `lc_selectall` commands.

## Selection Model

### TextPosition

A position in the document is a `{block_index, char_offset}` pair:

```cpp
struct TextPosition {
    int block_index = -1;   // index into LayoutDocument::blocks
    int char_offset = 0;    // character offset within the block's flattened text
    bool valid() const { return block_index >= 0; }
};
```

Comparison is lexicographic: first by `block_index`, then by `char_offset`.

### SelectionState

```cpp
struct SelectionState {
    TextPosition anchor;    // where mouse-down happened
    TextPosition active;    // current drag endpoint
    bool selecting = false; // true during mouse drag
};
```

Added as a field in `ViewState`.

- **No selection**: `!anchor.valid()` or anchor == active
- **Active selection**: anchor != active; the highlighted range is `[min(anchor, active), max(anchor, active)]`
- **Select-all**: anchor = `{0, 0}`, active = `{last_block, last_char}`

### Selectable blocks

Blocks with text runs participate in character-level hit-testing: Heading, Paragraph, ListItem, TaskList, CodeFence, TableCell. HorizontalRule and empty blocks are skipped for hit-testing but fully highlighted when they fall between the two selection endpoints.

## Mouse Interaction

### WM_LBUTTONDOWN — Start selection

1. Convert pixel coordinates to DIPs via `pixel_to_dip_x/y`.
2. Add `scroll_y` to get document coordinates.
3. Hit-test to find the block under the cursor. For blocks with text runs, call `IDWriteTextLayout::HitTestPoint` to get the character offset.
4. Set `anchor = active = {block, offset}`, `selecting = true`.
5. Call `SetCapture(hwnd)` to track mouse outside the window.
6. Clear any previous selection.
7. Do NOT fire link actions on mouse-down (wait for mouse-up to distinguish click from drag).

### WM_MOUSEMOVE — Extend selection

Only when `selecting == true`:

1. Convert coordinates to DIPs + scroll_y.
2. Hit-test to find new `{block, offset}`.
3. If mouse is above viewport (y < 0): start auto-scroll timer, scroll up by one line height per tick.
4. If mouse is below viewport (y > viewport_h): auto-scroll down.
5. If mouse returns to viewport: kill auto-scroll timer.
6. Update `active`, invalidate window.

When `selecting == false`: update cursor and link hover state as before, plus track `hovered_code_block` for the copy button.

### WM_LBUTTONUP — Finish selection

1. `ReleaseCapture()`, `selecting = false`, kill auto-scroll timer.
2. If `anchor == active` (no drag): clear selection. If the position is on a link, fire link action (existing behavior).
3. If `anchor != active`: selection is complete, do not fire link actions.

### WM_LBUTTONDBLCLK — Select word

Requires `CS_DBLCLKS` in the window class style (add to `ensure_window_class`).

1. Hit-test to find `{block, offset}`.
2. Scan the block's text for word boundaries around `offset` (whitespace/punctuation delimiters).
3. Set `anchor = {block, word_start}`, `active = {block, word_end}`.

### Hit-testing between blocks

When the mouse is in the gap between blocks (paragraph spacing, heading margins), snap to the nearest block boundary: if closer to the block above, use `{above_block, last_char}`; if closer to the block below, use `{below_block, 0}`. This ensures dragging through gaps produces a continuous selection without dead zones.

### Cursor

- **I-beam** (`IDC_IBEAM`): over blocks that have text runs.
- **Hand** (`IDC_HAND`): over links (existing behavior, takes priority over I-beam).
- **Arrow** (`IDC_ARROW`): over empty space, horizontal rules, code block copy button.

## Code Block Copy Button

### Appearance

A small button in the top-right corner of each code fence block's background rect, inset by a few pixels of padding.

- **Icon**: clipboard/copy glyph drawn with D2D (two overlapping rectangles), or a Unicode glyph from Segoe Fluent Icons.
- **Normal state**: semi-transparent, blends with code background.
- **Hover state**: brighter, more opaque.
- **After copy**: icon changes to a checkmark for ~1 second (use `SetTimer`), then reverts.

### State tracking

`ViewState` gets a `hovered_code_block` field (int, -1 when none). Updated during `WM_MOUSEMOVE` — set when cursor is inside a CodeFence block rect, cleared otherwise.

### Hit-testing priority

On `WM_LBUTTONUP`, check if the click is inside a code block copy button BEFORE checking selection/link logic. If it is, copy the full code text to clipboard and show feedback.

### Copy content

The full text content of the code fence block (all text runs concatenated), excluding the language tag. Internal newlines preserved.

## Keyboard & TC Commands

### Keyboard shortcuts (WM_KEYDOWN)

- **Ctrl+C**: if selection exists, copy selected text to clipboard.
- **Ctrl+A**: select all — `anchor = {0, 0}`, `active = {last_block, last_char}`. Invalidate.
- **Escape**: clear selection. Invalidate.

### TC commands (ListSendCommand)

- **lc_copy**: same as Ctrl+C. Return `LISTPLUGIN_OK` if text was copied, `LISTPLUGIN_ERROR` if no selection.
- **lc_selectall**: same as Ctrl+A. Return `LISTPLUGIN_OK`.

## Text Extraction

When copying, walk blocks from selection start to selection end:

1. For each block in range, get the text substring:
   - **Start block**: from `start.char_offset` to end of text.
   - **End block**: from 0 to `end.char_offset`.
   - **Middle blocks**: full text.
   - **Single block**: from `start.char_offset` to `end.char_offset`.
2. Join blocks with `\r\n`.
3. For list items, prepend bullet character or number.
4. For code fences, preserve internal newlines as-is.
5. For headings, just the text (no markdown `#` prefix).
6. Skip blocks with no text runs (HorizontalRule, etc.) — they contribute a blank line.

Place the result on the clipboard as `CF_UNICODETEXT` via `OpenClipboard` / `EmptyClipboard` / `SetClipboardData` / `CloseClipboard`.

## Rendering Selection Highlights

### Draw order

Per block: background -> **selection highlight** -> decorations (borders, rules) -> bullets -> text.

### Highlight logic

During `RenderEngine::paint`, for each visible block:

1. **Block fully inside selection range** (start_block < block_index < end_block): draw a filled rect covering the entire text area using `colors.selection`.
2. **Block is the start or end of selection** (partial): call `IDWriteTextLayout::HitTestTextRange(start_offset, length)` on the selected character range to get one or more highlight rects. Draw each with `colors.selection`.
3. **Block is both start and end** (selection within single block): same as partial, using `[start.char_offset, end.char_offset)`.
4. **Block outside selection**: no highlight.

### Selection color

Use the existing `colors.selection` field from the theme palette (already defined in `ColorPalette` but currently unused).

### RenderEngine changes

`paint()` receives an optional selection range (start/end `TextPosition`) in addition to the layout and scroll_y. The render engine does not own selection state — host_adapter passes the computed range each frame.

## Auto-scroll

When dragging beyond the viewport:

- Use `SetTimer(hwnd, TIMER_AUTOSCROLL, 50, nullptr)` for ~20fps scroll rate.
- On `WM_TIMER` with `TIMER_AUTOSCROLL`: scroll by one line height in the appropriate direction, update selection active point, invalidate.
- Kill timer on `WM_LBUTTONUP` or when mouse returns to viewport.

## Files Modified

- **host_adapter.cpp**: selection state in ViewState, WM_LBUTTONDOWN/MOVE/UP/DBLCLK handlers, WM_KEYDOWN for Ctrl+C/A/Esc, WM_TIMER for auto-scroll, ListSendCommand for lc_copy/lc_selectall, cursor management, clipboard operations.
- **render_engine.h/cpp**: `paint()` takes selection range param, draw selection highlights, draw code block copy button.
- **layout_engine.h**: `TextPosition` struct definition (shared by host_adapter and render_engine).
- **document_model.h**: no changes needed — TextPosition references LayoutDocument blocks, not the AST.

## Not in scope

- Rich text / markdown copy (plain text only).
- Triple-click to select paragraph.
- Shift+click to extend selection.
- Find/search (Ctrl+F).
