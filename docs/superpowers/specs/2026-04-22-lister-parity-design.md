# Lister Integration Parity

## Problem

The md and colorizer plugins run as child windows inside Total Commander's Lister, but several host features that are expected to "just work" are broken:

- **F7 / F5 (Find / Find next)** — plugins don't export `ListSearchTextW`, so the Lister Find dialog has nowhere to send queries.
- **F2 (Reload), F3, N (Next File), P (Previous), W (Wrap toggle)** — these are Lister accelerators dispatched from the parent window, but the plugin's `WM_KEYDOWN` handler unconditionally `return 0`, consuming every keystroke before the parent ever sees it.
- **`lcp_wraptext` in the colorizer** — only the md plugin honors the wrap flag; the colorizer reads `word_wrap` from TOML but never responds to `lc_newparams` / ShowFlags.

## Goals

1. Unhandled keystrokes reach the Lister parent, unlocking F2/F3/F5/F7/N/P/W and any other host accelerator without per-key wiring.
2. `ListSearchTextW` implemented on both plugins, driving Lister's native Find dialog with match-case, whole-words, and backward search.
3. All matches highlighted (dimmed), current match emphasized (strong). Scroll the current match into view.
4. `lcp_wraptext` honored by the colorizer the same way the md plugin honors it.

## Non-goals

- `ListPrintW` (print from plugin) — explicitly out of scope.
- Hex string search (`lcs_findhex` — not a standard flag, and Lister's "Search hex string" checkbox applies to the built-in text viewer, not plugins).
- Find-and-replace, regex, incremental find UI, search-as-you-type — the Lister dialog is modal and one-shot.
- Custom find toolbar / inline search bar — we deliberately reuse Lister's dialog.

## Architecture

```
wlx-core/search_engine.{h,cpp}           NEW: text-only search, reused by both plugins
  -> SearchIndex::build(LayoutDocument)  flatten text + offset map
  -> SearchIndex::find_all(query)        returns std::vector<SearchMatch>

src/host_adapter.cpp (md)                CHG: forward unhandled keys; add ListSearchTextW
src/colorizer/colorizer_host_adapter.cpp CHG: same; plus honor lcp_wraptext
src/plugin.def                           CHG: export ListSearchTextW
src/colorizer/colorizer_plugin.def       CHG: export ListSearchTextW

src/render_engine.{h,cpp}                CHG: set_search_matches(); paint highlight rects
                                              (shared by both plugins — colorizer includes
                                              render_engine.h directly, no layout adapter needed)

src/theme_service.*                      CHG: two new palette colors
config/wlx-listerine-md.toml             CHG: search_highlight, search_highlight_current
config/wlx-listerine-colorizer.toml      CHG: same
config/themes/*.toml                     CHG: new Helix scopes for colorizer
```

## Component: SearchIndex (wlx-core)

```cpp
struct SearchMatch {
    int block_index;
    int char_start;   // offset into block's flattened text (UTF-16 code units)
    int char_end;
};

struct SearchQuery {
    std::wstring needle;
    bool match_case = false;
    bool whole_words = false;
    bool backwards = false;   // informational; find_all returns document-order matches
};

class SearchIndex {
public:
    void build(const LayoutDocument& layout);
    std::vector<SearchMatch> find_all(const SearchQuery& q) const;
    bool empty() const { return flat_.empty(); }

private:
    std::wstring flat_;
    std::vector<int> block_starts_;  // flat_[block_starts_[i]] is start of block i
};
```

**Flattening rules:**
- For each `LayoutBlock`, concatenate `text_runs[i].text` in order.
- Append a single `\n` between blocks so whole-word boundaries terminate at block edges and needles cannot cross blocks.
- `block_starts_[i]` = offset in `flat_` where block `i` begins (not including the leading `\n` from the previous block).
- Blocks with `text_runs.empty()` (e.g., horizontal rules, image-only blocks) contribute zero characters but still occupy an index.

**Matching rules:**
- `match_case = false`: case-fold the needle and `flat_` via `CharLowerBuffW` once; cache the folded copy alongside `flat_` to avoid re-folding per query.
- `whole_words = true`: require that the character immediately before and after the match is not alphanumeric and not `_`. Use `iswalnum` + explicit `_` check.
- Needle longer than `flat_`, empty needle, or empty flat: return `{}`.
- All matches returned in document order. `backwards` flag is NOT applied here — the caller (host_adapter) picks direction when advancing `current_match`.

**Translation:** a match at flat-offset `p` lives in block `i = upper_bound(block_starts_, p) - 1`, with `char_start = p - block_starts_[i]`.

## Component: Key forwarding

The current `WM_KEYDOWN` handler in both host_adapters unconditionally returns 0. Replace with:

```cpp
case WM_KEYDOWN: {
    if (!vs) break;

    bool handled = false;

    // ... existing Ctrl+C / Ctrl+A / VK_UP / VK_DOWN / etc. branches,
    //     each sets `handled = true` instead of `return 0` ...

    if (handled) return 0;

    HWND parent = GetParent(hwnd);
    if (parent) return SendMessageW(parent, msg, wp, lp);
    return 0;
}
```

**Esc is special:** Esc currently clears selection if one exists. Keep that, but if there's no selection, fall through to the parent (Lister uses Esc to close the window).

**Scope of "handled":**
- Ctrl+C, Ctrl+A → handled
- VK_UP, VK_DOWN, VK_PRIOR, VK_NEXT, VK_HOME, VK_END → handled (scrolling)
- VK_ESCAPE when selection is active → handled
- Everything else → forwarded

**WM_CHAR / WM_SYSKEYDOWN:** forward unconditionally via the same `SendMessageW(parent, ...)` pattern. Lister's accelerators are registered on the parent; they need the raw keystroke.

**Infinite-loop guard:** none needed. Lister's parent WndProc doesn't re-post to its children; it dispatches to the accelerator table and consumes the message.

## Component: ListSearchTextW export

Signature (from the WLX SDK):

```cpp
int __stdcall ListSearchTextW(HWND ListWin, wchar_t* SearchString, int SearchParameter);
```

- `SearchString` — needle (UTF-16, host-allocated, treat as read-only).
- `SearchParameter` — bitflags: `lcs_findfirst`, `lcs_matchcase`, `lcs_wholewords`, `lcs_backwards`.
- Return: `LISTPLUGIN_OK` on match, `LISTPLUGIN_ERROR` if not found (host shows "Search string not found" dialog).

**ViewState additions (both plugins):**

```cpp
SearchIndex search_index;          // built lazily on first search after layout change
std::vector<SearchMatch> matches;  // current result set (empty when no active search)
int current_match = -1;            // index into `matches`
SearchQuery last_query;            // so F5 re-uses flags
bool index_dirty = true;           // set after do_layout / load_document
```

**Behavior:**

1. If `index_dirty`, rebuild `search_index` from current `layout`. Clear `dirty`. Treat this as an implicit re-find: also force `find_all` below (see step 2) so stale `matches` from a pre-resize index are discarded.
2. Build `SearchQuery` from parameters. If `lcs_findfirst` is set **or** `query != last_query` **or** the index was just rebuilt in step 1, compute `matches = search_index.find_all(query)`. On `lcs_findfirst` set `current_match = -1`; on index rebuild (same query after relayout), clamp the previous `current_match` to `matches.size() - 1` so F5 resumes near where the user left off.
3. If `matches.empty()`: clear `matches`/`current_match` in render state, `InvalidateRect`, return `LISTPLUGIN_ERROR`.
4. Advance `current_match`:
   - Forward: `current_match = (current_match + 1) % matches.size()` (wraps).
   - Backward: `current_match = (current_match - 1 + matches.size()) % matches.size()`.
5. Scroll so the block containing the current match is visible (center it vertically if off-screen; leave it alone if already in viewport).
6. Push `matches` and `current_match` into the renderer; `InvalidateRect`; return `LISTPLUGIN_OK`.

**Wrap-around:** we wrap silently. Lister will separately show the "not found" dialog on `ERROR`, so the user knows when there are no matches at all; wrapping through the same ring avoids a second jarring dialog on F5.

**Escape clears search highlights:** extend the existing Esc path — if there's no selection but there is an active match set, clear matches instead of forwarding to parent.

## Component: Render highlight

Both renderers gain:

```cpp
void set_search_matches(const std::vector<SearchMatch>& matches, int current_index);
```

State is stored on the renderer and painted before text in each `paint()`:

For each match:
1. Look up `block = layout.blocks[match.block_index]`.
2. Walk `block.text_runs` to find the run containing `match.char_start..match.char_end` (a match never crosses runs because runs are per-block and the SearchIndex ensures matches are within one block).
3. `run.layout->HitTestTextRange(char_start_in_run, length, run.rect.left, run.rect.top, metrics, maxCount, &count)` — gives pixel rectangles (multi-line text can produce multiple rects).
4. Apply `scroll_y` offset, fill with `search_highlight` color (or `search_highlight_current` if `i == current_index`).

**Colors:**
- `search_highlight` — background tint, ~30% alpha, rendered behind text.
- `search_highlight_current` — stronger tint, ~60% alpha.
- Text color unchanged (remains readable over both).

**Theme schema:** the two new fields `search_highlight` and `search_highlight_current` live on `ColorPalette` in `theme_service.h` and are loaded from `[colors.light]` / `[colors.dark]` in each plugin's TOML (`config/wlx-listerine-md.toml` and `config/wlx-listerine-colorizer.toml`). Helix themes in `config/themes/` are NOT extended — an earlier attempt to register `ui.search.highlight{,.current}` scopes was dropped because the Helix loader (`helix_theme.cpp`) skips every `ui.*` scope at load time. Both plugins' `RenderEngine` consumes the search-highlight colors via `theme_.palette(dark_mode_)`, so there is no per-plugin divergence; overrides set in either plugin's TOML flow through the shared `ThemeService` path.

## Component: `lcp_wraptext` in colorizer

The colorizer today has `g_display_cfg.word_wrap` from TOML but no response to `lcp_wraptext`. Fix:

1. Add `bool wrap_text = false;` to `ColorViewState`.
2. In `ListLoadW`: `vs->wrap_text = (ShowFlags & lcp_wraptext) != 0;`
3. In `do_layout`: pass `vs->wrap_text` as an override — either by making `layout_source` accept `bool wrap_override` and using it in place of `display_cfg.word_wrap`, or by copying `g_display_cfg` to a local and mutating `word_wrap` before calling.
4. In `lc_newparams`: toggle `vs->wrap_text` from `Parameter & lcp_wraptext`; if changed, relayout.
5. In `ListLoadNextW`: same.

The TOML value becomes the **initial default** when Lister hasn't passed `lcp_wraptext` yet (first load with stale config) — `ShowFlags` always wins once it arrives.

## Data flow (Find)

```
User presses F7 with focus on our window
  -> WM_KEYDOWN(wp=VK_F7) reaches ViewWndProc
  -> Not in handled set; SendMessageW to parent
  -> Lister's parent WndProc triggers the accelerator
  -> Lister shows Find dialog, user types needle, clicks OK
  -> Lister calls ListSearchTextW(hwnd, L"needle", lcs_findfirst | ...)
  -> host_adapter rebuilds index if dirty; runs query; advances cursor
  -> renderer updates its match list; InvalidateRect
  -> paint: highlight rects drawn behind text, current match emphasized
  -> returns LISTPLUGIN_OK

F5 (Find next)
  -> parent-forwarded, Lister calls ListSearchTextW without lcs_findfirst
  -> host_adapter keeps matches, advances current_match, scrolls, repaints

Not found
  -> find_all returns {} — ERROR returned — Lister shows "Search string not found"
  -> highlights cleared

Esc
  -> if selection: clear selection (handled)
  -> elif matches: clear matches (handled, repaint)
  -> else: forward to parent (Lister closes window)
```

## Invalidation

`index_dirty = true` is set in:
- `load_document` (new file loaded)
- `do_layout` (viewport change, wrap toggle, theme switch) — because block offsets may have changed

`matches` and `current_match` are **not** automatically cleared on invalidation — they stay until the next search call or Esc. But because `last_query` is also kept, the next `ListSearchTextW` call after a dirty-index rebuild re-runs `find_all` and replaces the stale matches (see "Behavior" step 1). This means a resize during active search preserves the user's position as closely as possible rather than silently showing stale highlight rectangles over the wrong text.

## Error handling

- `ListSearchTextW` with `ListWin` not in `g_views` → `LISTPLUGIN_ERROR`.
- `ListSearchTextW` with null / empty `SearchString` → `LISTPLUGIN_ERROR`.
- `SearchIndex::build` on an empty layout → leaves `flat_` empty; subsequent `find_all` returns `{}`.
- `HitTestTextRange` returning `E_NOT_SUFFICIENT_BUFFER` → retry with the size it reports (standard DWrite pattern).
- All existing `LISTPLUGIN_ERROR` paths in `ListSendCommand` remain unchanged.

## Testing

**Unit tests (added to `tests/` and `colorizer-tests`):**
- `SearchIndex::build`: flattening, block_starts correctness, empty layout, layout with empty text runs.
- `SearchIndex::find_all`: basic, case-insensitive, whole-words (with punctuation, underscore, unicode), overlapping candidates, needle longer than haystack, multi-match same block, multi-block matches.
- Match-to-block translation: corner cases at block boundaries.

**Integration tests (WndProc / host exports):**
- Key forwarding: `WM_KEYDOWN` with VK_F7 returns to parent (mock parent records message); VK_UP does not forward.
- `ListSearchTextW` with `lcs_findfirst` resets; without it advances; not-found returns ERROR.
- Wrap-around: 3 matches, 4 F5 presses → back at first match.
- `lcp_wraptext` in colorizer: toggling the flag triggers relayout.

**Visual regression:**
- New case in `test_data/cases/`: a markdown file with a known needle, golden screenshot showing highlighted matches with current-match emphasized. Reuses existing `screenshot_tool` + Chrome compare flow (see `scripts/visual-test.sh`).
- Same for colorizer (a source file with highlighted matches).

**Manual smoke test (required before completion):**
1. Build both plugins, install into TC.
2. Open a markdown file; press F7; type "the"; verify dialog appears, all matches highlighted after OK, current match emphasized.
3. Press F5 repeatedly; verify cycling and wrap-around.
4. Press N; verify next file loads (was broken).
5. Press W; verify wrap toggles (was broken).
6. Press Esc during active search; verify highlights clear.
7. Repeat 2–6 on a large source file with the colorizer.

## Open questions

None blocking. Potential follow-ups (not in this spec):
- Match count display — Lister's dialog doesn't have a spot for it; would need a custom status bar.
- Regex / glob — would require our own dialog; vanilla Lister's isn't flexible enough.
