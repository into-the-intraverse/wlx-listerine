# Lister Parity — Post-Review Fixes

## Problem

The Lister-parity work (spec `2026-04-22-lister-parity-design.md`, 22 commits between 2026-04-22 and 2026-04-23) shipped working F7/F5/F2/N/P/W plumbing, but an extensive code review flagged ten issues. Three are structural (H-severity), four are behavior or coverage gaps (M-severity), three are documentation-level (L-severity). This spec bundles all ten into a single coherent refactor.

### Findings (from review)

- **H1.** Parent key forwarding was removed in commit `4c30d7d`. Unhandled WM_KEYDOWN falls to `DefWindowProcW` with no trace. The assumption that TC's `TranslateAccelerator` catches F5/F7/N/P/W before `DispatchMessage` reaches plugin WndProc is unverified and invisible if it breaks.
- **H2.** ~500 lines of integration code (F2 hook, parent subclass, reload-ID discovery, all associated globals) are byte-for-byte duplicated between `src/host_adapter.cpp` and `src/colorizer/colorizer_host_adapter.cpp`. Future fixes have to happen twice; branches are likely to drift.
- **H3.** `ParentSubclassProc` reload branch uses `return 0` after the first matching view. Multi-view-per-parent setups (TC Quick View in a second panel) reload only one window.
- **M1.** Menu-discovery heuristics (`menu→F2` and `menu→menu`) can mis-learn the reload command ID. Clicking an unrelated menu item within 5s of pressing F2 permanently teaches the code that the unrelated item == reload. File→Reload then mis-fires on the wrong item.
- **M2.** `ListSearchTextW` control flow (findfirst reset, requery on dirty index, cursor clamp, backward advancement, wrap-around) is untested. Unit tests cover only the flatten/find_all layer.
- **M3.** `scroll_to_match` centers the containing block, not the match position within the block. A tall block with a match near its bottom can leave the match off-screen.
- **M4.** `paint_search_highlights` is O(blocks × matches): for each block it re-walks the match list from index 0 until it hits `block_index`. Redundant prefix scans on documents with many blocks and sparse matches.
- **L1.** `find_reload_id_via_accel_resources` is documented in commit `4c30d7d` as unlikely to work (TC builds accel tables at runtime from `.lng`/`.ini`, not static `RT_ACCELERATOR` resources), but the code has no inline comment saying so.
- **L2.** `g_display_cfg.line_height_factor` (colorizer) is captured once in `ensure_theme()` and never refreshed. Not a current-runtime bug (theme doesn't reload), but fragile against future theme-reload work.
- **L3.** `is_word_char` uses `iswalnum`, which is locale-dependent. Acceptable for a file viewer; should be documented.

## Goals

1. Eliminate the duplicated integration surface. One implementation, shared by both plugins.
2. Fix the multi-view regression (H3) and the menu-discovery false-positive (M1).
3. Make `ListSearchTextW`'s control flow directly unit-testable.
4. Make the "unhandled keys fall through" assumption (H1) visible via trace output.
5. Tighten `scroll_to_match` and `paint_search_highlights` to their known-correct forms.
6. Annotate each L-severity item inline so future readers don't have to dig through commit history.

## Non-goals

- Restoring `PostMessage(parent, ...)` forwarding. Manual smoke-testing confirmed TranslateAccelerator handles accel keys upstream; we document the invariant instead.
- Supporting File→Reload in TC builds where both `RT_ACCELERATOR` and `WM_INITMENUPOPUP` text-based discovery fail. F2 is the primary UX and always works via the hook.
- Visual-regression test for search highlights. Out of scope; tracked separately.
- Changing `SearchIndex` semantics. The search engine itself stays as-is.

## Architecture

### New files

```
src/wlx_host_common.h   NEW: header-only template HostIntegration<V>
                             F2 hook + parent subclass + reload-ID discovery
src/search_ops.h        NEW: pure search_step<V>() helper, testable
tests/test_search_ops.cpp        NEW: unit tests for search_step
tests/test_wlx_host_common.cpp   NEW: unit tests for HostIntegration
```

### Modified files

```
src/host_adapter.cpp                     CHG: strip ~300 LoC, use HostIntegration<ViewState>,
                                              add reload_view() free function
src/colorizer/colorizer_host_adapter.cpp CHG: same treatment with ColorViewState
src/render_engine.cpp                    CHG: cursor-based paint_search_highlights
src/render_engine.h                      CHG: paint_search_highlights takes size_t&
src/search_engine.cpp                    CHG: L3 comment on iswalnum locale
CMakeLists.txt                           CHG: CMAKE_CXX_STANDARD 17 → 20
CLAUDE.md                                CHG: C++17 → C++20 note
```

### Deleted surface

- `g_msg_hook`, `g_hook_refcount`, `g_reload_menu_id`, `g_pending_f2_capture`, `g_candidate_reload_id`, `g_candidate_time`, `CANDIDATE_TTL_MS`, `g_parent_refcount` (both plugins) — move into the template.
- `find_menu_item_by_accel`, `find_menu_owner`, `install_parent_subclass`, `uninstall_parent_subclass`, `install_msg_hook`, `uninstall_msg_hook`, `GetMsgHookProc`, `ParentSubclassProc`, `accel_enum_proc_`, `find_reload_id_via_accel_resources` (both plugins) — same.
- The `menu→F2` and `menu→menu` heuristic branches in `ParentSubclassProc` — gone (M1 fix).
- `ColorizerDisplayConfig::line_height_factor` field (L2 fix) — read from theme at each use.

## Component: `HostIntegration<V>` template

### Concept

```cpp
template <typename V>
concept HostView = requires(V& v, const wchar_t* p) {
    { v.hwnd }            -> std::convertible_to<HWND>;
    { v.file_path }       -> std::same_as<std::wstring&>;
    { v.subclass_target } -> std::same_as<HWND&>;
    reload_view(v, p);  // ADL hook
};
```

The three required fields already exist on both `ViewState` and `ColorViewState`. Each plugin adds one free function in its host adapter's translation unit:

```cpp
static void reload_view(ViewState& vs, const wchar_t* path) {
    load_document(&vs, path);
    InvalidateRect(vs.hwnd, nullptr, FALSE);
}
```

The md plugin's `load_document` already calls nothing after itself (invalidate is done by callers), so `reload_view` adds the invalidate. The colorizer plugin's `load_document` already calls `InvalidateRect` internally, so its `reload_view` is just `load_document(&vs, path)`. Either way the hook and subclass call into a single free function that encapsulates the plugin-specific reload.

### Class

```cpp
template <HostView V>
class HostIntegration {
public:
    void attach(V* vs, HWND parent_hint);
    void detach(V* vs);
    void emergency_cleanup();
    UINT reload_menu_id() const { return reload_menu_id_; }

private:
    static LRESULT CALLBACK get_msg_hook_(int, WPARAM, LPARAM);
    static LRESULT CALLBACK parent_subclass_proc_(HWND, UINT, WPARAM, LPARAM,
                                                   UINT_PTR, DWORD_PTR);
    static HWND find_menu_owner_(HWND start);
    static UINT find_reload_id_via_accel_resources_();
    static UINT find_menu_item_by_accel_(HMENU menu, const wchar_t* accel);

    inline static HostIntegration* self_ = nullptr;

    std::unordered_map<HWND, V*> views_;
    std::unordered_map<HWND, int> parent_refcount_;
    HHOOK msg_hook_ = nullptr;
    int hook_refcount_ = 0;
    UINT reload_menu_id_ = 0;
};
```

### Instantiation

Each plugin declares one instance at file scope:

```cpp
// host_adapter.cpp
static HostIntegration<ViewState> g_integration;
```

Each DLL gets its own instantiation with its own statics — matching the static-data-per-DLL reality. The `self_` singleton pointer is set in `attach` (first call) and cleared in `detach` (last call); the static callbacks use it to recover the instance. Only one instance per template argument per DLL is supported, which is exactly what's needed.

### Behavior

**`attach(vs, parent_hint)`:**
1. Insert `vs` into `views_` keyed on `vs->hwnd`.
2. If `hook_refcount_++ == 0`: install `WH_GETMESSAGE` on current thread; store in `msg_hook_`.
3. If `reload_menu_id_ == 0`: try `find_reload_id_via_accel_resources_()`. If non-zero, store.
4. Walk from `parent_hint` up via `find_menu_owner_()`. If a window with a menu is found (or if not, fall back to `parent_hint`), record as `vs->subclass_target`. Increment `parent_refcount_[target]`; on first reference, call `SetWindowSubclass(target, parent_subclass_proc_, ...)`.
5. Set `self_ = this`.

**`detach(vs)`:**
1. Decrement `parent_refcount_[vs->subclass_target]`; if zero, `RemoveWindowSubclass` and erase.
2. Erase `vs` from `views_`.
3. If `--hook_refcount_ == 0`: `UnhookWindowsHookEx(msg_hook_)`; nullify.
4. If `views_.empty()`, clear `self_`.

**`emergency_cleanup()`** (called from `DLL_PROCESS_DETACH`, `reserved == nullptr` branch):
1. Remove every subclass still in `parent_refcount_`.
2. Unhook `msg_hook_` if non-null.
3. Clear maps.

### Discovery paths (kept)

1. **Accelerator-resource scan** (`attach`, once per DLL): `EnumResourceNamesW(RT_ACCELERATOR, ...)` on `GetModuleHandleW(nullptr)`, load each table, look for modifier-less `VK_F2`. Usually returns 0 on TC (per L1 comment) but cheap.
2. **`WM_INITMENUPOPUP` text scan** (`parent_subclass_proc_`, lazy): for each item whose text contains a tab followed by "F2" and either end-of-string or whitespace, record its command ID. Persists across menu opens.

### Discovery paths (removed — M1 fix)

- `menu→F2`: user clicks a menu item, then presses F2 within 5s → record item as reload. Removed because a user who clicks any plugin-irrelevant menu item and then presses F2 (a perfectly natural sequence) teaches the code a wrong ID.
- `menu→menu`: same menu item clicked twice within 5s → record as reload. Removed for the same reason (second click could be anything).

Fallback behavior: if both remaining discovery paths fail, the File→Reload menu item is a silent no-op. F2 always works via the hook.

### `parent_subclass_proc_` — multi-view fan-out (H3 fix)

```cpp
if (src == 0 && reload_menu_id_ != 0 && cmd == reload_menu_id_) {
    bool any = false;
    for (auto& [pwnd, vs] : views_) {
        if (vs->subclass_target == hwnd && !vs->file_path.empty()) {
            reload_view(*vs, vs->file_path.c_str());
            any = true;
        }
    }
    if (any) return 0;
}
```

The `src == 1` (accel) branch is now simpler: always return 0 (eat) when the cmd matches `reload_menu_id_`, because the F2 hook already reloaded all matching views. No more `g_pending_f2_capture` dance.

## Component: `search_step<V>` helper

`V` for `search_step` is **not** the same contract as `HostView`. It requires only the search-related fields, not the integration fields (`hwnd`, `subclass_target`). A dedicated concept documents this:

```cpp
template <typename V>
concept SearchState = requires(V& v) {
    { v.layout };              // pointer or reference to LayoutDocument
    { v.search_index }   -> std::same_as<SearchIndex&>;
    { v.matches }        -> std::same_as<std::vector<SearchMatch>&>;
    { v.current_match }  -> std::same_as<int&>;
    { v.last_query }     -> std::same_as<SearchQuery&>;
    { v.index_dirty }    -> std::same_as<bool&>;
};
```

Both real `ViewState` and `ColorViewState` satisfy this (they already carry those fields). Test fakes satisfy it by mirroring the field set without the HWND/renderer machinery.

```cpp
// src/search_ops.h
#pragma once
#include "search_engine.h"
#include "layout_engine.h"

struct SearchStepResult {
    bool has_match;                     // false → return LISTPLUGIN_ERROR
    int cursor;                         // new current_match index (-1 if none)
    std::vector<SearchMatch> matches;   // current match set
    bool index_was_rebuilt;             // diagnostic; for tests
};

template <SearchState V>
SearchStepResult search_step(V& vs, const SearchQuery& q, bool findfirst) {
    bool rebuilt = false;
    if (vs.index_dirty) {
        vs.search_index.build(*vs.layout);
        vs.index_dirty = false;
        rebuilt = true;
    }
    const bool query_changed = q != vs.last_query;
    const bool requery = findfirst || rebuilt || query_changed;
    if (requery) {
        vs.matches = vs.search_index.find_all(q);
        if (findfirst || query_changed) {
            vs.current_match = -1;
        } else if (vs.current_match >= static_cast<int>(vs.matches.size())) {
            vs.current_match = static_cast<int>(vs.matches.size()) - 1;
        }
        vs.last_query = q;
    }
    if (vs.matches.empty()) {
        vs.current_match = -1;
        return {false, -1, {}, rebuilt};
    }
    const int n = static_cast<int>(vs.matches.size());
    vs.current_match = q.backwards
        ? (vs.current_match <= 0 ? n - 1 : vs.current_match - 1)
        : (vs.current_match + 1) % n;
    return {true, vs.current_match, vs.matches, rebuilt};
}
```

`V` only needs the search-state fields (`search_index`, `matches`, `current_match`, `last_query`, `index_dirty`, `layout`). Tests use a minimal fake with a hand-built `LayoutDocument`.

`ListSearchTextW` becomes:

```cpp
int __stdcall ListSearchTextW(HWND ListWin, wchar_t* SearchString, int SearchParameter) {
    auto it = g_views.find(ListWin);
    if (it == g_views.end()) return LISTPLUGIN_ERROR;
    if (!SearchString || !*SearchString) return LISTPLUGIN_ERROR;
    auto* vs = it->second;
    if (!vs->layout) return LISTPLUGIN_ERROR;

    SearchQuery q{ SearchString,
                   (SearchParameter & lcs_matchcase) != 0,
                   (SearchParameter & lcs_wholewords) != 0,
                   (SearchParameter & lcs_backwards) != 0 };
    const bool findfirst = (SearchParameter & lcs_findfirst) != 0;

    auto r = search_step(*vs, q, findfirst);
    if (!r.has_match) {
        if (vs->renderer) vs->renderer->set_search_matches({}, -1);
        InvalidateRect(vs->hwnd, nullptr, FALSE);
        return LISTPLUGIN_ERROR;
    }
    scroll_to_match(vs, r.matches[r.cursor]);
    if (vs->renderer) vs->renderer->set_search_matches(r.matches, r.cursor);
    InvalidateRect(vs->hwnd, nullptr, FALSE);
    return LISTPLUGIN_OK;
}
```

## Component: `scroll_to_match` precision (M3)

After the existing block-centering pass, if a current-match run is available, call `HitTestTextRange` on that run for the match's character range to get the match's first rect. If `rect.top` is still above the viewport or `rect.bottom` is below it after centering, set `scroll_y = match_rect_top - viewport_h * 0.33f` (place match roughly 1/3 from top) and clamp. Single `HitTestTextRange` call per F5 — same cost as one match in the painter, negligible.

## Component: `paint_search_highlights` cursor (M4)

Current loop per block scans `search_matches_` from index 0. Change `paint()` to carry a local `size_t match_cursor = 0;` across the block loop and pass it by reference into `paint_search_highlights`. The helper advances `match_cursor` past any match with `block_index < current_block_index`, then iterates until `block_index > current_block_index`, then returns, leaving `match_cursor` at the first unprocessed match for the next block. Total work: O(matches + blocks) instead of O(matches × blocks).

Signature change:

```cpp
void paint_search_highlights(const LayoutBlock& block, int block_index,
                             float offset_y, size_t& match_cursor);
```

## H1 — key-forwarding invariant made visible

In both host adapters' `WM_KEYDOWN` case, after the handled dispatch and before `break;`:

```cpp
// Unhandled keys fall to DefWindowProcW. TC's TranslateAccelerator runs on
// the message loop ahead of DispatchMessage, so F5/F7/N/P/W are claimed
// upstream and never reach this point. F2 is the exception — TC consumes
// it before TranslateAccelerator, which is why we intercept it via
// WH_GETMESSAGE hook (see wlx_host_common.h). If a new accel key ever
// reaches this branch, the trace below will surface it.
WLX_TRACE(L"WM_KEYDOWN unhandled, falling through vk=0x%X", (unsigned)wp);
break;
```

No behavior change. Makes the invariant self-documenting and tractable if it ever breaks.

## L-level inline annotations

**L1** — above `find_reload_id_via_accel_resources_()` in `wlx_host_common.h`:
```cpp
// TC builds accelerator tables at runtime from .lng/.ini; this RT_ACCELERATOR
// scan returns 0 on shipping TC builds. Kept as a cheap try-first in case a
// future build or alternative file manager ships static accel resources.
```

**L2** — `ColorizerDisplayConfig::line_height_factor` removed. The three call sites in `colorizer_host_adapter.cpp` (two `WM_VSCROLL` branches, one `WM_MOUSEWHEEL`, one `WM_TIMER` autoscroll) read `g_theme.spacing().line_height_factor` directly. Identical runtime behavior today, correct against any future theme-reload work.

**L3** — above `is_word_char` in `search_engine.cpp`:
```cpp
// Locale-dependent — iswalnum's behavior for non-ASCII alphanumerics (e.g.
// Cyrillic, Greek) depends on the thread's C locale. Acceptable for a file
// viewer's search UX; tightening this would require ICU or a custom table.
```

## Testing

### `tests/test_search_ops.cpp` (new)

Fake V:
```cpp
struct FakeV {
    LayoutDocument layout_store;
    LayoutDocument* layout = &layout_store;
    SearchIndex search_index;
    std::vector<SearchMatch> matches;
    int current_match = -1;
    SearchQuery last_query;
    bool index_dirty = true;
};
```

Cases:
- findfirst on a 3-match doc → cursor=0, matches.size()==3.
- Four F5 presses on a 3-match doc → cursor goes 0,1,2,0 (wrap).
- backwards on cursor=0 → wraps to n-1.
- No-match needle → has_match=false, cursor=-1, matches empty.
- Same query, index_dirty set, current_match was 2, matches rebuilt with only 2 results → cursor clamps to 1.
- Query change without findfirst → resets cursor to -1 then advances to 0.
- Dirty index + findfirst both true → single rebuild, single find_all.

### `tests/test_wlx_host_common.cpp` (new)

Fake V:
```cpp
struct FakeV {
    HWND hwnd = nullptr;
    std::wstring file_path;
    HWND subclass_target = nullptr;
    int reload_count = 0;
};
inline void reload_view(FakeV& v, const wchar_t*) { v.reload_count++; }
```

Cases:
- Concept check: `static_assert(HostView<FakeV>);`.
- Attach two FakeVs with different `hwnd` but same `subclass_target`. Directly invoke `parent_subclass_proc_` with a menu-source `WM_COMMAND(reload_menu_id_)` — both FakeVs' `reload_count` should increment (H3 regression test). Requires exposing `reload_menu_id_` as settable for tests, or a test-only `force_reload_id(UINT)` on `HostIntegration`.
- Attach, detach one FakeV — parent subclass stays (refcount=1). Detach the second — subclass removed.
- `emergency_cleanup()` after partial attach — no crash, all subclasses removed.

The H3 regression test requires synthesizing a Windows message without a real message pump. We call `parent_subclass_proc_` directly as a static function rather than going through `SendMessage`, which avoids needing a real HWND.

### Existing coverage unchanged

- `tests/test_search_engine.cpp` — SearchIndex flatten/find_all, 11 cases.
- `tests/test_theme_service.cpp` — palette + theme_hash, including search-highlight entries.

### Not tested (manual smoke)

- F2 hook itself (needs a message pump).
- `scroll_to_match` precision (needs a real DWrite layout).
- Key fall-through trace (H1).

### Manual smoke test (required before merge)

1. Rebuild both plugins, install into TC.
2. Open a markdown file: F7, type "the", OK. Highlights appear, current match emphasized.
3. F5 cycles through all matches and wraps.
4. F2 reloads the file.
5. File menu → Reload (if TC surfaces the accel hint): reloads the file.
6. Open a second Lister window with another file. F2 in each window reloads that window's file.
7. Open two panels with the same file via Quick View + main Lister. File→Reload reloads both.
8. Esc clears search highlights.
9. Enable `-DWLX_TRACE_ENABLE=ON`, open DebugView, exercise keys. Verify no unexpected WM_KEYDOWN traces reach the fall-through branch during F5/F7/N/P/W use.
10. Repeat 2–8 with the colorizer on a source file.

## Migration

1. Bump `CMAKE_CXX_STANDARD` to 20. Update `CLAUDE.md`.
2. Write `src/wlx_host_common.h`, `src/search_ops.h`.
3. Write new tests; ensure they compile and pass against the new headers.
4. Port `src/host_adapter.cpp`: add `reload_view`, replace globals+helpers with `g_integration.attach/detach`, replace `ListSearchTextW` body with `search_step` call.
5. Port `src/colorizer/colorizer_host_adapter.cpp` the same way.
6. Delete the duplicated surface from both adapters.
7. Update `src/render_engine.{h,cpp}` for M4 cursor signature.
8. Inline-annotate L1, L2, L3.
9. Add H1 trace + comment in both adapters.
10. Run full build + test suite. Run the manual smoke test.

## Error handling

- `reload_view` failures (file gone, parse error): plugin's existing `load_document` already handles these silently. No change.
- `attach` called twice for the same `vs`: UB by contract. Callers (WLX `ListLoadW`) never do this.
- `detach` called for an unknown `vs`: silently no-op. Matches existing `ListCloseWindow` behavior.
- `emergency_cleanup` while other threads are dispatching messages: the `DLL_PROCESS_DETACH` path runs under the loader lock — no other dispatch is active. Matches the existing invariant (memory `feedback_dll_detach.md`).
- Empty `views_` with non-empty `parent_refcount_` (theoretically impossible): `emergency_cleanup` handles it by iterating `parent_refcount_` directly.

## Open questions

None blocking.

Potential follow-ups (deliberately out of scope):
- Visual-regression case for search highlights. Needs a golden Chrome PNG; tracked separately.
- True per-character match y in `scroll_to_match` at query time (store rect alongside each `SearchMatch`). Current approach (lazy HitTestTextRange on current match) is enough.
- Replace `iswalnum` with a Unicode-aware classifier. Requires ICU dependency; not justified for a file-viewer search.
