# Right-Click Context Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a native Win32 right-click popup menu to both `wlx-listerine-md` and `wlx-listerine-colorizer`, with shared mechanics in `runtime/host/`, plus a session-only Force-Language submenu in the colorizer.

**Architecture:** New `runtime/host/context_menu` module owns menu mechanics (`TrackPopupMenu(TPM_RETURNCMD)` + ID-to-`MenuResult` mapping). New `runtime/host/web_search` and `runtime/host/grammar_menu` helpers. Each plugin's WndProc gains a `WM_CONTEXTMENU` case that builds a `MenuContext`, calls `show_context_menu`, and dispatches the returned `MenuResult` to existing action helpers extracted to `runtime/host/view_actions.h`.

**Tech Stack:** C++20, MSVC, CMake, Conan 2, Win32 (`TrackPopupMenu`, `ShellExecuteW`, `OpenClipboard`), tree-sitter (existing), doctest (tests).

**Spec:** `docs/superpowers/specs/2026-05-07-context-menu-design.md` (commit `03bddbb`).

---

## File map

**New (under `src/`):**

| Path | Responsibility |
|---|---|
| `src/runtime/host/view_actions.h` | Header-only templates: `copy_selection<V>(v, hwnd)`, `select_all<V>(v)`. Used by WM_KEYDOWN and the menu dispatcher. |
| `src/runtime/host/web_search.h` / `.cpp` | Pure `build_google_search_url(query)` + side-effect `search_with_google(query)`. |
| `src/runtime/host/grammar_menu.h` / `.cpp` | Pure `grammar_display_name(id)` + integration `available_grammars(WlxCore*)`. |
| `src/runtime/host/context_menu.h` / `.cpp` | `MenuContext`, `MenuResult`, `build_menu_items_for_test(ctx)`, `show_context_menu(...)`, `build_md_menu_context<V>(...)`, `build_colorizer_menu_context<V>(...)`. |

**New (under `tests/`):**

| Path | Tests for |
|---|---|
| `tests/runtime/host/test_view_actions.cpp` | `select_all`; `copy_selection` early-return paths. |
| `tests/runtime/host/test_web_search.cpp` | `build_google_search_url` whitespace/trim/truncate/percent-encoding. |
| `tests/runtime/host/test_grammar_menu.cpp` | `grammar_display_name` table + capitalize-id fallback. |
| `tests/runtime/host/test_context_menu.cpp` | `build_menu_items_for_test` show/hide/enable/separator rules; `MenuResult::Kind` ID stability; `build_md_menu_context` and `build_colorizer_menu_context` against synthetic views. |
| `tests/core_dll/abi/test_wlx_core_abi.cpp` (modify) | New cases for `wlx_core_list_languages` and bumped `WLX_CORE_ABI_VERSION`. |

**Modified:**

| Path | Change |
|---|---|
| `include/wlx_core/abi.h` | Add `wlx_core_list_languages` / `wlx_core_free_language_list`; bump `WLX_CORE_ABI_VERSION` 1 → 2. |
| `src/core_dll/abi/wlx_core_abi.cpp` | Implement the new ABI functions. |
| `src/core_dll/registry/core_registry.h` / `.cpp` | Expose `available_languages()` (already exists in `Colorizer`, needs forwarding). |
| `src/plugin_md/window/host_adapter.cpp` | `WM_CONTEXTMENU` case + dispatcher; refactor `WM_KEYDOWN` Ctrl+C / Ctrl+A to call view-actions helpers. |
| `src/plugin_colorizer/window/colorizer_host_adapter.cpp` | Same; plus `force_grammar_id` field on `ColorViewState` and one-line override at the colorize call site. |
| `tests/CMakeLists.txt` | Add the four new test files to the `tests` target. |
| `CMakeLists.txt` (top-level) | (No change expected; verify via build.) |

**No changes to:** `listerplugin.h`, `runtime/interaction/`, `runtime/layout/`, `runtime/parser/`, the rendering layer, the search HUD, theme service.

---

## Conventions

- All git commits end with a trailer: `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>`.
- Default branch is **`master`** (never `main`).
- Conventional Commit format: `feat(scope): summary`, `refactor(scope): …`, `test(scope): …`, `build(scope): …`.
- Build/test command (run from project root, PowerShell):
  ```
  cmake --build --preset conan-release
  ./build/Release/tests.exe
  ./build/Release/colorizer-tests.exe
  ```
- A complete TDD step is: write failing test → run and confirm fail → write minimal impl → run and confirm pass → commit.

---

## Task 1: Extract view actions into `runtime/host/view_actions.h`

**Goal:** Pull the existing Ctrl+C / Ctrl+A bodies out of `WM_KEYDOWN` in both plugins into shared header-only templates so the menu dispatcher (Task 6, 8) can call the same code without duplicating logic. Pure refactor — no behavior change.

**Files:**
- Create: `src/runtime/host/view_actions.h`
- Create: `tests/runtime/host/test_view_actions.cpp`
- Modify: `src/plugin_md/window/host_adapter.cpp` (WM_KEYDOWN bodies; lines ~556–576)
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp` (WM_KEYDOWN bodies; lines ~700–719)
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1.1: Add the new test file to the `tests` target**

Edit `tests/CMakeLists.txt`. Insert the new entry alphabetically inside the `tests` target's source list, just after `runtime/host/test_wlx_host_common.cpp`:

```cmake
add_executable(tests
    ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
    runtime/parser/test_document_model.cpp
    runtime/theme/test_theme_service.cpp
    runtime/io/test_file_service.cpp
    runtime/parser/test_markdown_parser.cpp
    runtime/cache/test_cache_service.cpp
    runtime/layout/test_layout_engine.cpp
    runtime/interaction/test_text_selection.cpp
    runtime/search/test_search_engine.cpp
    runtime/search/test_search_ops.cpp
    runtime/host/test_wlx_host_common.cpp
    runtime/host/test_view_actions.cpp
    runtime/search/test_search_counter_format.cpp
    runtime/search/test_search_hud_painter.cpp
)
```

- [ ] **Step 1.2: Write the failing test**

Create `tests/runtime/host/test_view_actions.cpp`:

```cpp
#include <doctest/doctest.h>
#include "runtime/host/view_actions.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/layout_block.h"

#include <memory>
#include <windows.h>

using namespace wlx::runtime::host;
using namespace wlx::runtime::layout;

namespace {

// Minimal fake view satisfying the Selectable concept the new helpers expect.
struct FakeView {
    std::shared_ptr<LayoutDocument> layout;
    TextPosition sel_anchor;
    TextPosition sel_active;
    bool selecting = false;
};

static std::shared_ptr<LayoutDocument> make_layout_with_blocks(
    std::vector<std::wstring> block_texts) {
    auto layout = std::make_shared<LayoutDocument>();
    for (auto& t : block_texts) {
        LayoutBlock b;
        LayoutTextRun run;
        run.text = t;
        b.text_runs.push_back(run);
        layout->blocks.push_back(std::move(b));
    }
    return layout;
}

}  // namespace

TEST_CASE("view_actions::select_all on empty layout returns false") {
    FakeView v;
    v.layout = std::make_shared<LayoutDocument>();
    CHECK(select_all(v) == false);
    CHECK(v.sel_anchor.valid() == false);
}

TEST_CASE("view_actions::select_all on null layout returns false") {
    FakeView v;
    CHECK(select_all(v) == false);
}

TEST_CASE("view_actions::select_all spans all blocks") {
    FakeView v;
    v.layout = make_layout_with_blocks({L"hello", L"world!!"});
    REQUIRE(select_all(v));
    CHECK(v.sel_anchor.block_index == 0);
    CHECK(v.sel_anchor.char_offset == 0);
    CHECK(v.sel_active.block_index == 1);
    CHECK(v.sel_active.char_offset == 7);
    CHECK(v.selecting == false);
}

TEST_CASE("view_actions::copy_selection returns false when no selection") {
    FakeView v;
    v.layout = make_layout_with_blocks({L"hello"});
    // sel_anchor == sel_active (both default-constructed)
    CHECK(copy_selection(v, /*hwnd*/ nullptr) == false);
}

TEST_CASE("view_actions::copy_selection returns false when no layout") {
    FakeView v;
    v.sel_anchor = TextPosition{0, 0};
    v.sel_active = TextPosition{0, 5};
    CHECK(copy_selection(v, /*hwnd*/ nullptr) == false);
}
```

- [ ] **Step 1.3: Run test to verify it fails (header missing)**

```
cmake --build --preset conan-release
```

Expected: compile error — `runtime/host/view_actions.h` not found.

- [ ] **Step 1.4: Write the minimal header**

Create `src/runtime/host/view_actions.h`:

```cpp
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/host/clipboard.h"
#include "runtime/host/hit_test.h"
#include "runtime/host/selection_helpers.h"
#include "runtime/interaction/text_selection.h"
#include "runtime/layout/text_position.h"

#include <algorithm>
#include <windows.h>

namespace wlx::runtime::host {

// Copy the current selection to the clipboard. Returns true if anything
// was copied; false if there is no selection or no layout.
template <Selectable V>
bool copy_selection(V& v, HWND hwnd) {
    if (!v.layout) return false;
    if (!v.sel_anchor.valid()) return false;
    if (v.sel_anchor == v.sel_active) return false;
    auto lo = std::min(v.sel_anchor, v.sel_active);
    auto hi = std::max(v.sel_anchor, v.sel_active);
    auto text = wlx::runtime::interaction::extract_selected_text(*v.layout, lo, hi);
    return copy_to_clipboard(hwnd, text);
}

// Set sel_anchor/sel_active to span the entire document. Returns true if
// the selection was changed; false if the layout is null or empty.
template <Selectable V>
bool select_all(V& v) {
    if (!v.layout || v.layout->blocks.empty()) return false;
    v.sel_anchor = wlx::runtime::layout::TextPosition{0, 0};
    int last = static_cast<int>(v.layout->blocks.size()) - 1;
    v.sel_active = wlx::runtime::layout::TextPosition{
        last, block_text_length(v.layout->blocks[last])};
    v.selecting = false;
    return true;
}

}  // namespace wlx::runtime::host
```

- [ ] **Step 1.5: Run tests to verify they pass**

```
cmake --build --preset conan-release
./build/Release/tests.exe -tc='view_actions*'
```

Expected: 5 cases pass.

- [ ] **Step 1.6: Refactor md plugin's WM_KEYDOWN to use the helpers**

In `src/plugin_md/window/host_adapter.cpp`, locate the WM_KEYDOWN block (search for `// Ctrl+C — copy selection`, around line 557). Replace the `Ctrl+C` and `Ctrl+A` blocks with calls to the new helpers:

```cpp
        // Ctrl+C — copy selection
        if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            wlx::runtime::host::copy_selection(*vs, hwnd);
            handled = true;
        }
        // Ctrl+A — select all
        else if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (wlx::runtime::host::select_all(*vs))
                InvalidateRect(hwnd, nullptr, FALSE);
            handled = true;
        }
```

Add at the top of the file with the other host includes:

```cpp
#include "runtime/host/view_actions.h"
```

(Order alphabetically next to `runtime/host/selection_helpers.h`.)

- [ ] **Step 1.7: Refactor colorizer plugin's WM_KEYDOWN identically**

In `src/plugin_colorizer/window/colorizer_host_adapter.cpp`, locate the WM_KEYDOWN block (around line 700). Replace the `Ctrl+C` and `Ctrl+A` branches with the same helper-based blocks (verbatim — both plugins use the same `vs` member names):

```cpp
        // Ctrl+C — copy selection
        if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            wlx::runtime::host::copy_selection(*vs, hwnd);
            handled = true;
        }
        // Ctrl+A — select all
        else if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (wlx::runtime::host::select_all(*vs))
                InvalidateRect(hwnd, nullptr, FALSE);
            handled = true;
        }
```

Add the include:

```cpp
#include "runtime/host/view_actions.h"
```

- [ ] **Step 1.8: Build and run all tests to verify no regression**

```
cmake --build --preset conan-release
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

Expected: all existing tests still pass; the 5 new `view_actions*` cases pass; both `tests.exe` and `colorizer-tests.exe` link successfully.

- [ ] **Step 1.9: Commit**

```
git add src/runtime/host/view_actions.h tests/runtime/host/test_view_actions.cpp tests/CMakeLists.txt src/plugin_md/window/host_adapter.cpp src/plugin_colorizer/window/colorizer_host_adapter.cpp
git commit -m "refactor(host): extract Ctrl+C/Ctrl+A bodies into view_actions.h

Pull the copy-selection and select-all logic out of both plugins'
WM_KEYDOWN handlers into header-only templates in runtime/host. Pure
refactor — same behavior — readying these branches for reuse by the
upcoming context-menu dispatcher.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: `web_search` module

**Goal:** Pure `build_google_search_url(query)` (whitespace-collapse, trim, truncate to 1500 wchars, UTF-8 percent-encode) + side-effect `search_with_google(query)` that `ShellExecuteW`s the URL.

**Files:**
- Create: `src/runtime/host/web_search.h`
- Create: `src/runtime/host/web_search.cpp`
- Create: `tests/runtime/host/test_web_search.cpp`
- Modify: `src/CMakeLists.txt` (or wherever `runtime` library sources are listed)
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 2.1: Locate and update the runtime library source list**

Search the project for where `clipboard.cpp` is listed in CMake (this is the same build artifact `web_search.cpp` must join):

```
grep -rn "clipboard.cpp" --include=CMakeLists.txt .
```

Expected: a single hit in `src/CMakeLists.txt` (or the runtime sub-CMakeLists). In whichever file lists `runtime/host/clipboard.cpp`, append `runtime/host/web_search.cpp` immediately after it. Example shape (your file may differ):

```cmake
set(RUNTIME_HOST_SRC
    runtime/host/clipboard.cpp
    runtime/host/dark_mode.cpp
    runtime/host/factories.cpp
    runtime/host/hit_test.cpp
    runtime/host/module_path.cpp
    runtime/host/web_search.cpp
    runtime/host/window_class.cpp
)
```

- [ ] **Step 2.2: Add the test file to the `tests` target**

In `tests/CMakeLists.txt`, append `runtime/host/test_web_search.cpp` to the `tests` source list (alphabetical position alongside the other `runtime/host/test_*` entries).

- [ ] **Step 2.3: Write the failing test**

Create `tests/runtime/host/test_web_search.cpp`:

```cpp
#include <doctest/doctest.h>
#include "runtime/host/web_search.h"

using namespace wlx::runtime::host;

TEST_CASE("build_google_search_url empty input returns empty") {
    CHECK(build_google_search_url(L"").empty());
}

TEST_CASE("build_google_search_url whitespace-only returns empty") {
    CHECK(build_google_search_url(L"   \t\r\n  ").empty());
}

TEST_CASE("build_google_search_url plain ASCII") {
    auto url = build_google_search_url(L"hello world");
    CHECK(url == L"https://www.google.com/search?q=hello%20world");
}

TEST_CASE("build_google_search_url collapses internal whitespace runs") {
    auto url = build_google_search_url(L"foo   bar\n\tbaz");
    CHECK(url == L"https://www.google.com/search?q=foo%20bar%20baz");
}

TEST_CASE("build_google_search_url trims leading/trailing whitespace") {
    auto url = build_google_search_url(L"  hello  ");
    CHECK(url == L"https://www.google.com/search?q=hello");
}

TEST_CASE("build_google_search_url percent-encodes reserved chars") {
    // ?, #, &, =, +, /, : must be encoded
    auto url = build_google_search_url(L"a?b#c&d=e+f/g:h");
    CHECK(url == L"https://www.google.com/search?q=a%3Fb%23c%26d%3De%2Bf%2Fg%3Ah");
}

TEST_CASE("build_google_search_url unreserved chars stay literal") {
    // RFC 3986 unreserved: A-Z a-z 0-9 - . _ ~
    auto url = build_google_search_url(L"abcXYZ-._~012");
    CHECK(url == L"https://www.google.com/search?q=abcXYZ-._~012");
}

TEST_CASE("build_google_search_url percent-encodes UTF-8 bytes") {
    // U+00E9 'é' = UTF-8 0xC3 0xA9
    auto url = build_google_search_url(L"café");
    CHECK(url == L"https://www.google.com/search?q=caf%C3%A9");
}

TEST_CASE("build_google_search_url percent-encodes CJK") {
    // U+4E2D '中' = UTF-8 0xE4 0xB8 0xAD
    auto url = build_google_search_url(L"中");
    CHECK(url == L"https://www.google.com/search?q=%E4%B8%AD");
}

TEST_CASE("build_google_search_url truncates over 1500 wchars before encoding") {
    std::wstring long_query(2000, L'a');
    auto url = build_google_search_url(long_query);
    // Prefix + exactly 1500 'a' characters
    std::wstring expected = L"https://www.google.com/search?q=" + std::wstring(1500, L'a');
    CHECK(url == expected);
}
```

- [ ] **Step 2.4: Run tests to verify they fail (header missing)**

```
cmake --build --preset conan-release
```

Expected: compile error — `runtime/host/web_search.h` not found.

- [ ] **Step 2.5: Write the header**

Create `src/runtime/host/web_search.h`:

```cpp
#pragma once

#include <string>
#include <string_view>

namespace wlx::runtime::host {

// Pure: collapses internal whitespace runs to a single space, trims
// leading/trailing whitespace, truncates to 1500 wchars, percent-encodes
// the UTF-8 bytes per RFC 3986 (unreserved set: A-Z a-z 0-9 - . _ ~),
// and prepends the Google search prefix. Returns an empty string for
// empty / whitespace-only input.
std::wstring build_google_search_url(std::wstring_view query);

// Side-effect: builds the URL via build_google_search_url and ShellExecuteWs
// it. No-op on empty result. Failures (no shell handler) trace via
// WLX_TRACE; no user-visible error.
void search_with_google(std::wstring_view query);

}  // namespace wlx::runtime::host
```

- [ ] **Step 2.6: Write the implementation**

Create `src/runtime/host/web_search.cpp`:

```cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/host/web_search.h"

#include <shellapi.h>
#include <windows.h>

#include <cwctype>
#include <string>

#define WLX_TRACE_TAG L"wlx-host"
#include "runtime/diagnostics/wlx_trace.h"

namespace wlx::runtime::host {

namespace {

constexpr size_t kMaxQueryWchars = 1500;
constexpr wchar_t kPrefix[] = L"https://www.google.com/search?q=";

// RFC 3986 unreserved set; everything else is percent-encoded.
bool is_unreserved(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ||
           (c >= L'a' && c <= L'z') ||
           (c >= L'0' && c <= L'9') ||
           c == L'-' || c == L'.' || c == L'_' || c == L'~';
}

std::wstring normalize_whitespace(std::wstring_view in) {
    std::wstring out;
    out.reserve(in.size());
    bool prev_space = true;  // start as true so leading whitespace is dropped
    for (wchar_t c : in) {
        if (iswspace(static_cast<wint_t>(c))) {
            if (!prev_space) {
                out.push_back(L' ');
                prev_space = true;
            }
        } else {
            out.push_back(c);
            prev_space = false;
        }
    }
    if (!out.empty() && out.back() == L' ')
        out.pop_back();
    return out;
}

std::wstring percent_encode_utf8(std::wstring_view in) {
    // Convert UTF-16 input to UTF-8, then percent-encode per byte.
    int needed = WideCharToMultiByte(CP_UTF8, 0, in.data(),
                                     static_cast<int>(in.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string utf8(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, in.data(), static_cast<int>(in.size()),
                        utf8.data(), needed, nullptr, nullptr);

    static constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(utf8.size() * 3);
    for (unsigned char b : utf8) {
        // Reapply unreserved test on the byte value (ASCII subset).
        if (is_unreserved(static_cast<wchar_t>(b))) {
            out.push_back(static_cast<wchar_t>(b));
        } else {
            out.push_back(L'%');
            out.push_back(hex[(b >> 4) & 0xF]);
            out.push_back(hex[b & 0xF]);
        }
    }
    return out;
}

}  // namespace

std::wstring build_google_search_url(std::wstring_view query) {
    std::wstring normalized = normalize_whitespace(query);
    if (normalized.empty()) return {};
    if (normalized.size() > kMaxQueryWchars)
        normalized.resize(kMaxQueryWchars);
    return std::wstring(kPrefix) + percent_encode_utf8(normalized);
}

void search_with_google(std::wstring_view query) {
    auto url = build_google_search_url(query);
    if (url.empty()) return;
    HINSTANCE hi = ShellExecuteW(nullptr, L"open", url.c_str(),
                                 nullptr, nullptr, SW_SHOW);
    if (reinterpret_cast<INT_PTR>(hi) <= 32) {
        WLX_TRACE(L"search_with_google: ShellExecuteW failed (%p)", hi);
    }
}

}  // namespace wlx::runtime::host
```

- [ ] **Step 2.7: Build and run tests**

```
cmake --build --preset conan-release
./build/Release/tests.exe -tc='build_google_search_url*'
```

Expected: 9 cases pass.

- [ ] **Step 2.8: Commit**

```
git add src/runtime/host/web_search.h src/runtime/host/web_search.cpp tests/runtime/host/test_web_search.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(host): add web_search module for 'Search with Google'

Pure build_google_search_url(query) handles whitespace collapsing,
trimming, 1500-wchar truncation, and RFC 3986 percent-encoding of
UTF-8 bytes. Side-effect search_with_google ShellExecuteWs the URL,
tracing (no popup) on failure. Used by the upcoming context menu.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: ABI: add `wlx_core_list_languages` (bumps ABI version 1 → 2)

**Goal:** Expose the existing `Colorizer::available_languages()` through the C ABI so the colorizer plugin can populate the Force-Language submenu. Bumps `WLX_CORE_ABI_VERSION` because we're adding a required symbol; ABI consumers built against v1 will return null from `acquire_compatible()` against a v2 core, and that's fine — both ship together.

**Files:**
- Modify: `include/wlx_core/abi.h`
- Modify: `src/core_dll/registry/core_registry.h`
- Modify: `src/core_dll/registry/core_registry.cpp`
- Modify: `src/core_dll/abi/wlx_core_abi.cpp`
- Modify: `tests/core_dll/abi/test_wlx_core_abi.cpp`

- [ ] **Step 3.1: Write the failing ABI test**

Append to `tests/core_dll/abi/test_wlx_core_abi.cpp` (open the file and add these cases at the end of the existing test list):

```cpp
TEST_CASE("ABI version is 2") {
    CHECK(wlx_core_abi_version() == 2);
}

TEST_CASE("wlx_core_list_languages returns a non-empty list including cpp") {
    WlxCore* core = wlx_core::acquire_compatible();
    REQUIRE(core != nullptr);

    WlxLanguageList list{};
    REQUIRE(wlx_core_list_languages(core, &list) == 0);
    REQUIRE(list.count > 0);
    REQUIRE(list.ids != nullptr);

    bool saw_cpp = false;
    for (uint32_t i = 0; i < list.count; i++) {
        REQUIRE(list.ids[i] != nullptr);
        if (std::string(list.ids[i]) == "cpp") { saw_cpp = true; break; }
    }
    CHECK(saw_cpp);

    wlx_core_free_language_list(&list);
    CHECK(list.ids == nullptr);
    CHECK(list.count == 0);
}

TEST_CASE("wlx_core_list_languages returns -1 on null inputs") {
    WlxLanguageList list{};
    CHECK(wlx_core_list_languages(nullptr, &list) < 0);

    WlxCore* core = wlx_core::acquire_compatible();
    REQUIRE(core != nullptr);
    CHECK(wlx_core_list_languages(core, nullptr) < 0);
}
```

- [ ] **Step 3.2: Run colorizer-tests to verify failure**

```
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe -tc='ABI version*'
```

Expected: build error (or test failure) — `WlxLanguageList` undefined and ABI version is still 1.

- [ ] **Step 3.3: Update the ABI header**

Edit `include/wlx_core/abi.h`. Change the version macro:

```cpp
#define WLX_CORE_ABI_VERSION 2
```

In the `extern "C"` block, after `wlx_core_free_spans` (around line 47), insert:

```cpp
typedef struct WlxLanguageList {
    char**   ids;     // array of `count` null-terminated UTF-8 strings
    uint32_t count;
} WlxLanguageList;

// Enumerates the grammars supported by the core. On success, fills *out_list
// with a heap-owned array (must be freed with wlx_core_free_language_list)
// and returns 0. Returns negative on bad arguments or if the core is
// uninitialized.
WLX_CORE_API int  wlx_core_list_languages(WlxCore*, WlxLanguageList* out_list);

// Frees the buffers owned by `*list` and zeros it. Safe to call on an
// already-zeroed list.
WLX_CORE_API void wlx_core_free_language_list(WlxLanguageList* list);
```

- [ ] **Step 3.4: Forward `available_languages()` from CoreRegistry**

Edit `src/core_dll/registry/core_registry.h`. Inside the `CoreRegistry` class, add a public method (alongside `supports`):

```cpp
std::vector<std::string> available_languages();
```

(Add `#include <vector>` and `#include <string>` at the top if not already present.)

Edit `src/core_dll/registry/core_registry.cpp`. Append to the end of the file before the closing namespace brace:

```cpp
std::vector<std::string> CoreRegistry::available_languages() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!colorizer_) return {};
    return colorizer_->available_languages();
}
```

- [ ] **Step 3.5: Implement the ABI functions**

Edit `src/core_dll/abi/wlx_core_abi.cpp`. Append after `wlx_core_theme_color`:

```cpp
extern "C" WLX_CORE_API int
wlx_core_list_languages(WlxCore* h, WlxLanguageList* out_list) {
    if (!h || !out_list) return -1;
    auto& reg = *reinterpret_cast<wlx::core::registry::CoreRegistry*>(h);

    auto langs = reg.available_languages();

    if (langs.empty()) {
        out_list->ids = nullptr;
        out_list->count = 0;
        return 0;
    }

    auto** arr = static_cast<char**>(std::malloc(sizeof(char*) * langs.size()));
    if (!arr) return -2;

    for (size_t i = 0; i < langs.size(); ++i) {
        const auto& s = langs[i];
        arr[i] = static_cast<char*>(std::malloc(s.size() + 1));
        if (!arr[i]) {
            // Roll back: free what we've allocated so far.
            for (size_t j = 0; j < i; ++j) std::free(arr[j]);
            std::free(arr);
            return -2;
        }
        std::memcpy(arr[i], s.data(), s.size());
        arr[i][s.size()] = '\0';
    }

    out_list->ids = arr;
    out_list->count = static_cast<uint32_t>(langs.size());
    return 0;
}

extern "C" WLX_CORE_API void
wlx_core_free_language_list(WlxLanguageList* list) {
    if (!list) return;
    if (list->ids) {
        for (uint32_t i = 0; i < list->count; ++i)
            std::free(list->ids[i]);
        std::free(list->ids);
    }
    list->ids = nullptr;
    list->count = 0;
}
```

- [ ] **Step 3.6: Run colorizer-tests to verify they pass**

```
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe -tc='ABI version*,wlx_core_list_languages*'
```

Expected: all 3 new cases pass; existing ABI tests still pass.

- [ ] **Step 3.7: Run the full test suite to catch ABI version regressions**

```
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

Expected: every test passes. Both plugins compiled against the bumped header still acquire the core successfully because they ship together.

- [ ] **Step 3.8: Commit**

```
git add include/wlx_core/abi.h src/core_dll/registry/core_registry.h src/core_dll/registry/core_registry.cpp src/core_dll/abi/wlx_core_abi.cpp tests/core_dll/abi/test_wlx_core_abi.cpp
git commit -m "feat(core-abi): add wlx_core_list_languages, bump ABI to v2

Expose the existing Colorizer::available_languages() through the C ABI
as a heap-owned char** array with paired wlx_core_free_language_list.
Bumps WLX_CORE_ABI_VERSION 1 → 2 — the .wlx64s and core DLL ship
lockstep so the bump does not break consumers in the field.

Used by the upcoming Force-Language submenu in the colorizer plugin.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: `grammar_menu` module

**Goal:** Pure `grammar_display_name(id)` mapping (with capitalize-id fallback for unknowns) + `available_grammars(WlxCore*)` enumerator that calls `wlx_core_list_languages` and pairs each id with its display name, sorted alphabetically.

**Files:**
- Create: `src/runtime/host/grammar_menu.h`
- Create: `src/runtime/host/grammar_menu.cpp`
- Create: `tests/runtime/host/test_grammar_menu.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 4.1: Add module sources to build**

In `src/CMakeLists.txt`, append `runtime/host/grammar_menu.cpp` next to `runtime/host/web_search.cpp`. In `tests/CMakeLists.txt`, append `runtime/host/test_grammar_menu.cpp` to the `tests` target source list.

- [ ] **Step 4.2: Write the failing test**

Create `tests/runtime/host/test_grammar_menu.cpp`:

```cpp
#include <doctest/doctest.h>
#include "runtime/host/grammar_menu.h"

using namespace wlx::runtime::host;

TEST_CASE("grammar_display_name maps known ids to display names") {
    CHECK(grammar_display_name("cpp")        == L"C++");
    CHECK(grammar_display_name("c")          == L"C");
    CHECK(grammar_display_name("c-sharp")    == L"C#");
    CHECK(grammar_display_name("javascript") == L"JavaScript");
    CHECK(grammar_display_name("typescript") == L"TypeScript");
    CHECK(grammar_display_name("html")       == L"HTML");
    CHECK(grammar_display_name("css")        == L"CSS");
    CHECK(grammar_display_name("json")       == L"JSON");
    CHECK(grammar_display_name("toml")       == L"TOML");
    CHECK(grammar_display_name("yaml")       == L"YAML");
    CHECK(grammar_display_name("cmake")      == L"CMake");
    CHECK(grammar_display_name("git-config") == L"Git Config");
    CHECK(grammar_display_name("git_rebase") == L"Git Rebase");
    CHECK(grammar_display_name("dockerfile") == L"Dockerfile");
    CHECK(grammar_display_name("bash")       == L"Bash");
    CHECK(grammar_display_name("python")     == L"Python");
    CHECK(grammar_display_name("rust")       == L"Rust");
    CHECK(grammar_display_name("go")         == L"Go");
    CHECK(grammar_display_name("java")       == L"Java");
    CHECK(grammar_display_name("lua")        == L"Lua");
}

TEST_CASE("grammar_display_name capitalizes unknown ids as fallback") {
    CHECK(grammar_display_name("zigzag") == L"Zigzag");
    CHECK(grammar_display_name("FOO")    == L"Foo");
    CHECK(grammar_display_name("a")      == L"A");
}

TEST_CASE("grammar_display_name returns empty for empty id") {
    CHECK(grammar_display_name("").empty());
}
```

- [ ] **Step 4.3: Run tests to verify failure**

```
cmake --build --preset conan-release
```

Expected: compile error — header not found.

- [ ] **Step 4.4: Write the header**

Create `src/runtime/host/grammar_menu.h`:

```cpp
#pragma once

#include "wlx_core/abi.h"

#include <string>
#include <string_view>
#include <vector>

namespace wlx::runtime::host {

struct LanguageOption {
    std::string  grammar_id;
    std::wstring display_name;
};

// Pure mapping. Known grammar ids return their human-readable display
// (e.g., "cpp" → "C++"). Unknown ids fall back to capitalized id
// ("foobar" → "Foobar"). Empty input returns empty.
std::wstring grammar_display_name(std::string_view grammar_id);

// Enumerates grammars from the core ABI, attaches display names, sorts
// case-insensitively by display_name. Returns an empty vector if `core`
// is null or the ABI call fails.
std::vector<LanguageOption> available_grammars(WlxCore* core);

}  // namespace wlx::runtime::host
```

- [ ] **Step 4.5: Write the implementation**

Create `src/runtime/host/grammar_menu.cpp`:

```cpp
#include "runtime/host/grammar_menu.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <unordered_map>

namespace wlx::runtime::host {

namespace {

// Display-name table. Keep alphabetical by id for human readability.
struct DisplayEntry { const char* id; const wchar_t* display; };
constexpr DisplayEntry kDisplayTable[] = {
    {"bash",           L"Bash"},
    {"c",              L"C"},
    {"c-sharp",        L"C#"},
    {"cmake",          L"CMake"},
    {"cpp",            L"C++"},
    {"css",            L"CSS"},
    {"dockerfile",     L"Dockerfile"},
    {"git-config",     L"Git Config"},
    {"git_rebase",     L"Git Rebase"},
    {"gitattributes",  L"Git Attributes"},
    {"gitignore",      L"Git Ignore"},
    {"go",             L"Go"},
    {"html",           L"HTML"},
    {"java",           L"Java"},
    {"javascript",     L"JavaScript"},
    {"json",           L"JSON"},
    {"lua",            L"Lua"},
    {"php",            L"PHP"},
    {"powershell",     L"PowerShell"},
    {"python",         L"Python"},
    {"rust",           L"Rust"},
    {"sql",            L"SQL"},
    {"toml",           L"TOML"},
    {"typescript",     L"TypeScript"},
    {"vim",            L"Vim"},
    {"xml",            L"XML"},
    {"yaml",           L"YAML"},
};

std::wstring capitalize_ascii(std::string_view in) {
    std::wstring out;
    out.reserve(in.size());
    bool first = true;
    for (char c : in) {
        wchar_t w = static_cast<wchar_t>(static_cast<unsigned char>(c));
        if (first) {
            if (w >= L'a' && w <= L'z') w = static_cast<wchar_t>(w - 32);
            first = false;
        } else {
            if (w >= L'A' && w <= L'Z') w = static_cast<wchar_t>(w + 32);
        }
        out.push_back(w);
    }
    return out;
}

}  // namespace

std::wstring grammar_display_name(std::string_view grammar_id) {
    if (grammar_id.empty()) return {};
    for (const auto& e : kDisplayTable) {
        if (grammar_id == e.id) return e.display;
    }
    return capitalize_ascii(grammar_id);
}

std::vector<LanguageOption> available_grammars(WlxCore* core) {
    if (!core) return {};

    WlxLanguageList list{};
    if (wlx_core_list_languages(core, &list) != 0 || list.count == 0) {
        wlx_core_free_language_list(&list);
        return {};
    }

    std::vector<LanguageOption> out;
    out.reserve(list.count);
    for (uint32_t i = 0; i < list.count; ++i) {
        if (!list.ids[i]) continue;
        LanguageOption opt;
        opt.grammar_id   = list.ids[i];
        opt.display_name = grammar_display_name(opt.grammar_id);
        out.push_back(std::move(opt));
    }
    wlx_core_free_language_list(&list);

    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        // Case-insensitive lexicographic compare on display_name.
        const auto& x = a.display_name;
        const auto& y = b.display_name;
        size_t n = std::min(x.size(), y.size());
        for (size_t i = 0; i < n; ++i) {
            wchar_t cx = static_cast<wchar_t>(towlower(static_cast<wint_t>(x[i])));
            wchar_t cy = static_cast<wchar_t>(towlower(static_cast<wint_t>(y[i])));
            if (cx != cy) return cx < cy;
        }
        return x.size() < y.size();
    });

    return out;
}

}  // namespace wlx::runtime::host
```

- [ ] **Step 4.6: Run tests**

```
cmake --build --preset conan-release
./build/Release/tests.exe -tc='grammar_display_name*'
```

Expected: 3 cases pass.

- [ ] **Step 4.7: Commit**

```
git add src/runtime/host/grammar_menu.h src/runtime/host/grammar_menu.cpp tests/runtime/host/test_grammar_menu.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(host): add grammar_menu module

Pure grammar_display_name(id) maps known grammar ids to human-readable
display names (cpp → C++, c-sharp → C#, …) with a capitalize-id
fallback for unknowns. Integration available_grammars(WlxCore*) calls
wlx_core_list_languages, pairs each id with its display, and sorts
case-insensitively. Used by the upcoming Force-Language submenu.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: `context_menu` module

**Goal:** Define `MenuContext`, `MenuResult`, `build_menu_items_for_test` (pure show/hide/enable logic), `show_context_menu` (TrackPopupMenu wrapper), and the per-plugin context-builder templates `build_md_menu_context<V>` / `build_colorizer_menu_context<V>`. Unit-test the pure logic; the TrackPopupMenu call itself is manual-smoke-only.

**Files:**
- Create: `src/runtime/host/context_menu.h`
- Create: `src/runtime/host/context_menu.cpp`
- Create: `tests/runtime/host/test_context_menu.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 5.1: Add module sources to build**

In `src/CMakeLists.txt`, append `runtime/host/context_menu.cpp` next to `runtime/host/grammar_menu.cpp`. In `tests/CMakeLists.txt`, append `runtime/host/test_context_menu.cpp`.

- [ ] **Step 5.2: Write the failing test**

Create `tests/runtime/host/test_context_menu.cpp`:

```cpp
#include <doctest/doctest.h>
#include "runtime/host/context_menu.h"
#include "runtime/interaction/interaction_engine.h"
#include "runtime/layout/layout_block.h"
#include "runtime/layout/layout_document.h"
#include "runtime/parser/document_model.h"

#include <memory>

using namespace wlx::runtime::host;
using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;

namespace {

// Minimal fake md view for build_md_menu_context.
struct FakeMdView {
    std::shared_ptr<LayoutDocument> layout;
    std::unique_ptr<wlx::runtime::interaction::InteractionEngine> interaction;
    TextPosition sel_anchor;
    TextPosition sel_active;
};

// Minimal fake colorizer view for build_colorizer_menu_context.
struct FakeColorizerView {
    std::shared_ptr<LayoutDocument> layout;
    TextPosition sel_anchor;
    TextPosition sel_active;
    std::string force_grammar_id;
};

}  // namespace

// ---- MenuResult::Kind ID stability ----
TEST_CASE("MenuResult::Kind enum values are stable") {
    // If anyone reorders these, the saved-context-menu wire interpretation
    // breaks. Pin the integer values explicitly.
    CHECK(static_cast<int>(MenuResult::None)             == 0);
    CHECK(static_cast<int>(MenuResult::Copy)             == 1);
    CHECK(static_cast<int>(MenuResult::SelectAll)        == 2);
    CHECK(static_cast<int>(MenuResult::SearchGoogle)     == 3);
    CHECK(static_cast<int>(MenuResult::OpenLink)         == 4);
    CHECK(static_cast<int>(MenuResult::CopyLinkAddress)  == 5);
    CHECK(static_cast<int>(MenuResult::CopyCodeBlock)    == 6);
    CHECK(static_cast<int>(MenuResult::EditConfig)       == 7);
    CHECK(static_cast<int>(MenuResult::SetLanguage)      == 8);
}

// ---- build_menu_items_for_test: show/hide/enable rules ----
TEST_CASE("build_menu_items: empty context shows the always-on items") {
    MenuContext ctx;
    auto items = build_menu_items_for_test(ctx);

    // Expect: Copy(disabled), SelectAll, separator, SearchGoogle(disabled).
    REQUIRE(items.size() == 4);
    CHECK(items[0].kind == MenuItemKind::Copy);
    CHECK(items[0].enabled == false);
    CHECK(items[1].kind == MenuItemKind::SelectAll);
    CHECK(items[1].enabled == true);
    CHECK(items[2].kind == MenuItemKind::Separator);
    CHECK(items[3].kind == MenuItemKind::SearchGoogle);
    CHECK(items[3].enabled == false);
}

TEST_CASE("build_menu_items: with selection enables Copy and SearchGoogle") {
    MenuContext ctx;
    ctx.has_selection = true;
    auto items = build_menu_items_for_test(ctx);
    REQUIRE(items.size() == 4);
    CHECK(items[0].kind == MenuItemKind::Copy);
    CHECK(items[0].enabled == true);
    CHECK(items[3].kind == MenuItemKind::SearchGoogle);
    CHECK(items[3].enabled == true);
}

TEST_CASE("build_menu_items: link adds OpenLink + CopyLinkAddress under separator") {
    MenuContext ctx;
    ctx.link.present = true;
    ctx.link.url     = L"https://example.com";
    auto items = build_menu_items_for_test(ctx);
    // Copy, SelectAll, Sep, SearchGoogle, Sep, OpenLink, CopyLinkAddress
    REQUIRE(items.size() == 7);
    CHECK(items[4].kind == MenuItemKind::Separator);
    CHECK(items[5].kind == MenuItemKind::OpenLink);
    CHECK(items[6].kind == MenuItemKind::CopyLinkAddress);
}

TEST_CASE("build_menu_items: code block adds CopyCodeBlock under separator") {
    MenuContext ctx;
    ctx.code_block.present     = true;
    ctx.code_block.block_index = 3;
    auto items = build_menu_items_for_test(ctx);
    // Copy, SelectAll, Sep, SearchGoogle, Sep, CopyCodeBlock
    REQUIRE(items.size() == 6);
    CHECK(items[4].kind == MenuItemKind::Separator);
    CHECK(items[5].kind == MenuItemKind::CopyCodeBlock);
}

TEST_CASE("build_menu_items: config_path adds EditConfig under separator") {
    MenuContext ctx;
    ctx.config_path = L"C:\\path\\to\\plugin.toml";
    auto items = build_menu_items_for_test(ctx);
    // Copy, SelectAll, Sep, SearchGoogle, Sep, EditConfig
    REQUIRE(items.size() == 6);
    CHECK(items[4].kind == MenuItemKind::Separator);
    CHECK(items[5].kind == MenuItemKind::EditConfig);
}

TEST_CASE("build_menu_items: empty config_path hides EditConfig") {
    MenuContext ctx;
    auto items = build_menu_items_for_test(ctx);
    for (const auto& i : items) CHECK(i.kind != MenuItemKind::EditConfig);
}

TEST_CASE("build_menu_items: languages add Force Language root with separator before") {
    MenuContext ctx;
    ctx.config_path = L"C:\\plugin.toml";
    ctx.languages = { {"cpp", L"C++"}, {"python", L"Python"} };
    ctx.active_grammar_id = "cpp";
    auto items = build_menu_items_for_test(ctx);
    // Copy, SelectAll, Sep, SearchGoogle, Sep, EditConfig, Sep, ForceLanguageRoot,
    // (submenu items follow flat; helper composes them inside CreatePopupMenu)
    auto root_it = std::find_if(items.begin(), items.end(),
        [](const MenuItem& i) { return i.kind == MenuItemKind::LanguageSubmenuRoot; });
    REQUIRE(root_it != items.end());
    REQUIRE(root_it != items.begin());
    CHECK((root_it - 1)->kind == MenuItemKind::Separator);
}

TEST_CASE("build_menu_items: never emits leading or trailing separators") {
    MenuContext ctx;
    auto items = build_menu_items_for_test(ctx);
    REQUIRE(!items.empty());
    CHECK(items.front().kind != MenuItemKind::Separator);
    CHECK(items.back().kind != MenuItemKind::Separator);
}

TEST_CASE("build_menu_items: never emits two consecutive separators") {
    // Empty context except SearchGoogle path — no extras between separators
    // would still be valid, but we want NO two-in-a-row anywhere.
    MenuContext ctx;
    ctx.has_selection = true;
    ctx.link.present  = true;
    ctx.code_block.present = true;
    ctx.config_path = L"x";
    ctx.languages = { {"cpp", L"C++"} };
    auto items = build_menu_items_for_test(ctx);
    for (size_t i = 1; i < items.size(); ++i) {
        bool both = items[i - 1].kind == MenuItemKind::Separator
                 && items[i].kind     == MenuItemKind::Separator;
        CHECK(both == false);
    }
}

// ---- build_md_menu_context against a synthetic FakeMdView ----
TEST_CASE("build_md_menu_context: empty layout yields empty context") {
    FakeMdView vs;
    auto ctx = build_md_menu_context(vs, 0.0f, 0.0f);
    CHECK(ctx.has_selection == false);
    CHECK(ctx.link.present == false);
    CHECK(ctx.code_block.present == false);
}

TEST_CASE("build_md_menu_context: detects selection") {
    FakeMdView vs;
    vs.layout = std::make_shared<LayoutDocument>();
    {
        LayoutBlock b;
        LayoutTextRun run;
        run.text = L"hello";
        b.text_runs.push_back(run);
        vs.layout->blocks.push_back(std::move(b));
    }
    vs.sel_anchor = TextPosition{0, 0};
    vs.sel_active = TextPosition{0, 5};
    auto ctx = build_md_menu_context(vs, 0.0f, 0.0f);
    CHECK(ctx.has_selection == true);
}

TEST_CASE("build_md_menu_context: detects code block under cursor") {
    FakeMdView vs;
    vs.layout = std::make_shared<LayoutDocument>();
    {
        LayoutBlock b;
        b.type = BlockType::CodeFence;
        b.rect = D2D1::RectF(0, 0, 100, 50);
        LayoutTextRun run;
        run.text = L"int x;";
        b.text_runs.push_back(run);
        vs.layout->blocks.push_back(std::move(b));
    }
    auto ctx = build_md_menu_context(vs, 10.0f, 10.0f);
    CHECK(ctx.code_block.present == true);
    CHECK(ctx.code_block.block_index == 0);
}

TEST_CASE("build_md_menu_context: cursor outside any code block leaves it absent") {
    FakeMdView vs;
    vs.layout = std::make_shared<LayoutDocument>();
    {
        LayoutBlock b;
        b.type = BlockType::CodeFence;
        b.rect = D2D1::RectF(0, 0, 100, 50);
        vs.layout->blocks.push_back(std::move(b));
    }
    auto ctx = build_md_menu_context(vs, 200.0f, 200.0f);
    CHECK(ctx.code_block.present == false);
}

// ---- build_colorizer_menu_context ----
TEST_CASE("build_colorizer_menu_context: marks auto-detect when no force_grammar_id") {
    FakeColorizerView vs;
    auto ctx = build_colorizer_menu_context(vs,
        std::vector<LanguageOption>{ {"cpp", L"C++"} });
    CHECK(ctx.auto_detect_active == true);
    CHECK(ctx.active_grammar_id.empty());
    CHECK(ctx.languages.size() == 1);
}

TEST_CASE("build_colorizer_menu_context: forwards force_grammar_id to active") {
    FakeColorizerView vs;
    vs.force_grammar_id = "python";
    auto ctx = build_colorizer_menu_context(vs,
        std::vector<LanguageOption>{ {"cpp", L"C++"}, {"python", L"Python"} });
    CHECK(ctx.auto_detect_active == false);
    CHECK(ctx.active_grammar_id == "python");
}
```

- [ ] **Step 5.3: Run to verify failure**

```
cmake --build --preset conan-release
```

Expected: compile error — `runtime/host/context_menu.h` not found.

- [ ] **Step 5.4: Write the header**

Create `src/runtime/host/context_menu.h`:

```cpp
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/host/grammar_menu.h"
#include "runtime/interaction/interaction_engine.h"
#include "runtime/layout/layout_document.h"
#include "runtime/parser/document_model.h"

#include <string>
#include <vector>
#include <windows.h>

namespace wlx::runtime::host {

// ---------- Public types ----------

struct LinkMenuContext {
    bool         present = false;
    std::wstring url;
    bool         external = false;
};

struct CodeBlockMenuContext {
    bool present = false;
    int  block_index = -1;
};

struct MenuContext {
    bool                          has_selection = false;
    LinkMenuContext               link;
    CodeBlockMenuContext          code_block;
    std::vector<LanguageOption>   languages;
    std::string                   active_grammar_id;
    bool                          auto_detect_active = false;
    std::wstring                  config_path;
};

struct MenuResult {
    enum Kind {
        None             = 0,
        Copy             = 1,
        SelectAll        = 2,
        SearchGoogle     = 3,
        OpenLink         = 4,
        CopyLinkAddress  = 5,
        CopyCodeBlock    = 6,
        EditConfig       = 7,
        SetLanguage      = 8,
    };

    Kind        kind = None;
    std::string language_id;        // empty = auto-detect (for SetLanguage)
    int         code_block_index = -1;
};

// Build the menu, TrackPopupMenu(TPM_RETURNCMD), map ID → MenuResult.
// `screen_pt == {-1,-1}` means keyboard-invoked; the helper anchors at
// the top-left of the selection rect (via `owner` client→screen mapping)
// or the window center.
MenuResult show_context_menu(HWND owner, POINT screen_pt, const MenuContext& ctx);

// ---------- Test-only surface ----------

enum class MenuItemKind {
    Separator,
    Copy, SelectAll, SearchGoogle,
    OpenLink, CopyLinkAddress, CopyCodeBlock,
    EditConfig,
    LanguageSubmenuRoot,   // followed (logically) by the submenu items
};

struct MenuItem {
    MenuItemKind kind;
    bool         enabled = true;
};

// Pure: produces the flat menu-item sequence for `ctx`. The helper that
// builds the actual HMENU consumes this and renders separators only when
// they sit between non-empty sections (no leading / trailing / doubles).
std::vector<MenuItem> build_menu_items_for_test(const MenuContext& ctx);

// ---------- Per-plugin context builders ----------

// V must expose: layout (shared_ptr<LayoutDocument>), interaction
// (unique_ptr<InteractionEngine>), sel_anchor / sel_active (TextPosition).
template <typename V>
MenuContext build_md_menu_context(V& vs, float doc_x, float doc_y) {
    MenuContext ctx;
    ctx.has_selection = vs.sel_anchor.valid()
                     && vs.sel_anchor != vs.sel_active;

    if (vs.layout && vs.interaction) {
        auto hit = vs.interaction->hit_test(doc_x, doc_y);
        if (hit.hit) {
            using namespace wlx::runtime::parser;
            switch (hit.target.kind) {
                case LinkKind::ExternalUrl:
                    ctx.link.present  = true;
                    ctx.link.url      = hit.target.url;
                    ctx.link.external = true;
                    break;
                case LinkKind::RelativeDoc:
                    ctx.link.present = true;
                    ctx.link.url     = hit.target.url;
                    break;
                case LinkKind::InternalAnchor:
                    ctx.link.present = true;
                    ctx.link.url     = L"#" + hit.target.anchor_fragment;
                    break;
            }
        }
    }

    if (vs.layout) {
        using namespace wlx::runtime::parser;
        for (int i = 0; i < static_cast<int>(vs.layout->blocks.size()); ++i) {
            const auto& b = vs.layout->blocks[i];
            if (b.type != BlockType::CodeFence) continue;
            if (doc_x >= b.rect.left && doc_x <= b.rect.right
             && doc_y >= b.rect.top  && doc_y <= b.rect.bottom) {
                ctx.code_block.present     = true;
                ctx.code_block.block_index = i;
                break;
            }
        }
    }

    return ctx;
}

// V must expose: sel_anchor / sel_active (TextPosition), force_grammar_id (std::string).
template <typename V>
MenuContext build_colorizer_menu_context(V& vs, std::vector<LanguageOption> langs) {
    MenuContext ctx;
    ctx.has_selection = vs.sel_anchor.valid()
                     && vs.sel_anchor != vs.sel_active;
    ctx.languages = std::move(langs);
    ctx.active_grammar_id  = vs.force_grammar_id;
    ctx.auto_detect_active = vs.force_grammar_id.empty();
    return ctx;
}

}  // namespace wlx::runtime::host
```

- [ ] **Step 5.5: Write the implementation**

Create `src/runtime/host/context_menu.cpp`:

```cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/host/context_menu.h"

#include <shellapi.h>
#include <windows.h>

#include <algorithm>

#define WLX_TRACE_TAG L"wlx-host"
#include "runtime/diagnostics/wlx_trace.h"

namespace wlx::runtime::host {

namespace {

// Win32 menu command IDs. Submenu language items use a base + index.
constexpr UINT kIdCopy            = 0x9100;
constexpr UINT kIdSelectAll       = 0x9101;
constexpr UINT kIdSearchGoogle    = 0x9102;
constexpr UINT kIdOpenLink        = 0x9103;
constexpr UINT kIdCopyLinkAddress = 0x9104;
constexpr UINT kIdCopyCodeBlock   = 0x9105;
constexpr UINT kIdEditConfig      = 0x9106;
constexpr UINT kIdLangAuto        = 0x9107;
constexpr UINT kIdLangBase        = 0x9200;  // 0x9200 + index → SetLanguage(...)

void append_separator_if_nonempty(std::vector<MenuItem>& items) {
    if (items.empty()) return;
    if (items.back().kind == MenuItemKind::Separator) return;
    items.push_back({MenuItemKind::Separator, true});
}

void trim_trailing_separator(std::vector<MenuItem>& items) {
    while (!items.empty() && items.back().kind == MenuItemKind::Separator)
        items.pop_back();
}

}  // namespace

std::vector<MenuItem> build_menu_items_for_test(const MenuContext& ctx) {
    std::vector<MenuItem> items;

    // Section 1: Copy / Select All
    items.push_back({MenuItemKind::Copy,      ctx.has_selection});
    items.push_back({MenuItemKind::SelectAll, true});

    // Section 2: Search with Google
    append_separator_if_nonempty(items);
    items.push_back({MenuItemKind::SearchGoogle, ctx.has_selection});

    // Section 3: link items (md only)
    if (ctx.link.present) {
        append_separator_if_nonempty(items);
        items.push_back({MenuItemKind::OpenLink,        true});
        items.push_back({MenuItemKind::CopyLinkAddress, true});
    }

    // Section 4: code-block item (md only)
    if (ctx.code_block.present) {
        append_separator_if_nonempty(items);
        items.push_back({MenuItemKind::CopyCodeBlock, true});
    }

    // Section 5: Edit Plugin Config (always present when path supplied)
    if (!ctx.config_path.empty()) {
        append_separator_if_nonempty(items);
        items.push_back({MenuItemKind::EditConfig, true});
    }

    // Section 6: Force Language submenu root (colorizer only)
    if (!ctx.languages.empty()) {
        append_separator_if_nonempty(items);
        items.push_back({MenuItemKind::LanguageSubmenuRoot, true});
    }

    trim_trailing_separator(items);
    return items;
}

namespace {

HMENU build_language_submenu(const MenuContext& ctx) {
    HMENU sub = CreatePopupMenu();
    if (!sub) return nullptr;

    UINT auto_flags = MF_STRING;
    if (ctx.auto_detect_active) auto_flags |= MF_CHECKED;
    AppendMenuW(sub, auto_flags, kIdLangAuto, L"Auto-detect");
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);

    for (size_t i = 0; i < ctx.languages.size(); ++i) {
        const auto& lo = ctx.languages[i];
        UINT flags = MF_STRING;
        if (lo.grammar_id == ctx.active_grammar_id && !ctx.auto_detect_active)
            flags |= MF_CHECKED;
        AppendMenuW(sub, flags, kIdLangBase + static_cast<UINT>(i),
                    lo.display_name.c_str());
    }
    return sub;
}

void append_label(HMENU menu, MenuItemKind kind, bool enabled,
                  const MenuContext& ctx, HMENU lang_sub) {
    UINT flags = enabled ? MF_STRING : (MF_STRING | MF_GRAYED);
    switch (kind) {
        case MenuItemKind::Separator:
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            return;
        case MenuItemKind::Copy:
            AppendMenuW(menu, flags, kIdCopy, L"&Copy\tCtrl+C");           return;
        case MenuItemKind::SelectAll:
            AppendMenuW(menu, flags, kIdSelectAll, L"Select &All\tCtrl+A");  return;
        case MenuItemKind::SearchGoogle:
            AppendMenuW(menu, flags, kIdSearchGoogle, L"&Search with Google"); return;
        case MenuItemKind::OpenLink:
            AppendMenuW(menu, flags, kIdOpenLink, L"&Open Link");          return;
        case MenuItemKind::CopyLinkAddress:
            AppendMenuW(menu, flags, kIdCopyLinkAddress, L"Copy Link &Address"); return;
        case MenuItemKind::CopyCodeBlock:
            AppendMenuW(menu, flags, kIdCopyCodeBlock, L"Copy Code &Block");     return;
        case MenuItemKind::EditConfig:
            AppendMenuW(menu, flags, kIdEditConfig, L"&Edit Plugin Config");    return;
        case MenuItemKind::LanguageSubmenuRoot:
            if (lang_sub) {
                AppendMenuW(menu, MF_STRING | MF_POPUP,
                            reinterpret_cast<UINT_PTR>(lang_sub),
                            L"&Force Language");
            }
            return;
    }
    (void)ctx;
}

POINT resolve_anchor(HWND owner, POINT screen_pt) {
    if (screen_pt.x != -1 || screen_pt.y != -1) return screen_pt;
    // Keyboard-invoked: anchor at window-center.
    RECT r{};
    GetWindowRect(owner, &r);
    return { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
}

}  // namespace

MenuResult show_context_menu(HWND owner, POINT screen_pt, const MenuContext& ctx) {
    MenuResult result;

    auto items = build_menu_items_for_test(ctx);
    if (items.empty()) return result;

    HMENU menu = CreatePopupMenu();
    if (!menu) {
        WLX_TRACE(L"show_context_menu: CreatePopupMenu failed");
        return result;
    }

    HMENU lang_sub = ctx.languages.empty() ? nullptr : build_language_submenu(ctx);

    for (const auto& item : items)
        append_label(menu, item.kind, item.enabled, ctx, lang_sub);

    POINT anchor = resolve_anchor(owner, screen_pt);

    SetForegroundWindow(owner);  // ensure menu dismisses on click-outside
    UINT cmd = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        anchor.x, anchor.y, 0, owner, nullptr);

    DestroyMenu(menu);  // also destroys submenu

    if (cmd == 0) return result;

    if (cmd == kIdCopy)            result.kind = MenuResult::Copy;
    else if (cmd == kIdSelectAll)  result.kind = MenuResult::SelectAll;
    else if (cmd == kIdSearchGoogle) result.kind = MenuResult::SearchGoogle;
    else if (cmd == kIdOpenLink)   result.kind = MenuResult::OpenLink;
    else if (cmd == kIdCopyLinkAddress) result.kind = MenuResult::CopyLinkAddress;
    else if (cmd == kIdCopyCodeBlock) {
        result.kind = MenuResult::CopyCodeBlock;
        result.code_block_index = ctx.code_block.block_index;
    }
    else if (cmd == kIdEditConfig) result.kind = MenuResult::EditConfig;
    else if (cmd == kIdLangAuto) {
        result.kind = MenuResult::SetLanguage;
        result.language_id.clear();   // empty = auto-detect
    }
    else if (cmd >= kIdLangBase && cmd < kIdLangBase + 0x100) {
        size_t idx = cmd - kIdLangBase;
        if (idx < ctx.languages.size()) {
            result.kind = MenuResult::SetLanguage;
            result.language_id = ctx.languages[idx].grammar_id;
        }
    }
    else {
        WLX_TRACE(L"show_context_menu: unknown command id %u", cmd);
    }

    return result;
}

}  // namespace wlx::runtime::host
```

- [ ] **Step 5.6: Build and run tests**

```
cmake --build --preset conan-release
./build/Release/tests.exe -tc='MenuResult*,build_menu_items*,build_md_menu_context*,build_colorizer_menu_context*'
```

Expected: 14 cases pass.

- [ ] **Step 5.7: Run the full suite to confirm no regression**

```
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

Expected: all tests pass.

- [ ] **Step 5.8: Commit**

```
git add src/runtime/host/context_menu.h src/runtime/host/context_menu.cpp tests/runtime/host/test_context_menu.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(host): add context_menu module

Public surface: MenuContext, MenuResult, show_context_menu (uses
TrackPopupMenu(TPM_RETURNCMD)), and templated build_md_menu_context /
build_colorizer_menu_context helpers. Test surface: pure
build_menu_items_for_test for show/hide/enable/separator rules and
MenuResult::Kind enum-stability pin.

The TrackPopupMenu invocation itself is manual-smoke-only (no
headless message-pump harness); pure logic is unit-tested.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Wire WM_CONTEXTMENU + dispatch in the markdown plugin

**Goal:** Hook the menu into `wlx-listerine-md`. On `WM_CONTEXTMENU`, build the context, show the menu, dispatch the result. Reuse the view-actions helpers from Task 1; reuse the existing link/anchor/relative/external link dispatch from `WM_LBUTTONUP`; reuse the existing code-block copy from the hover button.

**Files:**
- Modify: `src/plugin_md/window/host_adapter.cpp`

- [ ] **Step 6.1: Add includes**

Near the top of `src/plugin_md/window/host_adapter.cpp`, alongside the other `runtime/host/` includes, add:

```cpp
#include "runtime/host/context_menu.h"
#include "runtime/host/grammar_menu.h"   // for LanguageOption (unused by md, but type comes from there)
#include "runtime/host/web_search.h"
```

- [ ] **Step 6.2: Add a small helper for the existing copy-code-block path**

The hover-button path (around line 444 in the existing file) already does:
```cpp
std::wstring code_text;
for (auto& run : block.text_runs) code_text += run.text;
copy_to_clipboard(hwnd, code_text);
vs->copied_code_block = i;
SetTimer(hwnd, TIMER_COPY_FEEDBACK, 1000, nullptr);
```

Lift this into a static helper near the other static helpers (around line 248, after `clear_selection`):

```cpp
static void copy_code_block_at_index(ViewState* vs, int index) {
    if (!vs || !vs->layout) return;
    if (index < 0 || index >= static_cast<int>(vs->layout->blocks.size())) return;
    const auto& block = vs->layout->blocks[index];
    std::wstring code_text;
    for (auto& run : block.text_runs) code_text += run.text;
    copy_to_clipboard(vs->hwnd, code_text);
    vs->copied_code_block = index;
    SetTimer(vs->hwnd, TIMER_COPY_FEEDBACK, 1000, nullptr);
    InvalidateRect(vs->hwnd, nullptr, FALSE);
}
```

Replace the inline body inside `WM_LBUTTONUP` (around line 444 — the loop over `is_in_copy_button`) with a call:

```cpp
        for (int i = 0; i < static_cast<int>(vs->layout->blocks.size()); i++) {
            auto& block = vs->layout->blocks[i];
            if (is_in_copy_button(block, doc_x, doc_y)) {
                copy_code_block_at_index(vs, i);
                clear_selection(vs);
                return 0;
            }
        }
```

- [ ] **Step 6.3: Add a link-action helper**

Lift the existing link-routing switch (around line 470, inside `WM_LBUTTONUP`) into a static helper near `copy_code_block_at_index`:

```cpp
static void invoke_link_action(ViewState* vs, const InteractionEngine::LinkAction& action) {
    switch (action.action) {
    case InteractionEngine::Action::ScrollToAnchor:
        vs->scroll_y = std::clamp(action.scroll_y, 0.0f, vs->max_scroll_y);
        update_scrollbar(vs);
        break;
    case InteractionEngine::Action::OpenExternal:
        ShellExecuteW(nullptr, L"open", action.target.c_str(),
                      nullptr, nullptr, SW_SHOW);
        break;
    case InteractionEngine::Action::ReloadDocument:
        if (!vs->file_path.empty()) {
            std::wstring dir = vs->file_path;
            auto fpos = dir.find_last_of(L"\\/");
            if (fpos != std::wstring::npos) dir = dir.substr(0, fpos + 1);
            load_document(vs, (dir + action.target).c_str());
        }
        break;
    default: break;
    }
}
```

Replace the existing inline `switch (action.action)` block in `WM_LBUTTONUP` with `invoke_link_action(vs, action);`.

- [ ] **Step 6.4: Add the `WM_CONTEXTMENU` handler**

In `ViewWndProc`, immediately after the `WM_LBUTTONDBLCLK` case (around line 521), insert:

```cpp
    case WM_CONTEXTMENU: {
        if (!vs || !vs->layout) return 0;

        // Commit any in-progress drag-select.
        if (vs->selecting) {
            vs->selecting = false;
            ReleaseCapture();
            KillTimer(hwnd, TIMER_AUTOSCROLL);
        }

        // Convert screen→client→DIP→document for hit-test.
        POINT screen_pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        float doc_x = 0, doc_y = 0;
        if (screen_pt.x != -1 || screen_pt.y != -1) {
            POINT client_pt = screen_pt;
            ScreenToClient(hwnd, &client_pt);
            doc_x = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(client_pt.x))
                                 : static_cast<float>(client_pt.x);
            float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(client_pt.y))
                                    : static_cast<float>(client_pt.y);
            doc_y = py + vs->scroll_y;
        }

        auto ctx = wlx::runtime::host::build_md_menu_context(*vs, doc_x, doc_y);
        ctx.config_path = get_module_dir() + L"wlx-listerine-md.toml";

        auto result = wlx::runtime::host::show_context_menu(hwnd, screen_pt, ctx);

        switch (result.kind) {
        case wlx::runtime::host::MenuResult::Copy:
            wlx::runtime::host::copy_selection(*vs, hwnd);
            break;

        case wlx::runtime::host::MenuResult::SelectAll:
            if (wlx::runtime::host::select_all(*vs))
                InvalidateRect(hwnd, nullptr, FALSE);
            break;

        case wlx::runtime::host::MenuResult::SearchGoogle: {
            if (vs->sel_anchor.valid() && vs->sel_anchor != vs->sel_active) {
                auto lo = std::min(vs->sel_anchor, vs->sel_active);
                auto hi = std::max(vs->sel_anchor, vs->sel_active);
                auto text = extract_selected_text(*vs->layout, lo, hi);
                wlx::runtime::host::search_with_google(text);
            }
            break;
        }

        case wlx::runtime::host::MenuResult::OpenLink: {
            if (vs->interaction) {
                auto hit = vs->interaction->hit_test(doc_x, doc_y);
                if (hit.hit) {
                    auto action = vs->interaction->resolve(hit.target);
                    invoke_link_action(vs, action);
                }
            }
            break;
        }

        case wlx::runtime::host::MenuResult::CopyLinkAddress:
            if (!ctx.link.url.empty())
                copy_to_clipboard(hwnd, ctx.link.url);
            break;

        case wlx::runtime::host::MenuResult::CopyCodeBlock:
            copy_code_block_at_index(vs, result.code_block_index);
            break;

        case wlx::runtime::host::MenuResult::EditConfig:
            ShellExecuteW(nullptr, L"open", ctx.config_path.c_str(),
                          nullptr, nullptr, SW_SHOW);
            break;

        case wlx::runtime::host::MenuResult::SetLanguage:
        case wlx::runtime::host::MenuResult::None:
        default:
            break;  // markdown plugin doesn't expose Force Language
        }

        return 0;
    }
```

- [ ] **Step 6.5: Build**

```
cmake --build --preset conan-release
```

Expected: clean build. If the build flags `LinkAction` not found, ensure `runtime/interaction/interaction_engine.h` is included (it should be already via `interaction_engine` include around line 28).

- [ ] **Step 6.6: Run unit tests**

```
./build/Release/tests.exe
```

Expected: all tests still pass (no behavior regressions in WM_LBUTTONUP / WM_KEYDOWN code paths now that they call helpers).

- [ ] **Step 6.7: Manual smoke (markdown plugin)**

Open Total Commander, view a markdown file with `wlx-listerine-md`. Verify each:

- Right-click on plain text with no selection → menu shows Copy (greyed), Select All, Search with Google (greyed), Edit Plugin Config. No link or code-block items.
- Drag-select some text, right-click on the selection → Copy enabled, Search with Google enabled. Click Copy → paste elsewhere matches selection.
- Right-click on an external link (`[example](https://example.com)`) → Open Link + Copy Link Address appear in their own section.
- Right-click on a fenced code block → Copy Code Block appears.
- Right-click while no document loaded (somehow — unlikely TC path) → no crash; menu suppressed.
- Press Shift+F10 anywhere in the document → menu appears at window-center; same items.
- Click Edit Plugin Config → your default `.toml` editor opens `wlx-listerine-md.toml`.
- Press Esc with menu open → menu dismisses; selection unchanged.

- [ ] **Step 6.8: Commit**

```
git add src/plugin_md/window/host_adapter.cpp
git commit -m "feat(plugin-md): add right-click context menu

Hooks WM_CONTEXTMENU to runtime/host/context_menu. Reuses extracted
view-actions (copy_selection, select_all), the existing link-action
switch (lifted to invoke_link_action), and the existing code-block
copy path (lifted to copy_code_block_at_index). Edit Plugin Config
opens wlx-listerine-md.toml via ShellExecuteW. Search with Google
URL-encodes the selection.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Wire `force_grammar_id` into the colorizer's tokenize path

**Goal:** Add a session-only language override field on `ColorViewState`. When set, it replaces the extension-based grammar lookup at the colorize call site. Reset on document load. No menu yet — that's Task 8.

**Files:**
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp`

- [ ] **Step 7.1: Add the field to ColorViewState**

In `src/plugin_colorizer/window/colorizer_host_adapter.cpp`, locate the `ColorViewState` struct (around line 97) and add a new field next to `wrap_text`:

```cpp
    // Force-language override (session-only). Empty = auto-detect from extension.
    // Set by the right-click "Force Language" submenu; reset on file reload.
    std::string force_grammar_id;
```

- [ ] **Step 7.2: Reset the field on `load_document`**

In `load_document` (around line 370), at the same place where `vs->scroll_y = 0` is reset, add:

```cpp
    vs->force_grammar_id.clear();
```

- [ ] **Step 7.3: Honor the override at the colorize call site**

In `load_document`, locate the language-detection block (around line 397):

```cpp
    std::string language = ext_to_language(vs->file_path);
    if (language.empty())
        language = filename_to_language(vs->file_path);
    language = apply_cpp_variant(language, g_display_cfg.cpp_grammar, g_colorizer_handle);
```

Change to:

```cpp
    std::string language = vs->force_grammar_id;
    if (language.empty()) {
        language = ext_to_language(vs->file_path);
        if (language.empty())
            language = filename_to_language(vs->file_path);
        language = apply_cpp_variant(language, g_display_cfg.cpp_grammar, g_colorizer_handle);
    }
```

- [ ] **Step 7.4: Add a `relayout_with_grammar` helper**

Near the existing `relayout` static helper (around line 420), add a sibling that re-runs colorize using the current `force_grammar_id` (or extension fallback) on the cached UTF-8 source — without reading from disk again:

```cpp
static void recolorize_with_force(ColorViewState* vs) {
    if (!vs || vs->cached_raw_utf8.empty()) return;

    std::string language = vs->force_grammar_id;
    if (language.empty()) {
        language = ext_to_language(vs->file_path);
        if (language.empty())
            language = filename_to_language(vs->file_path);
        language = apply_cpp_variant(language, g_display_cfg.cpp_grammar, g_colorizer_handle);
    }

    vs->cached_colors = {};
    if (!language.empty() && g_colorizer_handle &&
        wlx_core_supports(g_colorizer_handle, language.c_str()) == 1) {
        WlxColorSpan* spans = nullptr;
        uint32_t count = 0;
        if (wlx_core_colorize(g_colorizer_handle,
                              vs->cached_raw_utf8.c_str(),
                              static_cast<uint32_t>(vs->cached_raw_utf8.size()),
                              language.c_str(),
                              vs->dark_mode ? 1 : 0,
                              &spans, &count) == 0) {
            vs->cached_colors = abi_spans_to_result(spans, count);
        }
    }
    do_layout(vs, vs->cached_text, vs->cached_raw_utf8, vs->cached_colors);
    InvalidateRect(vs->hwnd, nullptr, FALSE);
}
```

- [ ] **Step 7.5: Build**

```
cmake --build --preset conan-release
```

Expected: clean build. The new field is unused by anything except the modified `load_document` and the new helper — no behavior change yet for live users.

- [ ] **Step 7.6: Run the full suite**

```
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

Expected: all tests pass.

- [ ] **Step 7.7: Commit**

```
git add src/plugin_colorizer/window/colorizer_host_adapter.cpp
git commit -m "feat(plugin-colorizer): add session-only force_grammar_id field

Adds a string on ColorViewState that overrides the extension-based
grammar lookup at the colorize call site when non-empty. Reset on
load_document. Adds recolorize_with_force(vs) helper that re-runs
colorize on the cached UTF-8 source — preparing the wiring the
upcoming right-click 'Force Language' submenu will trigger.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Wire WM_CONTEXTMENU + dispatch in the colorizer plugin

**Goal:** Hook the menu into `wlx-listerine-colorizer`. Reuses everything from Tasks 1, 2, 4, 5, 7. Adds dispatch for `MenuResult::SetLanguage` calling `recolorize_with_force`.

**Files:**
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp`

- [ ] **Step 8.1: Add includes**

Near the top of `src/plugin_colorizer/window/colorizer_host_adapter.cpp`, alongside the other `runtime/host/` includes, add:

```cpp
#include "runtime/host/context_menu.h"
#include "runtime/host/grammar_menu.h"
#include "runtime/host/web_search.h"
```

- [ ] **Step 8.2: Add the `WM_CONTEXTMENU` handler**

In `ColorViewWndProc`, immediately after the `WM_LBUTTONDBLCLK` case (around line 668), insert:

```cpp
    case WM_CONTEXTMENU: {
        if (!vs || !vs->layout) return 0;

        // Commit any in-progress drag-select.
        if (vs->selecting) {
            vs->selecting = false;
            ReleaseCapture();
            KillTimer(hwnd, TIMER_AUTOSCROLL);
        }

        POINT screen_pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

        auto langs = wlx::runtime::host::available_grammars(g_colorizer_handle);
        auto ctx = wlx::runtime::host::build_colorizer_menu_context(*vs, std::move(langs));
        ctx.config_path = get_module_dir() + L"wlx-listerine-colorizer.toml";

        auto result = wlx::runtime::host::show_context_menu(hwnd, screen_pt, ctx);

        switch (result.kind) {
        case wlx::runtime::host::MenuResult::Copy:
            wlx::runtime::host::copy_selection(*vs, hwnd);
            break;

        case wlx::runtime::host::MenuResult::SelectAll:
            if (wlx::runtime::host::select_all(*vs))
                InvalidateRect(hwnd, nullptr, FALSE);
            break;

        case wlx::runtime::host::MenuResult::SearchGoogle: {
            if (vs->sel_anchor.valid() && vs->sel_anchor != vs->sel_active) {
                auto lo = std::min(vs->sel_anchor, vs->sel_active);
                auto hi = std::max(vs->sel_anchor, vs->sel_active);
                auto text = extract_selected_text(*vs->layout, lo, hi);
                wlx::runtime::host::search_with_google(text);
            }
            break;
        }

        case wlx::runtime::host::MenuResult::EditConfig:
            ShellExecuteW(nullptr, L"open", ctx.config_path.c_str(),
                          nullptr, nullptr, SW_SHOW);
            break;

        case wlx::runtime::host::MenuResult::SetLanguage:
            // Empty language_id = auto-detect.
            vs->force_grammar_id = result.language_id;
            recolorize_with_force(vs);
            break;

        case wlx::runtime::host::MenuResult::OpenLink:
        case wlx::runtime::host::MenuResult::CopyLinkAddress:
        case wlx::runtime::host::MenuResult::CopyCodeBlock:
        case wlx::runtime::host::MenuResult::None:
        default:
            break;  // colorizer has no links or code-block boundaries
        }

        return 0;
    }
```

Note: `<shellapi.h>` may need to be included at the top for `ShellExecuteW`. Check the existing includes; if `<shellapi.h>` isn't already there (the md plugin's `host_adapter.cpp` includes it; the colorizer's may not — verify by searching for `shellapi`), add it next to `<windowsx.h>`:

```cpp
#include <shellapi.h>
```

- [ ] **Step 8.3: Build**

```
cmake --build --preset conan-release
```

Expected: clean build.

- [ ] **Step 8.4: Run the full suite**

```
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

Expected: all tests pass.

- [ ] **Step 8.5: Manual smoke (colorizer plugin)**

Open Total Commander, view a code file with `wlx-listerine-colorizer`. Verify each:

- Right-click on plain text → Copy (greyed/enabled per selection), Select All, Search with Google, Edit Plugin Config, Force Language ▸.
- Open `Force Language ▸` submenu → top entry is `Auto-detect` with checkmark; alphabetical list of grammars below; the active grammar (the one the file's extension resolved to) has a checkmark.
- Click `Force Language → Python` on a `.txt` file → file is re-tokenized as Python; the next time you open the submenu, `Python` has the checkmark and `Auto-detect` does not.
- Click `Force Language → Auto-detect` → file goes back to extension-based detection; `Auto-detect` regains its checkmark.
- Open a *different* file (or reload) → override resets; `Auto-detect` is checked.
- Right-click → Edit Plugin Config → your default `.toml` editor opens `wlx-listerine-colorizer.toml`.
- Drag-select; right-click; Search with Google → opens Google search with the selection in your default browser.
- Press Shift+F10 → menu appears at window-center.

- [ ] **Step 8.6: Commit**

```
git add src/plugin_colorizer/window/colorizer_host_adapter.cpp
git commit -m "feat(plugin-colorizer): add right-click context menu

Hooks WM_CONTEXTMENU to runtime/host/context_menu. Force Language
submenu lists all grammars from wlx_core_list_languages, with
checkmarks on the active grammar (or Auto-detect when no override).
Picking a language sets vs->force_grammar_id and re-runs colorize on
the cached source via recolorize_with_force; the override is
session-only and resets on file load.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Final verification

**Goal:** Run a final sweep — full build, full unit tests, run the existing visual regression suite to confirm nothing in the rendering path changed.

- [ ] **Step 9.1: Clean build**

```
cmake --build --preset conan-release --clean-first
```

Expected: clean build.

- [ ] **Step 9.2: Full unit tests**

```
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

Expected: every test passes — old + the new ones from Tasks 1, 2, 3, 4, 5.

- [ ] **Step 9.3: Visual regression sweep**

```
./scripts/visual-test.sh
```

Expected: every case still ≥ 95% pixel similarity. The context menu changes touched no rendering code, so all goldens should match within tolerance.

- [ ] **Step 9.4: Combined manual smoke checklist**

Confirm — across both plugins — every row from the spec's Edge Cases table:

| Case | Verified? |
|---|---|
| Right-click during in-progress drag-select | □ both plugins |
| Right-click inside selection covering a link | □ md only |
| `WM_CONTEXTMENU` with `lParam == (-1,-1)` (Shift+F10) | □ both |
| Search Google with whitespace-only selection | □ both |
| Search Google with selection > 1500 wchars | □ both |
| Force Language → Auto-detect on already auto-detected file | □ colorizer |
| Menu opened with no document loaded | □ both |
| Menu dismissed via Esc / click outside | □ both |
| Edit Plugin Config opens correct file | □ both |

- [ ] **Step 9.5: Final no-op commit (only if any minor doc/comment fixes accumulated)**

If any incidental fix-ups landed during smoke (e.g., a typo in a comment), commit them as `chore(menu): smoke-test follow-ups`. Otherwise skip this step.

---

## Self-review

**Spec coverage:**

| Spec section | Implemented by |
|---|---|
| Native Win32 popup menu (TrackPopupMenu) | Task 5 |
| Both plugins, shared mechanics | Tasks 5, 6, 8 |
| Markdown menu (Copy / Select All / Search Google / link items / code-block / Edit Config) | Task 6 |
| Colorizer menu (no link/code items; + Force Language) | Tasks 7, 8 |
| `MenuContext` / `MenuResult` types | Task 5 |
| `web_search.search_with_google` | Task 2 |
| `grammar_menu.available_grammars` + display-name table | Tasks 3, 4 |
| Force-language session-only (resets on load_document) | Task 7 |
| Selection not modified by right-click | Task 6 (no selection mutation in handler) + Task 8 |
| Keyboard invocation (Shift+F10) anchor | Task 5 (`resolve_anchor`) |
| Drag-select commit on right-click | Tasks 6, 8 (handler prologue) |
| Trace-only error handling | Tasks 2, 5 |
| Unit tests for pure logic (4 test files) | Tasks 1, 2, 4, 5 |
| Manual smoke for menu invocation | Tasks 6.7, 8.5, 9.4 |

**Placeholder scan:** No "TBD", "TODO", or "implement appropriate handling" patterns. Every code step shows complete code. Every test step shows the assertions. Every command step shows the exact command.

**Type consistency:**
- `MenuContext` fields used in tests (Task 5) match the header definition (Task 5).
- `MenuResult::Kind` enum values pinned in test (Task 5.2) match the header (Task 5.4).
- `LanguageOption { grammar_id, display_name }` defined once in `grammar_menu.h` (Task 4) and reused in `context_menu.h` (Task 5) and in tests (Tasks 4, 5).
- `force_grammar_id` is `std::string` in both ColorViewState (Task 7), MenuContext::active_grammar_id (Task 5), and MenuResult::language_id (Task 5).
- `copy_selection<V>(v, hwnd)` and `select_all<V>(v)` signatures used in Task 1 tests match Task 1 header definitions; same calls in Tasks 6.4 and 8.2 use the same signatures.
- `build_md_menu_context<V>(vs, doc_x, doc_y)` and `build_colorizer_menu_context<V>(vs, langs)` signatures consistent across Tasks 5.4, 5.2 (test), 6.4, 8.2.

No issues found.
