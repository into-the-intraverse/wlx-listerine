# Right-Click Context Menu — Design

**Date:** 2026-05-07
**Status:** Approved (brainstorm); pending implementation plan
**Owner:** aleksej.pawlowskij

## Problem

Both plugins (`wlx-listerine-md` and `wlx-listerine-colorizer`) display
text content but neither shows a right-click context menu. TC's default
lister offers one (`Copy as text` / `Copy as Hex` / `Search for selected
text with Google`); our plugins offer nothing — right-click is a no-op.

Beyond parity, two plugin-specific gaps surface naturally on right-click:

- The colorizer detects a grammar from file extension; when detection is
  wrong (or there's no extension), users have no in-plugin way to force
  one.
- Editing the plugin TOML config currently requires navigating to the
  install dir manually.

## Goals

- Provide a native Win11 popup menu on right-click in both plugins —
  visually consistent with TC's own lister menu.
- Cover the obvious selection actions: Copy, Select All, Search with
  Google.
- Surface plugin-specific affordances on the same path: in markdown,
  link/code-block actions; in the colorizer, force-language and config
  editing.
- Lift the shared mechanics into `runtime/host/` so both plugins drive
  the same code, mirroring the existing `clipboard` / `selection_helpers`
  / `host_integration` split.
- Keep new code small: the action implementations (copy, link open,
  code-block copy, select-all) already exist and are reused.

## Non-goals

- Custom-painted menu matching the search HUD aesthetic. Native
  `TrackPopupMenu` follows OS dark mode on Win11; users get the same
  menu look TC's own lister has, with keyboard nav and accessibility
  for free.
- Persistent / per-extension / per-file language overrides. Force-
  language is **session-only** in this iteration. Persistence is a
  later feature with its own UX (config writes, cleanup, scope rules).
- Per-fenced-code-block force-language in markdown. The markdown
  document itself has no document-wide language to override; per-block
  override is its own design (auto-detect heuristics, where overrides
  live, cache invalidation).
- `Copy as Hex` (TC default-lister item; meaningful only in a binary
  view, neither of our plugins is one).
- View-state toggles (Reload / Toggle Wrap / Toggle Dark Mode) — not
  requested; would need their own design discussion.
- Submenu under `Edit Plugin Config` for Core TOML / Active Theme —
  promote later if asked. One item, opens the plugin's main TOML.
- Visual-regression goldens for the menu. The menu is a system popup,
  not part of the Direct2D surface that `screenshot_tool` captures;
  there is no harness to capture a `TrackPopupMenu` snapshot.

## Architecture

A new shared module under `runtime/host/` owns menu mechanics; each
plugin's WndProc adds a `WM_CONTEXTMENU` handler that builds a context
struct, calls into the helper, and dispatches the returned result.

```
right-click / Shift+F10 / menu key
  │
  ▼
ViewWndProc::WM_CONTEXTMENU                    (per-plugin)
  │  build MenuContext from ViewState + click position
  ▼
runtime/host/context_menu::show_context_menu   (shared)
  │  CreatePopupMenu, append items per ctx,
  │  TrackPopupMenu(TPM_RETURNCMD) → command id
  │  map id → MenuResult
  ▼
ViewWndProc::dispatch_menu_result              (per-plugin)
  │  case Copy           → extract_selected_text + copy_to_clipboard
  │  case SelectAll      → set sel_anchor/sel_active to full document
  │  case SearchGoogle   → web_search::search_with_google(selection)
  │  case OpenLink       → existing ShellExecuteW / scroll / reload paths
  │  case CopyLinkAddress→ copy_to_clipboard(link.url)
  │  case CopyCodeBlock  → existing code-block copy path
  │  case EditConfig     → ShellExecuteW("open", plugin TOML path)
  │  case SetLanguage    → vs->force_grammar_id = id; recolorize
  │  case None           → no-op
  ▼
InvalidateRect for state that changed
```

The action code for Copy / Select All / OpenLink / CopyCodeBlock
already exists in the WM_LBUTTONUP / WM_KEYDOWN paths; the dispatcher
calls into shared free functions extracted from those branches.

## New module surface

`src/runtime/host/context_menu.h`:

```cpp
namespace wlx::runtime::host {

struct LinkMenuContext {
    bool         present = false;
    std::wstring url;          // resolved/external form
    bool         external = false;
};

struct CodeBlockMenuContext {
    bool present = false;
    int  block_index = -1;
};

struct LanguageOption {
    std::string  grammar_id;
    std::wstring display_name;
};

// Plugins fill this in before showing the menu.
struct MenuContext {
    bool                          has_selection = false;
    LinkMenuContext               link;
    CodeBlockMenuContext          code_block;
    std::vector<LanguageOption>   languages;          // empty hides submenu
    std::string                   active_grammar_id;  // for checkmark
    bool                          auto_detect_active = false;
    std::wstring                  config_path;        // plugin's TOML; empty hides item
};

// What the user picked. Plugin dispatches.
struct MenuResult {
    enum Kind {
        None, Copy, SelectAll, SearchGoogle,
        OpenLink, CopyLinkAddress, CopyCodeBlock,
        EditConfig, SetLanguage,
    };
    Kind        kind = None;
    std::string language_id;       // for SetLanguage; empty = auto-detect
    int         code_block_index = -1;
};

// Build, TrackPopupMenu(TPM_RETURNCMD), map ID → MenuResult, return.
// screen_pt of {-1,-1} means keyboard-invoked; helper picks anchor.
MenuResult show_context_menu(HWND owner, POINT screen_pt,
                             const MenuContext& ctx);

}  // namespace wlx::runtime::host
```

`src/runtime/host/web_search.h`:

```cpp
namespace wlx::runtime::host {
// Whitespace-collapses, trims, truncates to 1500 wchars,
// percent-encodes UTF-8, ShellExecuteWs the Google search URL.
// No-op on empty/whitespace-only input.
void search_with_google(const std::wstring& query);

// Pure helper exposed for unit testing.
std::wstring build_google_search_url(const std::wstring& query);
}  // namespace wlx::runtime::host
```

`src/runtime/host/grammar_menu.h` (colorizer-only consumer):

```cpp
namespace wlx::runtime::host {
// Enumerates grammars from the colorizer ABI, attaches display names
// from the constexpr id→name table, sorts alphabetically by display.
std::vector<LanguageOption> available_grammars(WlxCore* core);

// Pure mapping helper, exposed for tests. Unknown ids fall back to
// capitalized id ("foobar" → "Foobar") so future grammars don't crash.
std::wstring grammar_display_name(std::string_view grammar_id);
}  // namespace wlx::runtime::host
```

The display-name table is a small `constexpr` array in
`grammar_menu.cpp` — current grammar set is ~20 entries.

### Why MenuResult instead of callbacks

`TrackPopupMenu(TPM_RETURNCMD)` is synchronous; the helper builds and
shows the menu, the user picks, the helper returns. A `MenuResult`
return keeps the helper free of plugin types (`ViewState`, `LinkTarget`,
`LayoutBlock`) and avoids `std::function` plumbing. Plugins already own
all the action code — they just dispatch.

### Plugin-side touches

- `src/plugin_md/window/host_adapter.cpp`:
  - `WM_CONTEXTMENU` case
  - `build_menu_context_md(ViewState&, float doc_x, float doc_y)`
  - `dispatch_menu_result(ViewState&, MenuResult)`
- `src/plugin_colorizer/window/colorizer_host_adapter.cpp`:
  - same shape
  - new `force_grammar_id` field on the colorizer's `ViewState`
    (empty = auto-detect)
  - the existing extension-based grammar lookup gets one new branch:
    "if `force_grammar_id` is non-empty, use it instead"

The bodies of the existing WM_LBUTTONUP / WM_KEYDOWN action branches
(copy selection, select-all, open link, copy code block) get extracted
into small free functions in `runtime/host/selection_helpers.h` (or a
new `view_actions.h` if it grows past a couple of fns) so both the
mouse path and the menu dispatcher call into one place.

## Menu structure

### Markdown plugin

```
┌──────────────────────────────────────────────┐
│  Copy                              Ctrl+C    │  ← greyed if no selection
│  Select All                        Ctrl+A    │
│  ──────────────────────────────────────────  │
│  Search with Google                          │  ← greyed if no selection
│  ──────────────────────────────────────────  │
│  Open Link                                   │  ← shown only on a link
│  Copy Link Address                           │  ← shown only on a link
│  ──────────────────────────────────────────  │
│  Copy Code Block                             │  ← shown only on a fenced code block
│  ──────────────────────────────────────────  │
│  Edit Plugin Config                          │
└──────────────────────────────────────────────┘
```

### Colorizer plugin

```
┌──────────────────────────────────────────────┐
│  Copy                              Ctrl+C    │
│  Select All                        Ctrl+A    │
│  ──────────────────────────────────────────  │
│  Search with Google                          │
│  ──────────────────────────────────────────  │
│  Edit Plugin Config                          │
│  ──────────────────────────────────────────  │
│  Force Language                          ▸   │
└──────────────────────────────────────────────┘

  Force Language submenu:

  ┌────────────────────────┐
  │  ✓ Auto-detect         │   ← checkmark when no override active
  │  ──────────────────────│
  │     Bash               │
  │     C                  │
  │     C++              ✓ │   ← checkmark on currently active grammar
  │     C#                 │
  │     CMake              │
  │     CSS                │
  │     Dockerfile         │
  │     Git Config         │
  │     Git Rebase         │
  │     Go                 │
  │     HTML               │
  │     Java               │
  │     JavaScript         │
  │     JSON               │
  │     Lua                │
  │     Python             │
  │     Rust               │
  │     TOML               │
  │     YAML               │
  └────────────────────────┘
```

### Grey-out / show-hide rules

| Item                          | Rule                                                              |
|-------------------------------|-------------------------------------------------------------------|
| Copy                          | greyed if `!has_selection`                                        |
| Select All                    | always enabled                                                    |
| Search with Google            | greyed if `!has_selection`                                        |
| Open Link / Copy Link Address | shown only if `link.present`                                      |
| Copy Code Block               | shown only if `code_block.present`                                |
| Edit Plugin Config            | always enabled                                                    |
| Force Language ▸              | shown only if `!languages.empty()`                                |
| Auto-detect (in submenu)      | checkmark + greyed if `auto_detect_active`                        |
| `<grammar>` (in submenu)      | checkmark on `active_grammar_id`                                  |

Separators are emitted by the helper only between non-empty sections —
no double separators, no leading / trailing separators.

### Display names

Constexpr table in `grammar_menu.cpp` maps id → display:
`cpp` → `C++`, `c_sharp` → `C#`, `javascript` → `JavaScript`,
`html` → `HTML`, `css` → `CSS`, `json` → `JSON`, `toml` → `TOML`,
`yaml` → `YAML`, `cmake` → `CMake`, `gitconfig` → `Git Config`,
`git_rebase` → `Git Rebase`, etc. Unknown ids fall back to capitalized
id. Submenu sorts alphabetically by display name.

### Selection on right-click

The menu does not modify the selection. Right-click outside an existing
selection preserves it; the menu operates on whatever is selected.
Matches Notepad / browsers / TC's lister. The standard editable-control
"clear selection on right-click outside" is wrong for a viewer — users
acting on a selection don't want a stray right-click to clobber it.

### Keyboard invocation

`WM_CONTEXTMENU` arrives with `lParam = (-1, -1)` for Shift+F10 / menu
key. Helper anchors at the top-left of the current selection rect on
screen if `has_selection`; window-center otherwise.

## Edge cases

| Case                                                          | Behavior                                                                                       |
|---------------------------------------------------------------|------------------------------------------------------------------------------------------------|
| Right-click during in-progress drag-select                    | Commit the drag (`selecting=false`, `ReleaseCapture`), then show menu                          |
| Right-click inside selection that covers a link               | Selection items operate on selection; link items still appear if `hit_test` reports a link     |
| `WM_CONTEXTMENU` with `lParam == (-1,-1)`                     | Anchor at selection rect top-left, or window-center if no selection                            |
| Search Google selection is whitespace-only after trim          | No-op — don't open a blank search                                                              |
| Search Google selection > 1500 wchars                         | Truncated; no UI feedback                                                                       |
| `Force Language` chosen grammar id missing from registry      | Fall back to plain text, `WLX_TRACE`                                                            |
| Menu opened with no document loaded (`!vs->layout`)           | Return 0 from `WM_CONTEXTMENU` without showing menu                                            |
| User dismisses menu (Esc / click outside)                     | `TrackPopupMenu` returns 0 → `MenuResult{kind=None}` → dispatcher no-ops                       |
| `OpenClipboard` fails inside Copy / CopyLinkAddress / CopyCodeBlock | `WLX_TRACE`, no user-visible error                                                            |
| `ShellExecuteW` fails (no handler / file missing / blocked)   | `WLX_TRACE`, no user-visible error                                                              |

## Error handling

Every action is best-effort. Failures `WLX_TRACE` and are otherwise
silent. This matches the existing helpers: `copy_to_clipboard` returns
a bool already ignored at call sites; link-open via `ShellExecuteW`
ignores its return.

`TrackPopupMenu` failure leaves the user with no menu shown — fine.
The helper traces and returns `MenuResult{kind=None}`.

## Testing

| Layer                     | What                                                                                      | Where                                              |
|---------------------------|-------------------------------------------------------------------------------------------|----------------------------------------------------|
| Unit (doctest)            | `MenuContext`-builder for md: selection / link / code-block detection from synthetic layouts | `tests/runtime/host/test_context_menu_build_md.cpp`        |
| Unit                      | `MenuContext`-builder for colorizer: active grammar, auto-detect flag                     | `tests/runtime/host/test_context_menu_build_colorizer.cpp` |
| Unit                      | `build_google_search_url`: whitespace collapse, trim, truncation, percent-encoding         | `tests/runtime/host/test_web_search.cpp`                   |
| Unit                      | `grammar_display_name`: known ids round-trip; unknown ids capitalize; sort order alphabetical | `tests/runtime/host/test_grammar_menu.cpp`                 |
| Unit                      | Stable command-ID enum: assert no collisions in the ID set                                | folded into `test_context_menu_build_md.cpp`              |
| **Not tested**            | `TrackPopupMenu` itself — Win32 UI, no headless harness                                   | —                                                  |
| **Not tested**            | Visual goldens — menu is a system popup, not on the Direct2D surface                      | —                                                  |
| Manual smoke (markdown)   | Right-click in/out of selection; on a link; on a code block; with no document; via Shift+F10 | —                                                  |
| Manual smoke (colorizer)  | Right-click; pick a force-language; pick Auto-detect; verify checkmarks; reload file → override resets (session-only contract) | —                                                  |

The unit-untestable pieces are the standard cost of touching Win32
menu/shell APIs with no message-pump harness — the codebase already
accepts this for clipboard and `ShellExecuteW` paths. Pure logic
(context building, query normalization, display-name mapping) is fully
unit-tested, and that's most of the new code.

## Open questions

None at design time. Potential follow-ups (out of scope here):

- Per-extension / per-file persisted force-language overrides.
- Per-fenced-code-block force-language in markdown.
- Submenu under `Edit Plugin Config` for Core TOML and active theme.
