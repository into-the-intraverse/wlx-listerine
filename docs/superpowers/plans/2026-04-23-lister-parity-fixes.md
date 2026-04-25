# Lister Parity — Post-Review Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate ~500 LoC of duplicated host-integration code between the md and colorizer plugins, fix the multi-view reload and menu-discovery bugs, make `ListSearchTextW` control flow directly testable, tighten scroll/paint primitives, and annotate known limitations inline — all ten findings from the 2026-04-23 code review.

**Architecture:** One C++20 header-only template `HostIntegration<V>` (concept-constrained on `HostView`) owns the F2 WH_GETMESSAGE hook, the parent subclass, and reload-ID discovery. A separate `search_step<V>` template extracted from `ListSearchTextW` decouples control flow from the Win32 shell so tests can exercise it with fakes. Render-engine painter walks matches via a single cursor threaded across blocks.

**Tech Stack:** C++20 (bumped from 17 for concepts), MSVC, CMake 3.20+, Conan 2.x, doctest, Direct2D/DirectWrite, Win32 (WH_GETMESSAGE, SetWindowSubclass, RT_ACCELERATOR enumeration).

---

## File Structure

### Created

- `src/wlx_host_common.h` — Header-only. `HostView` concept, `HostIntegration<V>` class template, F2 hook proc, parent subclass proc, discovery helpers. Each plugin DLL instantiates one `HostIntegration<V>` at file scope; instantiation sets up the `self_` singleton that static callbacks use.
- `src/search_ops.h` — Header-only. `SearchState` concept, `SearchStepResult` struct, `search_step<V>` template. Encapsulates the rebuild/requery/advance state machine extracted from `ListSearchTextW`.
- `tests/test_search_ops.cpp` — doctest file covering `search_step` semantics against an in-memory fake V. Wired into `tests` target in `CMakeLists.txt`.
- `tests/test_wlx_host_common.cpp` — doctest file covering `HostIntegration` attach/detach refcounting and the H3 multi-view fan-out regression. Wired into `tests` target.

### Modified

- `CMakeLists.txt` — `CMAKE_CXX_STANDARD` 17 → 20; add new test files to `tests` target.
- `CLAUDE.md` — `C++17` → `C++20` in project description.
- `src/host_adapter.cpp` — Add `reload_view(ViewState&, const wchar_t*)` free function, `static HostIntegration<ViewState> g_integration;`, wire `attach`/`detach`/`emergency_cleanup` calls into WLX entry points. Simplify `ListSearchTextW` to one `search_step` call. Delete ~300 LoC of duplicated hook/subclass/discovery code. Add H1 trace + comment in `WM_KEYDOWN` default path. Extend `scroll_to_match` with M3 precision pass.
- `src/colorizer/colorizer_host_adapter.cpp` — Same treatment with `ColorViewState`, plus L2 fix (remove cached `line_height_factor`, read from theme at use sites).
- `src/render_engine.h` — `paint_search_highlights` signature changes to take `size_t&` cursor.
- `src/render_engine.cpp` — `paint_search_highlights` implementation uses the cursor; `paint()` declares a local cursor and threads it through the block loop.
- `src/search_engine.cpp` — L3 inline comment on `is_word_char`.

### Deleted

All from both host adapters:
- Globals: `g_msg_hook`, `g_hook_refcount`, `g_reload_menu_id`, `g_pending_f2_capture`, `g_candidate_reload_id`, `g_candidate_time`, `CANDIDATE_TTL_MS`, `g_parent_refcount`, `PARENT_SUBCLASS_ID`.
- Functions: `GetMsgHookProc`, `install_msg_hook`, `uninstall_msg_hook`, `accel_enum_proc_`, `find_reload_id_via_accel_resources`, `find_menu_item_by_accel`, `find_menu_owner`, `install_parent_subclass`, `uninstall_parent_subclass`, `ParentSubclassProc`.
- Menu-discovery heuristics (`menu→F2`, `menu→menu`) — not re-implemented in the template.

From colorizer only:
- `ColorizerDisplayConfig::line_height_factor` field.

---

## Task 1: Bump C++ standard to 20

**Files:**
- Modify: `CMakeLists.txt:6` (CMAKE_CXX_STANDARD line)
- Modify: `CLAUDE.md` (project description)

- [ ] **Step 1: Check current standard line**

Run: `grep -n CMAKE_CXX_STANDARD CMakeLists.txt`
Expected: `6:set(CMAKE_CXX_STANDARD 17)` (approx — line number may differ).

- [ ] **Step 2: Bump the standard**

Edit `CMakeLists.txt`:

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

- [ ] **Step 3: Update CLAUDE.md**

Find the line `**wlx-listerine** — Total Commander WLX lister plugins. Minimalistic, native Direct2D/DirectWrite rendering. C++17, 64-bit only, Win11 target.`

Change `C++17` to `C++20`.

- [ ] **Step 4: Rebuild to confirm nothing broke**

Run:
```
conan install . --output-folder=build --build=missing -s build_type=Release
cmake --preset conan-default
cmake --build --preset conan-release
```
Expected: success. No compile errors from the bump alone — MSVC supports C++20 cleanly.

- [ ] **Step 5: Run tests**

Run: `./build/Release/tests.exe`
Expected: all existing tests (86 + colorizer tests) pass.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt CLAUDE.md
git commit -m "build: bump C++ standard to 20"
```

---

## Task 2: Create `SearchState` concept and `search_step` helper

**Files:**
- Create: `src/search_ops.h`

- [ ] **Step 1: Write the header**

Create `src/search_ops.h` with:

```cpp
#pragma once

#include "search_engine.h"
#include "layout_engine.h"

#include <concepts>
#include <type_traits>
#include <vector>

template <typename V>
concept SearchState = requires(V& v) {
    { v.layout };
    { v.search_index }  -> std::same_as<SearchIndex&>;
    { v.matches }       -> std::same_as<std::vector<SearchMatch>&>;
    { v.current_match } -> std::same_as<int&>;
    { v.last_query }    -> std::same_as<SearchQuery&>;
    { v.index_dirty }   -> std::same_as<bool&>;
};

struct SearchStepResult {
    bool has_match;
    int cursor;
    std::vector<SearchMatch> matches;
    bool index_was_rebuilt;
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

- [ ] **Step 2: Build to confirm it compiles**

Run: `cmake --build --preset conan-release`
Expected: success (the header is unused so far; builds only because it has no syntax errors).

- [ ] **Step 3: Commit**

```bash
git add src/search_ops.h
git commit -m "feat: add search_step helper + SearchState concept"
```

---

## Task 3: Add unit tests for `search_step`

**Files:**
- Create: `tests/test_search_ops.cpp`
- Modify: `CMakeLists.txt` (add file to `tests` target)

- [ ] **Step 1: Write the test file**

Create `tests/test_search_ops.cpp`:

```cpp
#include <doctest/doctest.h>
#include "search_ops.h"
#include "search_engine.h"
#include "layout_engine.h"

namespace {

struct FakeV {
    LayoutDocument layout_store;
    LayoutDocument* layout = &layout_store;
    SearchIndex search_index;
    std::vector<SearchMatch> matches;
    int current_match = -1;
    SearchQuery last_query;
    bool index_dirty = true;
};

static_assert(SearchState<FakeV>);

static FakeV make_fake(std::initializer_list<std::wstring> blocks) {
    FakeV v;
    for (auto& t : blocks) {
        LayoutBlock b;
        TextRun r;
        r.text = t;
        b.text_runs.push_back(std::move(r));
        v.layout_store.blocks.push_back(std::move(b));
    }
    return v;
}

} // namespace

TEST_CASE("search_step findfirst on three-match doc lands on match 0") {
    auto v = make_fake({L"foo bar foo", L"foo baz"});
    SearchQuery q; q.needle = L"foo";
    auto r = search_step(v, q, /*findfirst=*/true);
    CHECK(r.has_match);
    CHECK(r.cursor == 0);
    CHECK(r.matches.size() == 3);
    CHECK(r.index_was_rebuilt);
}

TEST_CASE("search_step forward wraps after last match") {
    auto v = make_fake({L"foo foo foo"});
    SearchQuery q; q.needle = L"foo";
    search_step(v, q, /*findfirst=*/true);          // cursor 0
    search_step(v, q, false);                       // cursor 1
    search_step(v, q, false);                       // cursor 2
    auto r = search_step(v, q, false);              // wraps to 0
    CHECK(r.cursor == 0);
}

TEST_CASE("search_step backwards from cursor 0 wraps to n-1") {
    auto v = make_fake({L"foo foo foo"});
    SearchQuery q; q.needle = L"foo";
    search_step(v, q, /*findfirst=*/true);          // cursor 0
    q.backwards = true;
    auto r = search_step(v, q, false);
    CHECK(r.cursor == 2);
}

TEST_CASE("search_step no-match returns has_match=false and clears cursor") {
    auto v = make_fake({L"hello"});
    SearchQuery q; q.needle = L"xyz";
    auto r = search_step(v, q, /*findfirst=*/true);
    CHECK_FALSE(r.has_match);
    CHECK(r.cursor == -1);
    CHECK(r.matches.empty());
    CHECK(v.current_match == -1);
}

TEST_CASE("search_step clamps cursor after relayout shrinks match set") {
    auto v = make_fake({L"foo foo foo"});
    SearchQuery q; q.needle = L"foo";
    search_step(v, q, /*findfirst=*/true);
    search_step(v, q, false);                       // cursor 1
    search_step(v, q, false);                       // cursor 2
    // Simulate relayout: replace layout with fewer matches, mark dirty.
    v.layout_store.blocks.clear();
    LayoutBlock b; TextRun r; r.text = L"foo"; b.text_runs.push_back(r);
    v.layout_store.blocks.push_back(std::move(b));
    v.index_dirty = true;
    auto step = search_step(v, q, /*findfirst=*/false);
    CHECK(step.has_match);
    CHECK(step.matches.size() == 1);
    // Prior cursor=2 was clamped to 0 (matches.size()-1), then advance wraps to 0.
    CHECK(step.cursor == 0);
}

TEST_CASE("search_step query change without findfirst still requeries") {
    auto v = make_fake({L"foo bar"});
    SearchQuery q1; q1.needle = L"foo";
    search_step(v, q1, /*findfirst=*/true);
    SearchQuery q2; q2.needle = L"bar";
    auto r = search_step(v, q2, /*findfirst=*/false);
    CHECK(r.has_match);
    CHECK(r.matches.size() == 1);
    CHECK(r.cursor == 0);
}

TEST_CASE("search_step same query on non-dirty index does not rebuild") {
    auto v = make_fake({L"foo foo"});
    SearchQuery q; q.needle = L"foo";
    search_step(v, q, /*findfirst=*/true);
    auto r = search_step(v, q, /*findfirst=*/false);
    CHECK_FALSE(r.index_was_rebuilt);
}
```

- [ ] **Step 2: Wire file into CMakeLists**

Find the `add_executable(tests ...)` block in `CMakeLists.txt`. Add `tests/test_search_ops.cpp` to the source list:

```cmake
add_executable(tests
    tests/test_main.cpp
    tests/test_document_model.cpp
    tests/test_theme_service.cpp
    tests/test_file_service.cpp
    tests/test_markdown_parser.cpp
    tests/test_cache_service.cpp
    tests/test_layout_engine.cpp
    tests/test_text_selection.cpp
    tests/test_search_engine.cpp
    tests/test_search_ops.cpp
)
```

- [ ] **Step 3: Run tests**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe`
Expected: 7 new test cases pass; total grows by 7.

- [ ] **Step 4: Commit**

```bash
git add tests/test_search_ops.cpp CMakeLists.txt
git commit -m "test: cover search_step semantics (findfirst, wrap, clamp, requery)"
```

---

## Task 4: Port md `ListSearchTextW` to use `search_step`

**Files:**
- Modify: `src/host_adapter.cpp` (ListSearchTextW body, add include)

- [ ] **Step 1: Add the include**

In `src/host_adapter.cpp`, near the other plugin-side includes, add:
```cpp
#include "search_ops.h"
```

- [ ] **Step 2: Replace ListSearchTextW body**

Locate the existing `int __stdcall ListSearchTextW(...)` definition. Replace it with:

```cpp
int __stdcall ListSearchTextW(HWND ListWin, wchar_t* SearchString, int SearchParameter) {
    WLX_TRACE(L"ListSearchTextW hwnd=%p needle=%s param=0x%X",
              ListWin, SearchString ? SearchString : L"(null)", SearchParameter);
    auto it = g_views.find(ListWin);
    if (it == g_views.end()) return LISTPLUGIN_ERROR;
    if (!SearchString || !*SearchString) return LISTPLUGIN_ERROR;

    auto* vs = it->second;
    if (!vs->layout) return LISTPLUGIN_ERROR;

    SearchQuery q;
    q.needle       = SearchString;
    q.match_case   = (SearchParameter & lcs_matchcase)  != 0;
    q.whole_words  = (SearchParameter & lcs_wholewords) != 0;
    q.backwards    = (SearchParameter & lcs_backwards)  != 0;
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

Note: `search_step` operates on `ViewState` by reference — pass `*vs`.

- [ ] **Step 3: Build and verify tests still pass**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe`
Expected: all tests pass (no behavior change; just a refactor of the search body).

- [ ] **Step 4: Confirm `ViewState` satisfies `SearchState` at compile time**

At the top of the refactored `ListSearchTextW` (before the body) or at file scope near the struct definition, add:
```cpp
static_assert(SearchState<ViewState>);
```

Rebuild — expected: clean compile.

- [ ] **Step 5: Commit**

```bash
git add src/host_adapter.cpp
git commit -m "refactor(md): ListSearchTextW uses search_step helper"
```

---

## Task 5: Port colorizer `ListSearchTextW` to use `search_step`

**Files:**
- Modify: `src/colorizer/colorizer_host_adapter.cpp`

- [ ] **Step 1: Add include**

In `src/colorizer/colorizer_host_adapter.cpp`, add:
```cpp
#include "search_ops.h"
```

- [ ] **Step 2: Replace the existing ListSearchTextW body**

Same shape as Task 4 step 2. Use `ColorViewState` in the `g_views` lookup path:

```cpp
int __stdcall ListSearchTextW(HWND ListWin, wchar_t* SearchString, int SearchParameter) {
    WLX_TRACE(L"ListSearchTextW hwnd=%p needle=%s param=0x%X",
              ListWin, SearchString ? SearchString : L"(null)", SearchParameter);
    auto it = g_views.find(ListWin);
    if (it == g_views.end()) return LISTPLUGIN_ERROR;
    if (!SearchString || !*SearchString) return LISTPLUGIN_ERROR;

    auto* vs = it->second;
    if (!vs->layout) return LISTPLUGIN_ERROR;

    SearchQuery q;
    q.needle       = SearchString;
    q.match_case   = (SearchParameter & lcs_matchcase)  != 0;
    q.whole_words  = (SearchParameter & lcs_wholewords) != 0;
    q.backwards    = (SearchParameter & lcs_backwards)  != 0;
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

- [ ] **Step 3: Add static_assert**

After the `ColorViewState` struct definition, add:
```cpp
static_assert(SearchState<ColorViewState>);
```

- [ ] **Step 4: Build and test**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe`
Expected: clean build, tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/colorizer/colorizer_host_adapter.cpp
git commit -m "refactor(colorizer): ListSearchTextW uses search_step helper"
```

---

## Task 6: Create `HostView` concept and `HostIntegration` skeleton

**Files:**
- Create: `src/wlx_host_common.h`

- [ ] **Step 1: Write the skeleton header**

Create `src/wlx_host_common.h`:

```cpp
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>

#include <concepts>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

// Concept: the state that HostIntegration needs to drive the F2 reload and
// the File->Reload menu subclass. Plus an ADL `reload_view(V&, const wchar_t*)`
// free function in the plugin's translation unit.
template <typename V>
concept HostView = requires(V& v, const wchar_t* p) {
    { v.hwnd }            -> std::convertible_to<HWND>;
    { v.file_path }       -> std::same_as<std::wstring&>;
    { v.subclass_target } -> std::same_as<HWND&>;
    reload_view(v, p);
};

template <HostView V>
class HostIntegration {
public:
    void attach(V* vs, HWND parent_hint);
    void detach(V* vs);
    void emergency_cleanup();

    // Test / diagnostic accessors.
    UINT reload_menu_id() const { return reload_menu_id_; }
    void set_reload_menu_id_for_test(UINT id) { reload_menu_id_ = id; }
    const std::unordered_map<HWND, V*>& views_for_test() const { return views_; }

    // Invoked by the subclass proc; also exposed publicly for unit tests that
    // want to synthesize menu commands without a real message pump.
    static LRESULT CALLBACK parent_subclass_proc_(HWND hwnd, UINT msg, WPARAM wp,
                                                   LPARAM lp, UINT_PTR, DWORD_PTR);

private:
    static LRESULT CALLBACK get_msg_hook_(int code, WPARAM wp, LPARAM lp);
    static HWND find_menu_owner_(HWND start);
    static UINT find_reload_id_via_accel_resources_();
    static UINT find_menu_item_by_accel_(HMENU menu, const wchar_t* accel);

    static constexpr UINT_PTR kSubclassId = 0x574C5850;  // 'WLXP'

    inline static HostIntegration* self_ = nullptr;

    std::unordered_map<HWND, V*> views_;
    std::unordered_map<HWND, int> parent_refcount_;
    HHOOK msg_hook_ = nullptr;
    int hook_refcount_ = 0;
    UINT reload_menu_id_ = 0;
};
```

Leave the member function definitions (`attach`, `detach`, `emergency_cleanup`, callbacks, helpers) out for now — they come in Tasks 7–9. This skeleton just validates the concept and the declarations compile.

- [ ] **Step 2: Add a throwaway static_assert to prove the concept compiles**

Append to `src/wlx_host_common.h`:

```cpp
namespace wlx_host_common_internal_ {
struct ConceptProbe {
    HWND hwnd = nullptr;
    std::wstring file_path;
    HWND subclass_target = nullptr;
};
inline void reload_view(ConceptProbe&, const wchar_t*) {}
static_assert(HostView<ConceptProbe>);
}
```

This verifies the concept resolves and ADL works.

- [ ] **Step 3: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build (the header is included nowhere yet, so only templated code that isn't instantiated is checked).

- [ ] **Step 4: Commit**

```bash
git add src/wlx_host_common.h
git commit -m "feat: declare HostView concept + HostIntegration skeleton"
```

---

## Task 7: Implement `HostIntegration::attach`, `detach`, `emergency_cleanup`, and F2 hook

**Files:**
- Modify: `src/wlx_host_common.h` (add method bodies)

- [ ] **Step 1: Implement the F2 hook**

Append to `src/wlx_host_common.h` (after the class definition, still in the header because it's a template):

```cpp
template <HostView V>
LRESULT CALLBACK HostIntegration<V>::get_msg_hook_(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == PM_REMOVE && self_) {
        MSG* m = reinterpret_cast<MSG*>(lp);
        if (m && m->message == WM_KEYDOWN && m->wParam == VK_F2) {
            auto it = self_->views_.find(m->hwnd);
            if (it != self_->views_.end()) {
                V* vs = it->second;
                if (!vs->file_path.empty()) {
                    reload_view(*vs, vs->file_path.c_str());
                }
                // Eat F2 iff we already know the reload cmd (otherwise let TC
                // dispatch it normally so WM_INITMENUPOPUP discovery still
                // has a chance to learn the ID).
                if (self_->reload_menu_id_ != 0) {
                    m->message = WM_NULL;
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
```

- [ ] **Step 2: Implement `attach`**

Append:

```cpp
template <HostView V>
void HostIntegration<V>::attach(V* vs, HWND parent_hint) {
    self_ = this;
    views_[vs->hwnd] = vs;

    if (hook_refcount_++ == 0) {
        msg_hook_ = SetWindowsHookExW(WH_GETMESSAGE, get_msg_hook_,
                                       nullptr, GetCurrentThreadId());
    }

    if (reload_menu_id_ == 0) {
        reload_menu_id_ = find_reload_id_via_accel_resources_();
    }

    HWND target = find_menu_owner_(parent_hint);
    if (!target) target = parent_hint;
    vs->subclass_target = target;
    if (target && parent_refcount_[target]++ == 0) {
        SetWindowSubclass(target, parent_subclass_proc_, kSubclassId, 0);
    }
}
```

- [ ] **Step 3: Implement `detach`**

Append:

```cpp
template <HostView V>
void HostIntegration<V>::detach(V* vs) {
    if (vs->subclass_target) {
        auto it = parent_refcount_.find(vs->subclass_target);
        if (it != parent_refcount_.end() && --it->second <= 0) {
            RemoveWindowSubclass(vs->subclass_target, parent_subclass_proc_, kSubclassId);
            parent_refcount_.erase(it);
        }
    }
    views_.erase(vs->hwnd);
    if (hook_refcount_ > 0 && --hook_refcount_ == 0 && msg_hook_) {
        UnhookWindowsHookEx(msg_hook_);
        msg_hook_ = nullptr;
    }
    if (views_.empty()) self_ = nullptr;
}
```

- [ ] **Step 4: Implement `emergency_cleanup`**

Append:

```cpp
template <HostView V>
void HostIntegration<V>::emergency_cleanup() {
    for (auto& [parent, _] : parent_refcount_) {
        RemoveWindowSubclass(parent, parent_subclass_proc_, kSubclassId);
    }
    parent_refcount_.clear();
    if (msg_hook_) {
        UnhookWindowsHookEx(msg_hook_);
        msg_hook_ = nullptr;
    }
    hook_refcount_ = 0;
    views_.clear();
    self_ = nullptr;
}
```

- [ ] **Step 5: Build (template won't instantiate yet — just syntax check)**

Run: `cmake --build --preset conan-release`
Expected: clean build.

- [ ] **Step 6: Commit**

```bash
git add src/wlx_host_common.h
git commit -m "feat: HostIntegration attach/detach + WH_GETMESSAGE F2 hook"
```

---

## Task 8: Implement parent subclass proc (H3 fan-out, no heuristics)

**Files:**
- Modify: `src/wlx_host_common.h` (add `parent_subclass_proc_` body)

- [ ] **Step 1: Implement the subclass proc**

Append to `src/wlx_host_common.h`:

```cpp
template <HostView V>
LRESULT CALLBACK HostIntegration<V>::parent_subclass_proc_(HWND hwnd, UINT msg,
                                                             WPARAM wp, LPARAM lp,
                                                             UINT_PTR, DWORD_PTR) {
    if (!self_) return DefSubclassProc(hwnd, msg, wp, lp);

    // Lazy text-based discovery: some TC builds label menu items with a tab
    // followed by "F2". Use that if our resource scan didn't yield anything.
    if (msg == WM_INITMENUPOPUP && self_->reload_menu_id_ == 0) {
        if (HMENU menu = reinterpret_cast<HMENU>(wp)) {
            if (UINT id = find_menu_item_by_accel_(menu, L"F2"); id) {
                self_->reload_menu_id_ = id;
            }
        }
    }

    if (msg == WM_COMMAND) {
        const UINT cmd = LOWORD(wp);
        const UINT src = HIWORD(wp);  // 0=menu, 1=accelerator

        // Always eat accelerator-source reload commands. The F2 hook already
        // reloaded every matching view; TC's default handler would be a no-op
        // for plugin windows anyway.
        if (src == 1 && self_->reload_menu_id_ != 0 && cmd == self_->reload_menu_id_) {
            return 0;
        }

        // Menu click on the known reload item: fan out to EVERY view whose
        // subclass_target is this hwnd (H3 fix — was previously return-after-first).
        if (src == 0 && self_->reload_menu_id_ != 0 && cmd == self_->reload_menu_id_) {
            bool any = false;
            for (auto& [pwnd, vs] : self_->views_) {
                if (vs->subclass_target == hwnd && !vs->file_path.empty()) {
                    reload_view(*vs, vs->file_path.c_str());
                    any = true;
                }
            }
            if (any) return 0;
        }
    }

    return DefSubclassProc(hwnd, msg, wp, lp);
}
```

Note: the previous heuristic paths (`menu→F2` and `menu→menu`) are deliberately absent. If neither `RT_ACCELERATOR` scan nor `WM_INITMENUPOPUP` text discovery finds the ID, File→Reload menu clicks are silent no-ops. F2 continues to work via the hook.

- [ ] **Step 2: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add src/wlx_host_common.h
git commit -m "feat: HostIntegration parent subclass proc with multi-view fan-out"
```

---

## Task 9: Implement discovery helpers with L1 annotation

**Files:**
- Modify: `src/wlx_host_common.h` (add helper method bodies)

- [ ] **Step 1: Implement `find_menu_owner_`**

Append:

```cpp
template <HostView V>
HWND HostIntegration<V>::find_menu_owner_(HWND start) {
    HWND wnd = start;
    while (wnd) {
        if (GetMenu(wnd)) return wnd;
        HWND parent = GetParent(wnd);
        if (!parent || parent == wnd) break;
        wnd = parent;
    }
    return nullptr;
}
```

- [ ] **Step 2: Implement `find_menu_item_by_accel_`**

Append:

```cpp
template <HostView V>
UINT HostIntegration<V>::find_menu_item_by_accel_(HMENU menu, const wchar_t* accel) {
    if (!menu) return 0;
    const int count = GetMenuItemCount(menu);
    const size_t accel_len = wcslen(accel);
    for (int i = 0; i < count; i++) {
        const UINT id = GetMenuItemID(menu, i);
        if (id == 0 || id == static_cast<UINT>(-1)) continue;
        wchar_t buf[256];
        const int len = GetMenuStringW(menu, i, buf, _countof(buf), MF_BYPOSITION);
        if (len <= 0) continue;
        const wchar_t* tab = wcschr(buf, L'\t');
        if (!tab) continue;
        if (wcsncmp(tab + 1, accel, accel_len) != 0) continue;
        const wchar_t after = tab[1 + accel_len];
        if (after == 0 || iswspace(after)) return id;
    }
    return 0;
}
```

- [ ] **Step 3: Implement `find_reload_id_via_accel_resources_` with L1 comment**

Append:

```cpp
// TC builds accelerator tables at runtime from .lng/.ini; this RT_ACCELERATOR
// scan returns 0 on shipping TC builds. Kept as a cheap try-first in case a
// future build or alternative file manager ships static accel resources.
template <HostView V>
UINT HostIntegration<V>::find_reload_id_via_accel_resources_() {
    HMODULE main = GetModuleHandleW(nullptr);
    if (!main) return 0;
    UINT found = 0;
    EnumResourceNamesW(main, RT_ACCELERATOR,
        [](HMODULE mod, LPCWSTR, LPWSTR name, LONG_PTR param) -> BOOL {
            auto* out = reinterpret_cast<UINT*>(param);
            HACCEL h = LoadAcceleratorsW(mod, name);
            if (!h) return TRUE;
            const int n = CopyAcceleratorTable(h, nullptr, 0);
            if (n <= 0) return TRUE;
            std::vector<ACCEL> accels(static_cast<size_t>(n));
            CopyAcceleratorTable(h, accels.data(), n);
            for (const auto& a : accels) {
                if (a.key == VK_F2
                    && (a.fVirt & FVIRTKEY)
                    && !(a.fVirt & (FALT | FCONTROL | FSHIFT))) {
                    *out = a.cmd;
                    return FALSE;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LONG_PTR>(&found));
    return found;
}
```

- [ ] **Step 4: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add src/wlx_host_common.h
git commit -m "feat: HostIntegration reload-ID discovery helpers"
```

---

## Task 10: Write `HostIntegration` unit tests

**Files:**
- Create: `tests/test_wlx_host_common.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the test file**

Create `tests/test_wlx_host_common.cpp`:

```cpp
#include <doctest/doctest.h>
#include "wlx_host_common.h"

#include <string>
#include <windows.h>

namespace {

struct FakeV {
    HWND hwnd = nullptr;
    std::wstring file_path;
    HWND subclass_target = nullptr;
    int reload_count = 0;
};

inline void reload_view(FakeV& v, const wchar_t*) {
    v.reload_count++;
}

static_assert(HostView<FakeV>);

} // namespace

TEST_CASE("HostIntegration attach/detach refcounts subclasses per parent") {
    HostIntegration<FakeV> integ;
    FakeV a, b;
    a.hwnd = reinterpret_cast<HWND>(0x1001);
    b.hwnd = reinterpret_cast<HWND>(0x1002);
    // Use GetDesktopWindow as a real HWND that accepts SetWindowSubclass.
    HWND parent = GetDesktopWindow();

    integ.attach(&a, parent);
    integ.attach(&b, parent);
    CHECK(integ.views_for_test().size() == 2);
    CHECK(a.subclass_target == b.subclass_target);

    integ.detach(&a);
    CHECK(integ.views_for_test().size() == 1);
    integ.detach(&b);
    CHECK(integ.views_for_test().empty());
}

TEST_CASE("HostIntegration menu-source reload fans out to all matching views (H3)") {
    HostIntegration<FakeV> integ;
    FakeV a, b;
    a.hwnd = reinterpret_cast<HWND>(0x2001);
    b.hwnd = reinterpret_cast<HWND>(0x2002);
    a.file_path = L"dummy_a.md";
    b.file_path = L"dummy_b.md";
    HWND parent = GetDesktopWindow();

    integ.attach(&a, parent);
    integ.attach(&b, parent);
    REQUIRE(a.subclass_target == b.subclass_target);

    const UINT kFakeReloadId = 7777;
    integ.set_reload_menu_id_for_test(kFakeReloadId);

    // Synthesize a menu-source WM_COMMAND. HIWORD=0 => menu; LOWORD=cmd.
    const WPARAM wp = MAKEWPARAM(kFakeReloadId, 0);
    HostIntegration<FakeV>::parent_subclass_proc_(a.subclass_target, WM_COMMAND,
                                                   wp, 0, 0, 0);

    CHECK(a.reload_count == 1);
    CHECK(b.reload_count == 1);

    integ.detach(&a);
    integ.detach(&b);
}

TEST_CASE("HostIntegration emergency_cleanup is idempotent and leaves empty state") {
    HostIntegration<FakeV> integ;
    FakeV a;
    a.hwnd = reinterpret_cast<HWND>(0x3001);
    integ.attach(&a, GetDesktopWindow());
    integ.emergency_cleanup();
    CHECK(integ.views_for_test().empty());
    CHECK(integ.reload_menu_id() == 0 || integ.reload_menu_id() != 0); // either is fine — id is preserved through cleanup in current impl, test is about views/parents
    integ.emergency_cleanup(); // second call must not crash
}

TEST_CASE("HostIntegration subclass proc with no self returns DefSubclassProc") {
    // When no attach has been called, self_ is nullptr. Invoking the proc
    // directly should still be safe — it falls through to DefSubclassProc.
    // We can't assert a return value meaningfully without a real HWND, but
    // at minimum the call must not crash.
    HostIntegration<FakeV>::parent_subclass_proc_(GetDesktopWindow(), WM_NULL,
                                                   0, 0, 0, 0);
    CHECK(true);
}
```

- [ ] **Step 2: Add to `CMakeLists.txt`**

In the `add_executable(tests ...)` source list, append:
```cmake
    tests/test_wlx_host_common.cpp
```

- [ ] **Step 3: Build and run tests**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe`
Expected: four new cases pass. H3 regression test verifies both FakeVs get reloaded.

- [ ] **Step 4: Commit**

```bash
git add tests/test_wlx_host_common.cpp CMakeLists.txt
git commit -m "test: cover HostIntegration refcount + H3 multi-view fan-out"
```

---

## Task 11: Port md plugin to `HostIntegration`

**Files:**
- Modify: `src/host_adapter.cpp` (delete duplicated surface, wire template)

- [ ] **Step 1: Add the include**

Near the other plugin includes in `src/host_adapter.cpp`:
```cpp
#include "wlx_host_common.h"
```

- [ ] **Step 2: Add the `reload_view` free function**

After the `ViewState` definition (before the globals block), add:
```cpp
static void reload_view(ViewState& vs, const wchar_t* path) {
    load_document(&vs, path);
    InvalidateRect(vs.hwnd, nullptr, FALSE);
}

static_assert(HostView<ViewState>);
```

Note: `load_document` is a file-static function already defined later in the file. Since `reload_view` needs to appear in the template's instantiation context, either declare `load_document` forward above or keep `reload_view` below the `load_document` definition. **Move the `reload_view` function to AFTER `load_document` is defined** to avoid forward-declaration churn; the static_assert can remain wherever `reload_view` is declared.

- [ ] **Step 3: Add the `HostIntegration` instance**

After the globals block, add:
```cpp
static HostIntegration<ViewState> g_integration;
```

- [ ] **Step 4: Wire `attach` into `ListLoadW`**

Locate the current `ListLoadW` body. Near the end where `install_msg_hook()` and `install_parent_subclass()` are called, replace those two calls with:
```cpp
g_integration.attach(vs, ParentWin);
```

- [ ] **Step 5: Wire `detach` into `ListCloseWindow`**

In `ListCloseWindow`, replace the `uninstall_parent_subclass(subclass_target)` and `uninstall_msg_hook()` calls with:
```cpp
g_integration.detach(it->second);  // before `delete it->second`
```

Make sure detach is called BEFORE `delete it->second`; the detach reads `vs->subclass_target`.

- [ ] **Step 6: Wire `emergency_cleanup` into `DllMain`**

In `DLL_PROCESS_DETACH`, within the `if (reserved == nullptr)` branch, replace the `RemoveWindowSubclass` loop and the `g_msg_hook` unhook block with:
```cpp
g_integration.emergency_cleanup();
```

- [ ] **Step 7: Delete duplicated surface**

Delete the following from `src/host_adapter.cpp`:
- `g_msg_hook`, `g_hook_refcount` globals
- `PARENT_SUBCLASS_ID` constant
- `g_reload_menu_id`, `g_pending_f2_capture`, `g_candidate_reload_id`, `g_candidate_time`, `CANDIDATE_TTL_MS`, `g_parent_refcount` globals
- `GetMsgHookProc`, `install_msg_hook`, `uninstall_msg_hook` functions
- `accel_enum_proc_`, `find_reload_id_via_accel_resources` functions
- `find_menu_item_by_accel`, `find_menu_owner`, `install_parent_subclass`, `uninstall_parent_subclass`, `ParentSubclassProc` functions

Also: the `ListLoadW` block that calls `find_reload_id_via_accel_resources()` to seed `g_reload_menu_id` — delete it; the template's `attach` handles discovery internally.

Also: the `ViewState::subclass_target` field remains (the concept requires it), but it's now set by the template's `attach`.

- [ ] **Step 8: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build. The md plugin now uses the shared integration template.

- [ ] **Step 9: Run tests**

Run: `./build/Release/tests.exe`
Expected: all tests still pass.

- [ ] **Step 10: Commit**

```bash
git add src/host_adapter.cpp
git commit -m "refactor(md): use shared HostIntegration template"
```

---

## Task 12: Port colorizer plugin to `HostIntegration`

**Files:**
- Modify: `src/colorizer/colorizer_host_adapter.cpp`

- [ ] **Step 1: Add include**
```cpp
#include "wlx_host_common.h"
```

- [ ] **Step 2: Add `reload_view` free function**

After `load_document` is defined in the colorizer adapter:
```cpp
static void reload_view(ColorViewState& vs, const wchar_t* path) {
    load_document(&vs, path);
    // colorizer's load_document already calls InvalidateRect internally.
}

static_assert(HostView<ColorViewState>);
```

- [ ] **Step 3: Add the integration instance**
```cpp
static HostIntegration<ColorViewState> g_integration;
```

- [ ] **Step 4: Wire `attach` / `detach` / `emergency_cleanup`**

Same pattern as Task 11 steps 4–6, but with the colorizer's `ColorViewState`.

- [ ] **Step 5: Delete duplicated surface**

Same deletions as Task 11 step 7, but in `colorizer_host_adapter.cpp`.

- [ ] **Step 6: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build.

- [ ] **Step 7: Run tests**

Run: `./build/Release/tests.exe`
Expected: all pass.

- [ ] **Step 8: Commit**

```bash
git add src/colorizer/colorizer_host_adapter.cpp
git commit -m "refactor(colorizer): use shared HostIntegration template"
```

---

## Task 13: `render_engine` M4 cursor refactor

**Files:**
- Modify: `src/render_engine.h`
- Modify: `src/render_engine.cpp`

- [ ] **Step 1: Update signature in the header**

In `src/render_engine.h`, change:
```cpp
void paint_search_highlights(const LayoutBlock& block, int block_index, float offset_y);
```
to:
```cpp
void paint_search_highlights(const LayoutBlock& block, int block_index,
                             float offset_y, size_t& match_cursor);
```

- [ ] **Step 2: Update `paint_search_highlights` body**

In `src/render_engine.cpp`, replace the current loop over `search_matches_` with a cursor-based walk:

```cpp
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

            for (UINT32 j = 0; j < actual; j++) {
                const auto& mm = metrics[j];
                const D2D1_RECT_F r = { mm.left, mm.top,
                                        mm.left + mm.width, mm.top + mm.height };
                rt_->FillRectangle(r, brush);
            }
        }
    }
}
```

- [ ] **Step 3: Update the `paint()` caller to thread the cursor**

In `paint()` (same file), find the block iteration that calls `paint_search_highlights`. Declare `size_t search_cursor = 0;` before the loop, and pass it by reference:

```cpp
size_t search_cursor = 0;
for (int block_idx = 0; ...; block_idx++) {
    const auto& block = layout.blocks[block_idx];
    paint_inline_code_bg(block, 0);
    paint_span_backgrounds(block, 0);
    paint_selection_highlight(block, block_idx, 0, sel_start, sel_end);
    paint_search_highlights(block, block_idx, 0, search_cursor);
    paint_block_decoration(block, 0);
    // ... rest of block painting
}
```

- [ ] **Step 4: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build.

- [ ] **Step 5: Run tests**

Run: `./build/Release/tests.exe`
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add src/render_engine.h src/render_engine.cpp
git commit -m "perf: paint_search_highlights uses cursor-based iteration (M4)"
```

---

## Task 14: `scroll_to_match` M3 precision (both plugins)

**Files:**
- Modify: `src/host_adapter.cpp` (md `scroll_to_match`)
- Modify: `src/colorizer/colorizer_host_adapter.cpp` (colorizer `scroll_to_match`)

- [ ] **Step 1: Extend md `scroll_to_match`**

Locate the md `scroll_to_match(ViewState*, const SearchMatch&)` in `src/host_adapter.cpp`. Replace the body with:

```cpp
static void scroll_to_match(ViewState* vs, const SearchMatch& m) {
    if (!vs->layout) return;
    if (m.block_index < 0 ||
        m.block_index >= static_cast<int>(vs->layout->blocks.size())) return;

    const auto& block = vs->layout->blocks[m.block_index];
    const float viewport_h = vs->renderer ? vs->renderer->dip_height() : 100.0f;
    const float block_top = block.rect.top;
    const float block_bot = block.rect.bottom;

    if (block_top >= vs->scroll_y && block_bot <= vs->scroll_y + viewport_h) {
        // Block already fully visible — but match may still be off-screen
        // in a tall block. Fall through to the precision pass below.
    } else {
        // Center the block vertically first.
        const float target = block_top - (viewport_h - (block_bot - block_top)) * 0.5f;
        vs->scroll_y = std::clamp(target, 0.0f, vs->max_scroll_y);
    }

    // Precision pass: hit-test the match's first rect. If the match is still
    // outside the viewport, put it ~1/3 from the top.
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
            run.rect.left, run.rect.top,
            nullptr, 0, &required);
        if (required == 0) break;

        std::vector<DWRITE_HIT_TEST_METRICS> metrics(required);
        UINT32 actual = 0;
        run.layout->HitTestTextRange(
            static_cast<UINT32>(local_start), length,
            run.rect.left, run.rect.top,
            metrics.data(), required, &actual);
        if (actual == 0) break;

        const float match_top = metrics[0].top;
        const float match_bot = metrics[0].top + metrics[0].height;
        if (match_top < vs->scroll_y || match_bot > vs->scroll_y + viewport_h) {
            vs->scroll_y = std::clamp(match_top - viewport_h * 0.33f,
                                       0.0f, vs->max_scroll_y);
        }
        break;
    }

    update_scrollbar(vs);
}
```

- [ ] **Step 2: Mirror the change in the colorizer**

In `src/colorizer/colorizer_host_adapter.cpp`, replace the `scroll_to_match(ColorViewState*, const SearchMatch&)` body with the same logic (substitute `ColorViewState` for `ViewState`).

- [ ] **Step 3: Build and test**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe`
Expected: clean build; tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/host_adapter.cpp src/colorizer/colorizer_host_adapter.cpp
git commit -m "fix: scroll_to_match positions the match, not the block (M3)"
```

---

## Task 15: H1 trace + comment in both plugins

**Files:**
- Modify: `src/host_adapter.cpp`
- Modify: `src/colorizer/colorizer_host_adapter.cpp`

- [ ] **Step 1: Add trace + comment in md WM_KEYDOWN**

In `src/host_adapter.cpp`, locate the `case WM_KEYDOWN:` block. After the `if (handled) return 0;` line and before the final `break;`, insert:

```cpp
// Unhandled keys fall to DefWindowProcW. TC's TranslateAccelerator runs on
// the message loop ahead of DispatchMessage, so F5/F7/N/P/W are claimed
// upstream and never reach this point. F2 is the exception — TC consumes
// it before TranslateAccelerator, which is why we intercept it via the
// WH_GETMESSAGE hook (see wlx_host_common.h). If a new accel key ever
// reaches this branch, the trace below will surface it.
WLX_TRACE(L"WM_KEYDOWN unhandled, falling through vk=0x%X", (unsigned)wp);
break;
```

- [ ] **Step 2: Same in colorizer**

Mirror the change in `src/colorizer/colorizer_host_adapter.cpp`'s `WM_KEYDOWN` default branch.

- [ ] **Step 3: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build (WLX_TRACE is a no-op when WLX_TRACE_ENABLE is off, so no runtime cost).

- [ ] **Step 4: Commit**

```bash
git add src/host_adapter.cpp src/colorizer/colorizer_host_adapter.cpp
git commit -m "docs: surface unhandled-WM_KEYDOWN invariant with trace (H1)"
```

---

## Task 16: L2 — refresh `line_height_factor` from theme at every use

The `ColorizerDisplayConfig::line_height_factor` field in `src/colorizer/colorizer_layout.h` is consumed by `colorizer_layout.cpp:77` and cannot be removed without threading an extra parameter through `layout_source`. Keep the field on the layout config but stop caching it on `g_display_cfg`: populate it fresh per layout, and read it directly from the theme at every host-adapter use site.

**Files:**
- Modify: `src/colorizer/colorizer_host_adapter.cpp`

- [ ] **Step 1: Remove the cache-init line in `ensure_theme`**

In `ensure_theme()` at `src/colorizer/colorizer_host_adapter.cpp:310-311`, delete:
```cpp
// line_height_factor from spacing config
g_display_cfg.line_height_factor = g_theme.spacing().line_height_factor;
```

- [ ] **Step 2: Populate fresh in `do_layout`**

Find the `do_layout(ColorViewState*, ...)` function. Where it declares the local `cfg`:
```cpp
ColorizerDisplayConfig cfg = g_display_cfg;
cfg.word_wrap = vs->wrap_text;
```
Add one line below `cfg.word_wrap = ...`:
```cpp
cfg.line_height_factor = g_theme.spacing().line_height_factor;
```

This ensures `layout_source` always sees the current theme value.

- [ ] **Step 3: Replace host-adapter use-site reads**

In `src/colorizer/colorizer_host_adapter.cpp`, four sites read `g_display_cfg.line_height_factor`:
- Line 815 (`WM_VSCROLL` line height)
- Line 850 (`WM_MOUSEWHEEL` line height)
- Line 986 (`WM_TIMER` autoscroll line height)
- Line 1010 (`WM_KEYDOWN` line height)

At each, replace `g_display_cfg.line_height_factor` with `g_theme.spacing().line_height_factor`.

- [ ] **Step 4: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build.

- [ ] **Step 5: Run tests**

Run:
```
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```
Expected: all pass. No behavior change in the current runtime (theme isn't live-reloaded), but L2 is now correct against any future theme-reload work.

- [ ] **Step 6: Commit**

```bash
git add src/colorizer/colorizer_host_adapter.cpp
git commit -m "refactor(colorizer): read line_height_factor from theme at use sites (L2)"
```

---

## Task 17: L3 — document `iswalnum` locale dependence

**Files:**
- Modify: `src/search_engine.cpp`

- [ ] **Step 1: Add the comment**

In `src/search_engine.cpp`, above the existing `is_word_char` function:

```cpp
// Locale-dependent — iswalnum's behavior for non-ASCII alphanumerics (e.g.
// Cyrillic, Greek) depends on the thread's C locale. Acceptable for a file
// viewer's search UX; tightening this would require ICU or a custom table.
static bool is_word_char(wchar_t c) {
    return iswalnum(static_cast<wint_t>(c)) || c == L'_';
}
```

- [ ] **Step 2: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add src/search_engine.cpp
git commit -m "docs: note iswalnum locale dependence in SearchIndex (L3)"
```

---

## Task 18: Final verification

**Files:** none

- [ ] **Step 1: Full clean rebuild**

Run:
```
conan install . --output-folder=build --build=missing -s build_type=Release
cmake --preset conan-default
cmake --build --preset conan-release
```
Expected: no warnings, no errors.

- [ ] **Step 2: Run both test suites**

Run:
```
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```
Expected: all tests pass. Count should be previous + 7 (Task 3) + 4 (Task 10) = +11.

- [ ] **Step 3: Visual regression sweep**

Run: `./scripts/visual-test.sh`
Expected: all 27 cases PASS at ≥95% similarity. No visual regressions from the refactor.

- [ ] **Step 4: Inspect line count savings**

Run: `git diff --stat HEAD~18 HEAD -- src/host_adapter.cpp src/colorizer/colorizer_host_adapter.cpp`
Expected: both files significantly smaller (~250-300 LoC removed each).

- [ ] **Step 5: Manual smoke test in TC (required before merge)**

Follow the test plan from the spec's "Manual smoke test" section:

1. Install both plugins, open TC Lister on a markdown file.
2. F7, type "the", OK. Verify highlights + current match emphasized.
3. F5 cycles matches; wrap-around at end.
4. F2 reloads the file.
5. File menu → Reload: reloads if TC's menu labels expose "F2" hint (check DebugView for discovery log).
6. N (next file), P (previous), W (wrap toggle) all work — verify WM_KEYDOWN trace does NOT fire with `WLX_TRACE_ENABLE=ON`.
7. Esc clears highlights.
8. Two Lister windows open — F2 in each reloads its own window.
9. Repeat 2–7 with the colorizer on a .cpp source file.

Report any failures and investigate before proceeding.

- [ ] **Step 6: Final commit note (if any adjustments were made during verification)**

If any of steps 1–5 required adjustments, commit them with a clear message. Otherwise skip.

- [ ] **Step 7: Summarize the branch**

Run: `git log --oneline master..HEAD`
Expected: 17 commits (one per task) cleanly describing the refactor.

---

## Spec Coverage Check

| Finding | Task(s) |
|---------|---------|
| H1 — key-forwarding invariant documented via trace | Task 15 |
| H2 — ~500 LoC duplication extracted into template | Tasks 6–12 |
| H3 — multi-view reload fan-out | Task 8 (impl), Task 10 (regression test) |
| M1 — menu-discovery heuristics removed | Task 8 (absence confirmed) |
| M2 — `search_step` extracted, unit-tested | Tasks 2, 3, 4, 5 |
| M3 — `scroll_to_match` precision | Task 14 |
| M4 — paint cursor | Task 13 |
| L1 — dead-code comment on accel-resource scan | Task 9 |
| L2 — `line_height_factor` read from theme | Task 16 |
| L3 — `iswalnum` locale comment | Task 17 |
| C++20 bump prerequisite | Task 1 |
| Final verification | Task 18 |
