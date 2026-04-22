# Lister Integration Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the md and colorizer WLX plugins respond to Lister's standard keyboard shortcuts (F2/F3/F5/F7/N/P/W) and implement `ListSearchTextW` with multi-match highlighting. Also makes the colorizer honor `lcp_wraptext`.

**Architecture:** New `SearchIndex` class in the shared `wlx-core` static lib flattens a `LayoutDocument` into a single UTF-16 string with a block-offset map, then serves `find_all(SearchQuery)` results. Both plugins: (1) forward unhandled `WM_KEYDOWN`/`WM_CHAR`/`WM_SYSKEYDOWN` to their parent so Lister's accelerators fire; (2) export `ListSearchTextW` which rebuilds the index on dirty, runs the query, advances a cursor, scrolls the match into view, and pushes match lists into `RenderEngine` for highlighted painting.

**Tech Stack:** C++17, Win32 (WLX plugin API), Direct2D/DirectWrite, tomlplusplus, doctest, CMake + Conan 2.x.

**Reference spec:** `docs/superpowers/specs/2026-04-22-lister-parity-design.md`

---

## File map

**Create:**
- `src/search_engine.h` — `SearchMatch`, `SearchQuery`, `SearchIndex` declarations
- `src/search_engine.cpp` — flattening, case-fold cache, `find_all` implementation
- `tests/test_search_engine.cpp` — doctest suite for SearchIndex
- `test_data/cases/28_search_highlight/` — visual regression case (md)
- `test_data/cases/29_colorizer_search_highlight/` — visual regression case (colorizer) (optional; skip if screenshot_tool doesn't support colorizer plugin)

**Modify:**
- `CMakeLists.txt` — add `search_engine.cpp` to `wlx-core`, `test_search_engine.cpp` to `tests`
- `src/plugin.def` — add `ListSearchTextW` export
- `src/colorizer/colorizer_plugin.def` — add `ListSearchTextW` export
- `src/host_adapter.cpp` — key forwarding, `ListSearchTextW`, search state, Esc-clears-matches, scroll-to-match
- `src/colorizer/colorizer_host_adapter.cpp` — same as md host_adapter + `lcp_wraptext` support
- `src/render_engine.h` / `src/render_engine.cpp` — `set_search_matches()`, `paint_search_highlights()`
- `src/theme_service.h` — add `search_highlight`, `search_highlight_current` to `ColorPalette`
- `src/theme_service.cpp` — parse new TOML keys, provide sensible defaults
- `config/wlx-listerine-md.toml` — add `search_highlight` and `search_highlight_current` under `[colors.light]` and `[colors.dark]`
- `config/wlx-listerine-colorizer.toml` — same
- `config/themes/default.toml` / `config/themes/default_light.toml` — add `ui.search.highlight` and `ui.search.highlight.current` scopes (so colorizer themes resolve consistently with md palette)

---

## Phase 1 — SearchIndex (TDD, pure C++ in wlx-core)

### Task 1: Create search_engine.h with type declarations

**Files:**
- Create: `src/search_engine.h`

- [ ] **Step 1: Write header**

```cpp
// src/search_engine.h
#pragma once

#include "layout_engine.h"

#include <string>
#include <vector>

struct SearchMatch {
    int block_index = -1;
    int char_start = 0;  // offset into block's flattened text (UTF-16 code units)
    int char_end = 0;
};

struct SearchQuery {
    std::wstring needle;
    bool match_case = false;
    bool whole_words = false;
    bool backwards = false;  // cursor-advancement hint; not used by find_all
};

inline bool operator==(const SearchQuery& a, const SearchQuery& b) {
    return a.needle == b.needle
        && a.match_case == b.match_case
        && a.whole_words == b.whole_words;
    // `backwards` intentionally excluded — it controls advancement, not results
}
inline bool operator!=(const SearchQuery& a, const SearchQuery& b) { return !(a == b); }

class SearchIndex {
public:
    void build(const LayoutDocument& layout);
    std::vector<SearchMatch> find_all(const SearchQuery& q) const;
    bool empty() const { return flat_.empty(); }

private:
    std::wstring flat_;               // concatenated block text, '\n' between blocks
    std::wstring flat_lower_;         // cached case-folded copy for case-insensitive search
    std::vector<int> block_starts_;   // block_starts_[i] = offset in flat_ where block i begins
};
```

- [ ] **Step 2: Commit**

```bash
git add src/search_engine.h
git commit -m "feat: add SearchIndex/SearchQuery/SearchMatch declarations"
```

---

### Task 2: Write failing test for basic single-match find

**Files:**
- Create: `tests/test_search_engine.cpp`

- [ ] **Step 1: Create test file with helper + first test**

```cpp
// tests/test_search_engine.cpp
#include <doctest/doctest.h>
#include "search_engine.h"
#include "layout_engine.h"

// Build a test LayoutDocument from a vector of block-text strings.
// No real DWrite — text_runs[0].text carries the content, other fields empty.
static LayoutDocument make_layout(std::initializer_list<std::wstring> block_texts) {
    LayoutDocument doc;
    for (auto& t : block_texts) {
        LayoutBlock b;
        TextRun r;
        r.text = t;
        b.text_runs.push_back(std::move(r));
        doc.blocks.push_back(std::move(b));
    }
    return doc;
}

TEST_CASE("SearchIndex finds a single match in one block") {
    auto doc = make_layout({L"hello world"});
    SearchIndex idx;
    idx.build(doc);

    SearchQuery q;
    q.needle = L"world";

    auto matches = idx.find_all(q);
    REQUIRE(matches.size() == 1);
    CHECK(matches[0].block_index == 0);
    CHECK(matches[0].char_start == 6);
    CHECK(matches[0].char_end == 11);
}
```

- [ ] **Step 2: Add to CMake**

Edit `CMakeLists.txt` at the `tests` target, add the file to the list:

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
    tests/test_search_engine.cpp   # <-- ADD
)
```

- [ ] **Step 3: Build, expect link failure**

Run:
```bash
cmake --build --preset conan-release --target tests 2>&1 | tail -20
```

Expected: link error on `SearchIndex::build` / `SearchIndex::find_all` — the header exists but no .cpp yet.

---

### Task 3: Implement SearchIndex (minimal to pass Task 2 test)

**Files:**
- Create: `src/search_engine.cpp`
- Modify: `CMakeLists.txt` (add to wlx-core)

- [ ] **Step 1: Write implementation**

```cpp
// src/search_engine.cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "search_engine.h"

#include <windows.h>
#include <algorithm>
#include <cwctype>

static std::wstring to_lower(std::wstring s) {
    if (!s.empty())
        CharLowerBuffW(&s[0], static_cast<DWORD>(s.size()));
    return s;
}

static bool is_word_char(wchar_t c) {
    return iswalnum(static_cast<wint_t>(c)) || c == L'_';
}

void SearchIndex::build(const LayoutDocument& layout) {
    flat_.clear();
    flat_lower_.clear();
    block_starts_.clear();
    block_starts_.reserve(layout.blocks.size());

    for (size_t i = 0; i < layout.blocks.size(); i++) {
        if (i > 0) flat_.push_back(L'\n');
        block_starts_.push_back(static_cast<int>(flat_.size()));
        for (const auto& run : layout.blocks[i].text_runs) {
            flat_ += run.text;
        }
    }
    flat_lower_ = to_lower(flat_);
}

std::vector<SearchMatch> SearchIndex::find_all(const SearchQuery& q) const {
    std::vector<SearchMatch> out;
    if (q.needle.empty() || flat_.empty()) return out;
    if (q.needle.size() > flat_.size()) return out;

    const std::wstring& hay = q.match_case ? flat_ : flat_lower_;
    const std::wstring ndl = q.match_case ? q.needle : to_lower(q.needle);

    size_t pos = 0;
    while ((pos = hay.find(ndl, pos)) != std::wstring::npos) {
        const size_t end = pos + ndl.size();

        // Reject matches that cross a block boundary (the '\n' separator).
        auto it = std::upper_bound(block_starts_.begin(), block_starts_.end(),
                                   static_cast<int>(pos));
        const int bi = static_cast<int>(it - block_starts_.begin()) - 1;
        const int local_start = static_cast<int>(pos) - block_starts_[bi];
        const int local_end   = static_cast<int>(end) - block_starts_[bi];

        bool crosses_boundary = false;
        if (bi + 1 < static_cast<int>(block_starts_.size())) {
            const int next_block_start = block_starts_[bi + 1];
            // next_block_start - 1 is the '\n' index; match must end before it
            if (static_cast<int>(end) > next_block_start - 1)
                crosses_boundary = true;
        }

        bool ok = !crosses_boundary;
        if (ok && q.whole_words) {
            if (pos > 0 && is_word_char(flat_[pos - 1])) ok = false;
            if (end < flat_.size() && is_word_char(flat_[end])) ok = false;
        }

        if (ok) {
            out.push_back({bi, local_start, local_end});
            pos = end;  // non-overlapping
        } else {
            pos = pos + 1;
        }
    }
    return out;
}
```

- [ ] **Step 2: Add to CMake**

Edit `CMakeLists.txt` at the `wlx-core` target, add `src/search_engine.cpp`:

```cmake
add_library(wlx-core STATIC
    src/file_service.cpp
    src/markdown_parser.cpp
    src/layout_engine.cpp
    src/render_engine.cpp
    src/interaction_engine.cpp
    src/theme_service.cpp
    src/cache_service.cpp
    src/text_selection.cpp
    src/search_engine.cpp   # <-- ADD
)
```

- [ ] **Step 3: Build and run test**

Run:
```bash
cmake --build --preset conan-release --target tests && ./build/Release/tests.exe --test-case="*single match*"
```

Expected: `1 test case passed`.

- [ ] **Step 4: Commit**

```bash
git add src/search_engine.cpp CMakeLists.txt tests/test_search_engine.cpp
git commit -m "feat: implement SearchIndex with flatten + find_all"
```

---

### Task 4: Add tests for all semantic cases

**Files:**
- Modify: `tests/test_search_engine.cpp`

- [ ] **Step 1: Add test cases**

Append to `tests/test_search_engine.cpp`:

```cpp
TEST_CASE("SearchIndex empty document returns no matches") {
    LayoutDocument doc;
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"x";
    CHECK(idx.find_all(q).empty());
    CHECK(idx.empty());
}

TEST_CASE("SearchIndex empty needle returns no matches") {
    auto doc = make_layout({L"hello"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;  // needle defaults to empty
    CHECK(idx.find_all(q).empty());
}

TEST_CASE("SearchIndex needle longer than haystack returns no matches") {
    auto doc = make_layout({L"hi"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"hello";
    CHECK(idx.find_all(q).empty());
}

TEST_CASE("SearchIndex case-insensitive by default") {
    auto doc = make_layout({L"Hello World"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"hello";
    auto m = idx.find_all(q);
    REQUIRE(m.size() == 1);
    CHECK(m[0].char_start == 0);
    CHECK(m[0].char_end == 5);
}

TEST_CASE("SearchIndex case-sensitive when match_case") {
    auto doc = make_layout({L"Hello World"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"hello";
    q.match_case = true;
    CHECK(idx.find_all(q).empty());

    q.needle = L"Hello";
    auto m = idx.find_all(q);
    REQUIRE(m.size() == 1);
    CHECK(m[0].char_start == 0);
}

TEST_CASE("SearchIndex whole_words matches only at word boundaries") {
    auto doc = make_layout({L"cat catalogue cat_1 cats cat."});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"cat";
    q.whole_words = true;
    auto m = idx.find_all(q);
    // Expected hits: "cat" at 0, "cat" at 25 (before the dot, a non-word-char).
    // Rejected: "catalogue" (letter after), "cat_1" (underscore counts as word-char), "cats" (letter after).
    REQUIRE(m.size() == 2);
    CHECK(m[0].char_start == 0);
    CHECK(m[1].char_start == 25);
}

TEST_CASE("SearchIndex returns document-order matches across blocks") {
    auto doc = make_layout({L"foo bar", L"baz foo qux", L"foo"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"foo";
    auto m = idx.find_all(q);
    REQUIRE(m.size() == 3);
    CHECK(m[0].block_index == 0);
    CHECK(m[0].char_start == 0);
    CHECK(m[1].block_index == 1);
    CHECK(m[1].char_start == 4);
    CHECK(m[2].block_index == 2);
    CHECK(m[2].char_start == 0);
}

TEST_CASE("SearchIndex never matches across block boundaries") {
    // "foo\nbar" as two blocks — needle "oo\nba" would match in flat_ but must be rejected
    auto doc = make_layout({L"foo", L"bar"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"oo\nba";
    CHECK(idx.find_all(q).empty());
}

TEST_CASE("SearchIndex non-overlapping matches") {
    auto doc = make_layout({L"aaaa"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"aa";
    auto m = idx.find_all(q);
    REQUIRE(m.size() == 2);
    CHECK(m[0].char_start == 0);
    CHECK(m[1].char_start == 2);
}

TEST_CASE("SearchIndex empty text_runs block doesn't shift offsets") {
    LayoutDocument doc;
    doc.blocks.resize(3);
    TextRun r0; r0.text = L"first";
    doc.blocks[0].text_runs.push_back(r0);
    // doc.blocks[1] intentionally has no text_runs (e.g., horizontal rule)
    TextRun r2; r2.text = L"third";
    doc.blocks[2].text_runs.push_back(r2);

    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"third";
    auto m = idx.find_all(q);
    REQUIRE(m.size() == 1);
    CHECK(m[0].block_index == 2);
    CHECK(m[0].char_start == 0);
}
```

- [ ] **Step 2: Build and run all search tests**

Run:
```bash
cmake --build --preset conan-release --target tests && ./build/Release/tests.exe --test-case="SearchIndex*"
```

Expected: all 10 test cases pass.

- [ ] **Step 3: Commit**

```bash
git add tests/test_search_engine.cpp
git commit -m "test: cover SearchIndex semantics (case, whole_words, boundaries)"
```

---

## Phase 2 — Theme colors

### Task 5: Add search_highlight / search_highlight_current to theme

**Files:**
- Modify: `src/theme_service.h`
- Modify: `src/theme_service.cpp`
- Modify: `config/wlx-listerine-md.toml`
- Modify: `config/wlx-listerine-colorizer.toml`

- [ ] **Step 1: Extend ColorPalette**

Edit `src/theme_service.h`, add two fields to `ColorPalette`:

```cpp
struct ColorPalette {
    uint32_t background;
    uint32_t text;
    uint32_t heading;
    uint32_t muted;
    uint32_t link;
    uint32_t link_hover;
    uint32_t code_bg;
    uint32_t quote_border;
    uint32_t rule;
    uint32_t selection;
    uint32_t search_highlight;          // <-- ADD (all-matches dim background)
    uint32_t search_highlight_current;  // <-- ADD (current-match strong background)
};
```

- [ ] **Step 2: Defaults + TOML parse**

Edit `src/theme_service.cpp`.

Add to `read_palette`:
```cpp
read("search_highlight",         pal.search_highlight);
read("search_highlight_current", pal.search_highlight_current);
```

Add to `default_config()` under light palette:
```cpp
cfg.light.search_highlight          = 0xFFE066;  // soft yellow
cfg.light.search_highlight_current  = 0xFFA500;  // orange — distinct from selection
```

And under dark palette:
```cpp
cfg.dark.search_highlight           = 0x5A4B00;  // muted amber
cfg.dark.search_highlight_current   = 0xC08400;  // stronger amber
```

- [ ] **Step 3: Add failing test for the new defaults**

Append to `tests/test_theme_service.cpp`:

```cpp
TEST_CASE("ThemeService defaults include search highlight colors") {
    ThemeService svc;
    CHECK(svc.palette(false).search_highlight != 0);
    CHECK(svc.palette(false).search_highlight_current != 0);
    CHECK(svc.palette(true).search_highlight != 0);
    CHECK(svc.palette(true).search_highlight_current != 0);
    // Current should differ from dim
    CHECK(svc.palette(false).search_highlight != svc.palette(false).search_highlight_current);
}
```

Run:
```bash
cmake --build --preset conan-release --target tests && ./build/Release/tests.exe --test-case="*search highlight*"
```

Expected: PASS (defaults were added in step 2; this just locks them in).

- [ ] **Step 4: Update md TOML config**

Edit `config/wlx-listerine-md.toml`. Under each of `[colors.light]` and `[colors.dark]`, add the two new keys with commented hex defaults. Find the existing `selection = "..."` line and add immediately after:

```toml
search_highlight = "#FFE066"           # all-matches dim
search_highlight_current = "#FFA500"   # current match emphasized
```

(For `[colors.dark]` use `"#5A4B00"` and `"#C08400"` respectively.)

- [ ] **Step 5: Update colorizer TOML config**

Edit `config/wlx-listerine-colorizer.toml` the same way.

- [ ] **Step 6: Commit**

```bash
git add src/theme_service.h src/theme_service.cpp tests/test_theme_service.cpp \
    config/wlx-listerine-md.toml config/wlx-listerine-colorizer.toml
git commit -m "feat: add search_highlight palette entries to theme schema"
```

---

### Task 6: Add ui.search.highlight scopes to Helix themes

**Files:**
- Modify: `config/themes/default.toml`
- Modify: `config/themes/default_light.toml`

- [ ] **Step 1: Inspect current theme structure**

Read `config/themes/default.toml` to understand scope key format (likely `"ui.selection" = { bg = "..." }` style).

Run:
```bash
grep -n "ui.selection\|palette" config/themes/default.toml | head -20
```

- [ ] **Step 2: Add scopes to default.toml (dark)**

Add these lines alongside existing `ui.selection`:

```toml
"ui.highlight.current" = { bg = "amber_strong" }
"ui.highlight" = { bg = "amber_dim" }
```

And under `[palette]`:
```toml
amber_dim = "#5A4B00"
amber_strong = "#C08400"
```

Use scope names `ui.highlight` / `ui.highlight.current` to match Helix's editor conventions. Note: Helix uses `ui.highlight` for search, not `ui.search.highlight`. Grep the existing themes to confirm; if `ui.highlight` is already used for something else, fall back to `ui.selection.primary` or introduce `ui.search`.

- [ ] **Step 3: Add same scopes to default_light.toml**

Use lighter palette values:

```toml
amber_dim = "#FFE066"
amber_strong = "#FFA500"
```

- [ ] **Step 4: Commit**

```bash
git add config/themes/default.toml config/themes/default_light.toml
git commit -m "feat: add ui.highlight scopes to Helix themes for search"
```

---

## Phase 3 — RenderEngine highlight painting

### Task 7: Add set_search_matches + paint_search_highlights

**Files:**
- Modify: `src/render_engine.h`
- Modify: `src/render_engine.cpp`

- [ ] **Step 1: Declare in header**

Edit `src/render_engine.h`. Add `#include "search_engine.h"` near existing includes. Add to `public:`:

```cpp
void set_search_matches(const std::vector<SearchMatch>& matches, int current_index);
```

Add to `private:`:
```cpp
void paint_search_highlights(const LayoutBlock& block, int block_index, float offset_y);

std::vector<SearchMatch> search_matches_;
int search_current_ = -1;
```

- [ ] **Step 2: Implement in cpp**

Edit `src/render_engine.cpp`.

Add the public setter (top-level, near other simple setters):
```cpp
void RenderEngine::set_search_matches(const std::vector<SearchMatch>& matches, int current_index) {
    search_matches_ = matches;
    search_current_ = current_index;
}
```

Add the paint helper (near `paint_selection_highlight`, same style):
```cpp
void RenderEngine::paint_search_highlights(const LayoutBlock& block, int block_index, float offset_y) {
    if (search_matches_.empty() || !rt_) return;

    const auto& pal = theme_.palette(dark_mode_);

    for (size_t i = 0; i < search_matches_.size(); i++) {
        const auto& m = search_matches_[i];
        if (m.block_index != block_index) continue;

        // Walk runs, find overlap with [m.char_start, m.char_end)
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

            // Two-call pattern: first for required buffer size.
            // offset_y is passed 0 from paint() — the scroll transform is
            // applied via rt_->SetTransform, matching paint_selection_highlight.
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

- [ ] **Step 3: Call paint_search_highlights from paint()**

In `RenderEngine::paint` (around line 211 of `src/render_engine.cpp`), the per-block loop uses `block_idx` as the index variable. Insert the new call between `paint_selection_highlight` and `paint_block_decoration` so selection draws first (selection wins on visual priority; highlights sit underneath):

```cpp
paint_span_backgrounds(block, 0);
paint_selection_highlight(block, block_idx, 0, sel_start, sel_end);
paint_search_highlights(block, block_idx, 0);   // <-- ADD
paint_block_decoration(block, 0);
```

Note `offset_y` is always `0` here — scroll is applied via `rt_->SetTransform` earlier in `paint`.

- [ ] **Step 4: Build**

Run:
```bash
cmake --build --preset conan-release --target wlx-listerine-md 2>&1 | tail -10
```

Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add src/render_engine.h src/render_engine.cpp
git commit -m "feat: paint search match highlights in RenderEngine"
```

---

## Phase 4 — Key forwarding (both plugins)

### Task 8: Forward unhandled keys in md host_adapter

**Files:**
- Modify: `src/host_adapter.cpp`

- [ ] **Step 1: Replace WM_KEYDOWN body**

Find the `case WM_KEYDOWN:` block in `src/host_adapter.cpp` (currently around line 595). Replace its body with:

```cpp
case WM_KEYDOWN: {
    if (!vs) break;
    float page = vs->renderer ? vs->renderer->dip_height() : 100.0f;
    float line = g_theme.fonts().body_size * g_theme.spacing().line_height_factor;

    bool handled = false;

    // Ctrl+C — copy selection
    if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (vs->layout && vs->sel_anchor.valid() && vs->sel_anchor != vs->sel_active) {
            auto lo = std::min(vs->sel_anchor, vs->sel_active);
            auto hi = std::max(vs->sel_anchor, vs->sel_active);
            auto text = extract_selected_text(*vs->layout, lo, hi);
            copy_to_clipboard(hwnd, text);
        }
        handled = true;
    }
    // Ctrl+A — select all
    else if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (vs->layout && !vs->layout->blocks.empty()) {
            vs->sel_anchor = TextPosition{0, 0};
            int last = static_cast<int>(vs->layout->blocks.size()) - 1;
            vs->sel_active = TextPosition{last, block_text_length(vs->layout->blocks[last])};
            vs->selecting = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        handled = true;
    }
    // Escape — clear selection; if no selection but active matches, clear them;
    // otherwise forward to parent (Lister uses Esc to close the window)
    else if (wp == VK_ESCAPE) {
        if (vs->sel_anchor.valid()) {
            clear_selection(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
            handled = true;
        } else if (!vs->matches.empty()) {
            vs->matches.clear();
            vs->current_match = -1;
            if (vs->renderer) vs->renderer->set_search_matches({}, -1);
            InvalidateRect(hwnd, nullptr, FALSE);
            handled = true;
        }
        // else fall through, forwarding to parent
    }
    else {
        switch (wp) {
        case VK_UP:    handle_scroll(vs, -line); handled = true; break;
        case VK_DOWN:  handle_scroll(vs,  line); handled = true; break;
        case VK_PRIOR: handle_scroll(vs, -page); handled = true; break;
        case VK_NEXT:  handle_scroll(vs,  page); handled = true; break;
        case VK_HOME:
            vs->scroll_y = 0;
            update_scrollbar(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
            handled = true;
            break;
        case VK_END:
            vs->scroll_y = vs->max_scroll_y;
            update_scrollbar(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
            handled = true;
            break;
        }
    }

    if (handled) return 0;

    HWND parent = GetParent(hwnd);
    if (parent) return SendMessageW(parent, msg, wp, lp);
    return 0;
}
```

Note: the references to `vs->matches`, `vs->current_match`, and `renderer->set_search_matches` rely on fields added in Task 10. Build will fail until Task 10 is done — which is fine; commit anyway and finish Phase 5.

Alternatively, add ViewState fields first (Task 10 step 1) before Task 8 step 2 below. See "Ordering note" at end of file.

- [ ] **Step 2: Add WM_CHAR / WM_SYSKEYDOWN forwarders**

Add these cases in the same WndProc, after `case WM_KEYDOWN:`:

```cpp
case WM_CHAR:
case WM_SYSKEYDOWN: {
    HWND parent = GetParent(hwnd);
    if (parent) return SendMessageW(parent, msg, wp, lp);
    return 0;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/host_adapter.cpp
git commit -m "fix: forward unhandled keys to Lister parent (md plugin)"
```

---

### Task 9: Forward unhandled keys in colorizer host_adapter

**Files:**
- Modify: `src/colorizer/colorizer_host_adapter.cpp`

- [ ] **Step 1: Mirror the md changes**

In `src/colorizer/colorizer_host_adapter.cpp`, apply exactly the same `WM_KEYDOWN` / `WM_CHAR` / `WM_SYSKEYDOWN` transformations from Task 8. The colorizer uses `g_theme.fonts().code_size * g_display_cfg.line_height_factor` for line height (see existing WM_MOUSEWHEEL handler) — use that instead of the md version's `body_size * spacing().line_height_factor`.

The colorizer does not yet have `vs->matches` / `vs->current_match` — those are added in Task 14. For now, **omit** the `else if (!vs->matches.empty())` branch inside the Escape handler in this task. Add it back in Task 14 when the ViewState fields exist.

- [ ] **Step 2: Commit**

```bash
git add src/colorizer/colorizer_host_adapter.cpp
git commit -m "fix: forward unhandled keys to Lister parent (colorizer plugin)"
```

---

## Phase 5 — ListSearchTextW (md plugin)

### Task 10: Add search state + export in md plugin

**Files:**
- Modify: `src/host_adapter.cpp`
- Modify: `src/plugin.def`

- [ ] **Step 1: Add include + ViewState fields**

At top of `src/host_adapter.cpp`, add:
```cpp
#include "search_engine.h"
```

In `struct ViewState`, add:
```cpp
SearchIndex search_index;
std::vector<SearchMatch> matches;
int current_match = -1;
SearchQuery last_query;
bool index_dirty = true;
```

- [ ] **Step 2: Mark index dirty on layout / load**

In `do_layout`, at the end, add:
```cpp
vs->index_dirty = true;
```

In `load_document`, at the top (after clearing scroll/selection), add:
```cpp
vs->matches.clear();
vs->current_match = -1;
vs->last_query = SearchQuery{};
vs->index_dirty = true;
```

- [ ] **Step 3: Add scroll-to-match helper**

Before `extern "C"`, add:
```cpp
static void scroll_to_match(ViewState* vs, const SearchMatch& m) {
    if (!vs->layout) return;
    if (m.block_index < 0 ||
        m.block_index >= static_cast<int>(vs->layout->blocks.size())) return;

    const auto& block = vs->layout->blocks[m.block_index];
    const float viewport_h = vs->renderer ? vs->renderer->dip_height() : 100.0f;
    const float block_top = block.rect.top;
    const float block_bot = block.rect.bottom;

    // Already visible? leave scroll alone
    if (block_top >= vs->scroll_y && block_bot <= vs->scroll_y + viewport_h) return;

    // Center the block in viewport
    const float target = block_top - (viewport_h - (block_bot - block_top)) * 0.5f;
    vs->scroll_y = std::clamp(target, 0.0f, vs->max_scroll_y);
    update_scrollbar(vs);
}
```

- [ ] **Step 4: Implement ListSearchTextW**

Inside `extern "C"`, add:
```cpp
int __stdcall ListSearchTextW(HWND ListWin, wchar_t* SearchString, int SearchParameter) {
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

    bool rebuilt = false;
    if (vs->index_dirty) {
        vs->search_index.build(*vs->layout);
        vs->index_dirty = false;
        rebuilt = true;
    }

    // Re-run find_all if the user started over, the layout changed, or flags/needle changed.
    const bool requery = findfirst || rebuilt || q != vs->last_query;
    if (requery) {
        vs->matches = vs->search_index.find_all(q);
        if (findfirst) {
            vs->current_match = -1;
        } else if (vs->current_match >= static_cast<int>(vs->matches.size())) {
            vs->current_match = static_cast<int>(vs->matches.size()) - 1;
        }
        vs->last_query = q;
    }

    if (vs->matches.empty()) {
        vs->current_match = -1;
        if (vs->renderer) vs->renderer->set_search_matches({}, -1);
        InvalidateRect(vs->hwnd, nullptr, FALSE);
        return LISTPLUGIN_ERROR;
    }

    const int n = static_cast<int>(vs->matches.size());
    if (q.backwards) {
        vs->current_match = (vs->current_match <= 0) ? n - 1 : vs->current_match - 1;
    } else {
        vs->current_match = (vs->current_match + 1) % n;
    }

    scroll_to_match(vs, vs->matches[vs->current_match]);
    if (vs->renderer) vs->renderer->set_search_matches(vs->matches, vs->current_match);
    InvalidateRect(vs->hwnd, nullptr, FALSE);
    return LISTPLUGIN_OK;
}
```

- [ ] **Step 5: Export in plugin.def**

Edit `src/plugin.def`:

```
LIBRARY "wlx-listerine-md"
EXPORTS
    ListLoadW
    ListLoadNextW
    ListCloseWindow
    ListGetDetectString
    ListSendCommand
    ListSetDefaultParams
    ListSearchTextW
```

- [ ] **Step 6: Build**

```bash
cmake --build --preset conan-release --target wlx-listerine-md 2>&1 | tail -10
```

Expected: clean build.

- [ ] **Step 7: Commit**

```bash
git add src/host_adapter.cpp src/plugin.def
git commit -m "feat: implement ListSearchTextW with multi-match highlight (md)"
```

---

## Phase 6 — colorizer parity (wrap + search)

### Task 11: colorizer honors lcp_wraptext

**Files:**
- Modify: `src/colorizer/colorizer_host_adapter.cpp`
- Modify: `src/colorizer/colorizer_layout.h`
- Modify: `src/colorizer/colorizer_layout.cpp`

- [ ] **Step 1: Inspect layout_source signature**

Read `src/colorizer/colorizer_layout.h` to find the declaration of `layout_source`. It currently takes a `ColorizerDisplayConfig` (which has `word_wrap`). Note the field.

- [ ] **Step 2: Add wrap_text to ColorViewState**

Edit `src/colorizer/colorizer_host_adapter.cpp`. In `struct ColorViewState`, add:
```cpp
bool wrap_text = false;
```

- [ ] **Step 3: Honor ShowFlags in ListLoadW / ListLoadNextW**

In `ListLoadW`, after reading `bool dark = (ShowFlags & lcp_darkmode) != 0;` add:
```cpp
bool wrap = (ShowFlags & lcp_wraptext) != 0;
```
and after `vs->dark_mode = dark;`:
```cpp
vs->wrap_text = wrap;
```

In `ListLoadNextW`, similarly:
```cpp
bool new_wrap = (ShowFlags & lcp_wraptext) != 0;
vs->wrap_text = new_wrap;
```
(Place near the `new_dark` block; relayout happens in `load_document` which calls `do_layout`.)

- [ ] **Step 4: Plumb into do_layout**

Modify `do_layout` in the colorizer host_adapter to copy `g_display_cfg` and override `word_wrap` from the view's state:

```cpp
static void do_layout(ColorViewState* vs, const std::wstring& text, const std::string& raw_utf8,
                      const ColorizeResult& colors) {
    if (!g_dwrite_factory) return;

    float viewport_width = vs->renderer ? vs->renderer->dip_width() : 1.0f;

    ColorizerDisplayConfig cfg = g_display_cfg;
    cfg.word_wrap = vs->wrap_text;   // ShowFlags beats TOML default

    auto layout = std::make_shared<LayoutDocument>(
        layout_source(g_dwrite_factory.Get(), text, raw_utf8,
                      colors, g_theme, vs->dark_mode, viewport_width, cfg));

    vs->layout = layout;
    update_scrollbar(vs);
}
```

**Caveat:** if the plugin is first loaded with no `lcp_wraptext` hint (e.g., before the user has ever used Ctrl+W), `wrap_text` is `false`, which disables whatever the TOML asked for. That's acceptable — Lister always sends ShowFlags, so the user's session setting will stick from load #2 onward. If this matters, the implementer can initialize `wrap_text = g_display_cfg.word_wrap` when ThemeService is loaded.

- [ ] **Step 5: Respond to lc_newparams**

In `ListSendCommand` -> `case lc_newparams:`, add a wrap branch parallel to `new_dark`:

```cpp
case lc_newparams: {
    bool new_dark = (Parameter & lcp_darkmode)  != 0;
    bool new_wrap = (Parameter & lcp_wraptext) != 0;
    bool changed = false;

    if (new_dark != vs->dark_mode) {
        vs->dark_mode = new_dark;
        vs->renderer->set_dark_mode(new_dark);
        apply_dark_mode(vs->hwnd, new_dark);
        // Re-colorize with new palette
        std::string language = ext_to_language(vs->file_path);
        if (language.empty()) language = filename_to_language(vs->file_path);
        vs->cached_colors = {};
        if (!language.empty() && g_colorizer && g_colorizer->supports(language)) {
            vs->cached_colors = g_colorizer->colorize(vs->cached_raw_utf8, language, vs->dark_mode);
        }
        changed = true;
    }
    if (new_wrap != vs->wrap_text) {
        vs->wrap_text = new_wrap;
        changed = true;
    }
    if (changed) {
        do_layout(vs, vs->cached_text, vs->cached_raw_utf8, vs->cached_colors);
        InvalidateRect(vs->hwnd, nullptr, FALSE);
    }
    return LISTPLUGIN_OK;
}
```

- [ ] **Step 6: Build**

```bash
cmake --build --preset conan-release --target wlx-listerine-colorizer 2>&1 | tail -10
```

Expected: clean build.

- [ ] **Step 7: Commit**

```bash
git add src/colorizer/colorizer_host_adapter.cpp
git commit -m "feat: colorizer honors lcp_wraptext from ShowFlags and lc_newparams"
```

---

### Task 12: Implement ListSearchTextW in colorizer

**Files:**
- Modify: `src/colorizer/colorizer_host_adapter.cpp`
- Modify: `src/colorizer/colorizer_plugin.def`

- [ ] **Step 1: Add include + ColorViewState fields**

At top of `src/colorizer/colorizer_host_adapter.cpp`:
```cpp
#include "search_engine.h"
```

In `struct ColorViewState`, add (same five fields as md plugin):
```cpp
SearchIndex search_index;
std::vector<SearchMatch> matches;
int current_match = -1;
SearchQuery last_query;
bool index_dirty = true;
```

- [ ] **Step 2: Mark index dirty on layout / load**

In `do_layout` end: `vs->index_dirty = true;`

In `load_document` (start, after `vs->scroll_y = 0;`):
```cpp
vs->matches.clear();
vs->current_match = -1;
vs->last_query = SearchQuery{};
vs->index_dirty = true;
```

- [ ] **Step 3: Add scroll_to_match helper**

Before `extern "C"`, add:
```cpp
static void scroll_to_match(ColorViewState* vs, const SearchMatch& m) {
    if (!vs->layout) return;
    if (m.block_index < 0 ||
        m.block_index >= static_cast<int>(vs->layout->blocks.size())) return;

    const auto& block = vs->layout->blocks[m.block_index];
    const float viewport_h = vs->renderer ? vs->renderer->dip_height() : 100.0f;
    const float block_top = block.rect.top;
    const float block_bot = block.rect.bottom;

    if (block_top >= vs->scroll_y && block_bot <= vs->scroll_y + viewport_h) return;

    const float target = block_top - (viewport_h - (block_bot - block_top)) * 0.5f;
    vs->scroll_y = std::clamp(target, 0.0f, vs->max_scroll_y);
    update_scrollbar(vs);
}
```

- [ ] **Step 4: Implement ListSearchTextW**

Inside `extern "C"`, add:
```cpp
int __stdcall ListSearchTextW(HWND ListWin, wchar_t* SearchString, int SearchParameter) {
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

    bool rebuilt = false;
    if (vs->index_dirty) {
        vs->search_index.build(*vs->layout);
        vs->index_dirty = false;
        rebuilt = true;
    }

    const bool requery = findfirst || rebuilt || q != vs->last_query;
    if (requery) {
        vs->matches = vs->search_index.find_all(q);
        if (findfirst) {
            vs->current_match = -1;
        } else if (vs->current_match >= static_cast<int>(vs->matches.size())) {
            vs->current_match = static_cast<int>(vs->matches.size()) - 1;
        }
        vs->last_query = q;
    }

    if (vs->matches.empty()) {
        vs->current_match = -1;
        if (vs->renderer) vs->renderer->set_search_matches({}, -1);
        InvalidateRect(vs->hwnd, nullptr, FALSE);
        return LISTPLUGIN_ERROR;
    }

    const int n = static_cast<int>(vs->matches.size());
    if (q.backwards) {
        vs->current_match = (vs->current_match <= 0) ? n - 1 : vs->current_match - 1;
    } else {
        vs->current_match = (vs->current_match + 1) % n;
    }

    scroll_to_match(vs, vs->matches[vs->current_match]);
    if (vs->renderer) vs->renderer->set_search_matches(vs->matches, vs->current_match);
    InvalidateRect(vs->hwnd, nullptr, FALSE);
    return LISTPLUGIN_OK;
}
```

- [ ] **Step 5: Add Esc-clears-matches branch to WM_KEYDOWN**

Edit the Escape handler inside `WM_KEYDOWN` (added in Task 9, which did not include this branch). Add:
```cpp
else if (wp == VK_ESCAPE) {
    if (vs->sel_anchor.valid()) {
        clear_selection(vs);
        InvalidateRect(hwnd, nullptr, FALSE);
        handled = true;
    } else if (!vs->matches.empty()) {
        vs->matches.clear();
        vs->current_match = -1;
        if (vs->renderer) vs->renderer->set_search_matches({}, -1);
        InvalidateRect(hwnd, nullptr, FALSE);
        handled = true;
    }
}
```

- [ ] **Step 6: Export in plugin.def**

Edit `src/colorizer/colorizer_plugin.def`:

```
LIBRARY "wlx-listerine-colorizer"
EXPORTS
    ListLoadW
    ListLoadNextW
    ListCloseWindow
    ListGetDetectString
    ListSendCommand
    ListSetDefaultParams
    ListSearchTextW
```

- [ ] **Step 7: Build**

```bash
cmake --build --preset conan-release --target wlx-listerine-colorizer 2>&1 | tail -10
```

Expected: clean build.

- [ ] **Step 8: Commit**

```bash
git add src/colorizer/colorizer_host_adapter.cpp src/colorizer/colorizer_plugin.def
git commit -m "feat: implement ListSearchTextW with multi-match highlight (colorizer)"
```

---

## Phase 7 — Validation

### Task 13: Full build + doctest run

- [ ] **Step 1: Clean build both plugins and tests**

```bash
cmake --build --preset conan-release 2>&1 | tail -20
```

Expected: both `.wlx64` files produced; `tests.exe` and `colorizer-tests.exe` built.

- [ ] **Step 2: Run all tests**

```bash
./build/Release/tests.exe && ./build/Release/colorizer-tests.exe
```

Expected: all passing. The search tests added in Tasks 2/4 should show `9` or more new test cases.

---

### Task 14: Visual regression case for search highlight (md)

**Files:**
- Create: `test_data/cases/28_search_highlight/case.toml`
- Create: `test_data/cases/28_search_highlight/input.md`
- Create: `test_data/cases/28_search_highlight/expected_chrome.png` (golden, generated via `bun run update-goldens`)

- [ ] **Step 1: Inspect existing case format**

```bash
ls test_data/cases/01_headings_atx/ && cat test_data/cases/01_headings_atx/case.toml
```

- [ ] **Step 2: Decide whether screenshot_tool supports invoking ListSearchTextW**

Read `src/screenshot_main.cpp` to determine whether the tool can trigger a search post-load. If not, skip this task — document search behavior manually (see Task 15). If supported, continue.

- [ ] **Step 3: Create input.md and case.toml**

Pick a short markdown with 3–4 occurrences of a common word ("the" or similar). Create `case.toml` with an extra `search` field (e.g., `search = "the"`) matching the screenshot_tool's API. If the tool needs extension, that's out of scope for this plan — skip this case and rely on manual test.

- [ ] **Step 4: Generate golden**

```bash
bun run update-goldens -- 28_search_highlight
```

- [ ] **Step 5: Run visual regression**

```bash
./scripts/visual-test.sh
```

Expected: all cases (including new one) ≥ 95% similarity.

- [ ] **Step 6: Commit (only if task didn't skip)**

```bash
git add test_data/cases/28_search_highlight/
git commit -m "test: visual regression for search highlight (md)"
```

---

### Task 15: Manual smoke test

- [ ] **Step 1: Install both plugins to TC**

Copy `output/wlx-listerine-md.wlx64`, `output/wlx-listerine-colorizer.wlx64`, their `.toml` configs, and the `themes/` directory into Total Commander's plugins directory (user-specific; typically via TC's plugin manager).

- [ ] **Step 2: Test key forwarding (md)**

Open a markdown file in Lister. Verify:
- F2 reloads the file (title bar / content refresh).
- F3 performs its standard Lister action (typically closes).
- N loads the next file in the panel.
- P loads the previous file.
- W toggles wrap.
- Ctrl+C still copies selection; Ctrl+A still selects all; Esc still clears selection.

- [ ] **Step 3: Test search (md)**

- Press F7 — Lister's Find dialog opens.
- Type "the" → OK. All matches dim-highlighted; current match highlighted strongly; view scrolled to the first match.
- F5 repeatedly — current match advances, wraps after the last.
- F7 again, type "xyzzy123" → "Search string not found" dialog.
- With active highlights, press Esc — highlights clear.
- Toggle Match case / Whole words / Backwards in the dialog — verify each works.

- [ ] **Step 4: Test search + wrap (colorizer)**

Repeat steps 2–3 for a source file (e.g., `src/host_adapter.cpp`) via the colorizer plugin. Confirm W toggles wrap in addition to everything else.

- [ ] **Step 5: Verify no regressions**

Run the visual regression suite once more:
```bash
./scripts/visual-test.sh
```
Expected: no previously-passing case drops below 95%.

- [ ] **Step 6: Final commit if any smoke-test fixes were needed**

If any fixes were committed in this task, nothing more to do. Otherwise, this task produces no commits.

---

## Ordering note for build-by-task execution

Tasks 1–4 can be done as written (self-contained in wlx-core).

Tasks 5–7 (theme + render) depend only on search_engine.h being in place. They can be done in any order after Task 1, and are independent of each other.

Tasks 8–9 (key forwarding) reference `vs->matches` (added in Tasks 10/12). There are two valid orderings:
- **Interleaved** (recommended): do Task 10 step 1 (add ViewState fields) before Task 8. Then Task 8, Task 9, Task 10 steps 2–7, Task 12.
- **Two-pass**: do Task 8 without the matches-clear Esc branch, commit, then revisit after Task 10 to add the branch.

The plan as written assumes interleaved execution. Subagents should add the ViewState fields (Task 10 step 1, Task 12 step 1) before applying the Esc-clears-matches branch in Tasks 8 / 9.

## Spec coverage check

| Spec requirement | Task |
|---|---|
| SearchIndex flattening + find_all | 1, 3 |
| Case, whole-words, boundary rejection, needle semantics | 4 |
| Key forwarding via SendMessageW(parent, …) | 8, 9 |
| Esc closes without selection / active search | 8, 9, 12 |
| ListSearchTextW export + findfirst/advance/wrap semantics | 10, 12 |
| Scroll current match into view | 10, 12 (scroll_to_match) |
| Highlight all matches + emphasize current | 7 |
| New palette entries (light + dark) | 5 |
| Helix theme scopes for colorizer | 6 |
| `lcp_wraptext` in colorizer | 11 |
| Index dirty-invalidation on relayout / reload | 10, 12 |
| Unit tests for search semantics | 2, 4 |
| Theme defaults test | 5 |
| Visual regression | 14 |
| Manual smoke test | 15 |
