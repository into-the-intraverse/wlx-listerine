# Text Selection & Copy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add mouse-driven text selection, copy (Ctrl+C), select-all (Ctrl+A), code block copy buttons, and TC lc_copy/lc_selectall integration.

**Architecture:** Block+offset selection model using `{block_index, char_offset}` pairs. Hit-testing uses `IDWriteTextLayout::HitTestPoint` per block. Selection highlights rendered via `HitTestTextRange`. Host adapter owns selection state; render engine receives the range each frame.

**Tech Stack:** C++17, Direct2D, DirectWrite, Win32 API, doctest for tests.

**Spec:** `docs/superpowers/specs/2026-04-11-text-selection-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/layout_engine.h` | Modify | Add `TextPosition` struct (shared by host_adapter and render_engine) |
| `src/host_adapter.cpp` | Modify | Selection state in ViewState, all mouse/keyboard handlers, clipboard, TC commands |
| `src/render_engine.h` | Modify | New `paint()` signature with selection range, code block copy button state |
| `src/render_engine.cpp` | Modify | Selection highlight rendering, code block copy button drawing |
| `tests/test_text_selection.cpp` | Create | Tests for TextPosition comparison, text extraction, word boundary detection |
| `CMakeLists.txt` | Modify | Add test_text_selection.cpp to test sources |

---

### Task 1: TextPosition struct and comparison

**Files:**
- Modify: `src/layout_engine.h` (after `struct LayoutDocument`, around line 74)
- Create: `tests/test_text_selection.cpp`
- Modify: `CMakeLists.txt` (add test file to test sources)

- [ ] **Step 1: Write failing tests for TextPosition**

Create `tests/test_text_selection.cpp`:

```cpp
#include <doctest/doctest.h>
#include "layout_engine.h"

TEST_CASE("TextPosition::valid") {
    TextPosition p;
    CHECK_FALSE(p.valid());

    p.block_index = 0;
    CHECK(p.valid());

    p.block_index = -1;
    CHECK_FALSE(p.valid());
}

TEST_CASE("TextPosition comparison") {
    TextPosition a{0, 5};
    TextPosition b{0, 10};
    TextPosition c{1, 0};
    TextPosition d{1, 0};

    CHECK(a < b);
    CHECK(b < c);
    CHECK(a < c);
    CHECK_FALSE(b < a);
    CHECK(a == a);
    CHECK(c == d);
    CHECK_FALSE(a == b);
    CHECK(a != b);
    CHECK_FALSE(c != d);
}

TEST_CASE("TextPosition min/max helpers") {
    TextPosition a{0, 5};
    TextPosition b{1, 3};

    auto lo = std::min(a, b);
    auto hi = std::max(a, b);
    CHECK(lo.block_index == 0);
    CHECK(lo.char_offset == 5);
    CHECK(hi.block_index == 1);
    CHECK(hi.char_offset == 3);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe -tc="TextPosition*"`
Expected: build fails — `TextPosition` not defined.

- [ ] **Step 3: Add TextPosition to layout_engine.h**

Add after the `LayoutDocument` struct (after line 74 in `src/layout_engine.h`):

```cpp
// ---------- selection model ----------

struct TextPosition {
    int block_index = -1;
    int char_offset = 0;

    bool valid() const { return block_index >= 0; }

    bool operator==(const TextPosition& o) const {
        return block_index == o.block_index && char_offset == o.char_offset;
    }
    bool operator!=(const TextPosition& o) const { return !(*this == o); }
    bool operator<(const TextPosition& o) const {
        if (block_index != o.block_index) return block_index < o.block_index;
        return char_offset < o.char_offset;
    }
    bool operator>(const TextPosition& o) const { return o < *this; }
    bool operator<=(const TextPosition& o) const { return !(o < *this); }
    bool operator>=(const TextPosition& o) const { return !(*this < o); }
};
```

- [ ] **Step 4: Add test file to CMakeLists.txt**

In `CMakeLists.txt`, find the `add_executable(tests ...)` block and add `tests/test_text_selection.cpp` to the source list.

- [ ] **Step 5: Build and run tests**

Run: `cmake --preset conan-default && cmake --build --preset conan-release && ./build/Release/tests.exe -tc="TextPosition*"`
Expected: all 3 test cases PASS.

- [ ] **Step 6: Commit**

```bash
git add src/layout_engine.h tests/test_text_selection.cpp CMakeLists.txt
git commit -m "feat: add TextPosition struct with comparison operators"
```

---

### Task 2: Text extraction logic

**Files:**
- Modify: `src/host_adapter.cpp` (add static helper function)
- Modify: `tests/test_text_selection.cpp` (add extraction tests)

- [ ] **Step 1: Write failing tests for text extraction**

Append to `tests/test_text_selection.cpp`:

```cpp
#include "markdown_parser.h"

// Helpers reused from test_layout_engine.cpp pattern
static ComPtr<IDWriteFactory> create_dwrite_factory() {
    ComPtr<IDWriteFactory> factory;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    return factory;
}

static Document parse(const char* md) {
    MarkdownParser p;
    return p.parse(md, std::strlen(md));
}

static LayoutDocument do_layout(IDWriteFactory* factory, const Document& doc,
                                float width = 800.0f) {
    ThemeService theme;
    LayoutEngine engine(factory, theme, false);
    return engine.layout(doc, width);
}
```

Note: The actual text extraction function will live in `host_adapter.cpp` as a static function but it's difficult to test from an external test file. Instead, extract the logic into a free function declared in `layout_engine.h` (it only depends on LayoutDocument and TextPosition, both defined there).

Add to `tests/test_text_selection.cpp`:

```cpp
TEST_CASE("extract_selected_text - single block partial") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("Hello World");
    auto layout = do_layout(factory.Get(), doc);
    REQUIRE(!layout.blocks.empty());

    // Select "llo W" from "Hello World"
    TextPosition start{0, 2};
    TextPosition end{0, 7};
    auto text = extract_selected_text(layout, start, end);
    CHECK(text == L"llo W");
}

TEST_CASE("extract_selected_text - full block") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("Hello World");
    auto layout = do_layout(factory.Get(), doc);
    REQUIRE(!layout.blocks.empty());

    // Find the paragraph block
    int para_idx = -1;
    for (int i = 0; i < static_cast<int>(layout.blocks.size()); i++) {
        if (layout.blocks[i].type == BlockType::Paragraph) {
            para_idx = i;
            break;
        }
    }
    REQUIRE(para_idx >= 0);

    auto& blk = layout.blocks[para_idx];
    int last_char = 0;
    for (auto& run : blk.text_runs)
        last_char += static_cast<int>(run.text.size());

    TextPosition start{para_idx, 0};
    TextPosition end{para_idx, last_char};
    auto text = extract_selected_text(layout, start, end);
    CHECK(text == L"Hello World");
}

TEST_CASE("extract_selected_text - cross block") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("First paragraph\n\nSecond paragraph");
    auto layout = do_layout(factory.Get(), doc);

    // Find first and last paragraph blocks
    int first_para = -1, last_para = -1;
    for (int i = 0; i < static_cast<int>(layout.blocks.size()); i++) {
        if (layout.blocks[i].type == BlockType::Paragraph) {
            if (first_para < 0) first_para = i;
            last_para = i;
        }
    }
    REQUIRE(first_para >= 0);
    REQUIRE(last_para > first_para);

    TextPosition start{first_para, 0};
    int last_len = 0;
    for (auto& run : layout.blocks[last_para].text_runs)
        last_len += static_cast<int>(run.text.size());
    TextPosition end{last_para, last_len};

    auto text = extract_selected_text(layout, start, end);
    // Should contain both paragraphs joined by \r\n
    CHECK(text.find(L"First paragraph") != std::wstring::npos);
    CHECK(text.find(L"Second paragraph") != std::wstring::npos);
    CHECK(text.find(L"\r\n") != std::wstring::npos);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe -tc="extract_selected_text*"`
Expected: build fails — `extract_selected_text` not defined.

- [ ] **Step 3: Implement extract_selected_text**

Add to `src/layout_engine.h` after `TextPosition`:

```cpp
// Extract plain text from a selection range in a layout document.
std::wstring extract_selected_text(const LayoutDocument& layout,
                                   TextPosition start, TextPosition end);
```

Create `src/text_selection.cpp` (new file):

```cpp
#include "layout_engine.h"
#include <algorithm>

static std::wstring block_full_text(const LayoutBlock& block) {
    std::wstring result;
    for (auto& run : block.text_runs)
        result += run.text;
    return result;
}

std::wstring extract_selected_text(const LayoutDocument& layout,
                                   TextPosition start, TextPosition end) {
    if (!start.valid() || !end.valid()) return {};
    if (end < start) std::swap(start, end);

    std::wstring result;
    int block_count = static_cast<int>(layout.blocks.size());

    for (int i = start.block_index; i <= end.block_index && i < block_count; i++) {
        auto& block = layout.blocks[i];
        std::wstring full = block_full_text(block);

        if (full.empty()) {
            // Non-text block (HorizontalRule, etc.) — blank line
            if (!result.empty()) result += L"\r\n";
            continue;
        }

        int from = 0;
        int to = static_cast<int>(full.size());

        if (i == start.block_index)
            from = std::clamp(start.char_offset, 0, to);
        if (i == end.block_index)
            to = std::clamp(end.char_offset, 0, to);

        if (from >= to && i == start.block_index && i == end.block_index)
            continue;

        if (!result.empty()) result += L"\r\n";

        // Prepend bullet for list items
        if ((block.type == BlockType::ListItem || block.type == BlockType::TaskList)
            && !block.bullet_text.empty() && from == 0) {
            result += block.bullet_text;
        }

        result += full.substr(static_cast<size_t>(from),
                              static_cast<size_t>(to - from));
    }

    return result;
}
```

- [ ] **Step 4: Add text_selection.cpp to CMakeLists.txt**

Add `src/text_selection.cpp` to the `wlx-core` library sources in CMakeLists.txt.

- [ ] **Step 5: Build and run tests**

Run: `cmake --preset conan-default && cmake --build --preset conan-release && ./build/Release/tests.exe -tc="extract_selected_text*"`
Expected: all 3 extraction test cases PASS.

- [ ] **Step 6: Run full test suite**

Run: `./build/Release/tests.exe`
Expected: all tests pass (86 old + 6 new).

- [ ] **Step 7: Commit**

```bash
git add src/layout_engine.h src/text_selection.cpp tests/test_text_selection.cpp CMakeLists.txt
git commit -m "feat: add text extraction for selection ranges"
```

---

### Task 3: Word boundary detection

**Files:**
- Modify: `src/layout_engine.h` (declare function)
- Modify: `src/text_selection.cpp` (implement)
- Modify: `tests/test_text_selection.cpp` (add tests)

- [ ] **Step 1: Write failing tests**

Append to `tests/test_text_selection.cpp`:

```cpp
TEST_CASE("find_word_boundaries - middle of word") {
    std::wstring text = L"Hello beautiful world";
    auto [start, end] = find_word_boundaries(text, 8); // 'u' in 'beautiful'
    CHECK(start == 6);
    CHECK(end == 15);
}

TEST_CASE("find_word_boundaries - start of text") {
    std::wstring text = L"Hello world";
    auto [start, end] = find_word_boundaries(text, 0);
    CHECK(start == 0);
    CHECK(end == 5);
}

TEST_CASE("find_word_boundaries - on whitespace") {
    std::wstring text = L"Hello world";
    auto [start, end] = find_word_boundaries(text, 5); // the space
    CHECK(start == 5);
    CHECK(end == 6);
}

TEST_CASE("find_word_boundaries - end of text") {
    std::wstring text = L"Hello";
    auto [start, end] = find_word_boundaries(text, 4);
    CHECK(start == 0);
    CHECK(end == 5);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe -tc="find_word*"`
Expected: build fails — `find_word_boundaries` not defined.

- [ ] **Step 3: Implement find_word_boundaries**

Add declaration to `src/layout_engine.h`:

```cpp
// Find word boundaries around a character offset. Returns {start, end}.
std::pair<int, int> find_word_boundaries(const std::wstring& text, int offset);
```

Add to `src/text_selection.cpp`:

```cpp
std::pair<int, int> find_word_boundaries(const std::wstring& text, int offset) {
    int len = static_cast<int>(text.size());
    if (len == 0) return {0, 0};
    offset = std::clamp(offset, 0, len - 1);

    auto is_word_char = [](wchar_t c) {
        return !iswspace(c) && !iswpunct(c);
    };

    bool on_word = is_word_char(text[offset]);

    int start = offset;
    int end = offset;

    if (on_word) {
        while (start > 0 && is_word_char(text[start - 1])) start--;
        while (end < len && is_word_char(text[end])) end++;
    } else {
        // Select the whitespace/punctuation run
        auto same_class = [&](wchar_t c) {
            return !is_word_char(c) && iswspace(c) == iswspace(text[offset]);
        };
        while (start > 0 && same_class(text[start - 1])) start--;
        while (end < len && same_class(text[end])) end++;
    }

    return {start, end};
}
```

- [ ] **Step 4: Build and run tests**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe -tc="find_word*"`
Expected: all 4 word boundary test cases PASS.

- [ ] **Step 5: Commit**

```bash
git add src/layout_engine.h src/text_selection.cpp tests/test_text_selection.cpp
git commit -m "feat: add word boundary detection for double-click select"
```

---

### Task 4: Selection state and mouse handlers in host_adapter

**Files:**
- Modify: `src/host_adapter.cpp`

This is the core interaction task. It adds selection state to ViewState and rewrites the mouse handlers.

- [ ] **Step 1: Add selection state to ViewState**

In `src/host_adapter.cpp`, modify the `ViewState` struct (around line 33):

```cpp
struct ViewState {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    bool dark_mode = false;
    std::wstring file_path;

    std::shared_ptr<Document> document;
    std::shared_ptr<LayoutDocument> layout;
    std::unique_ptr<RenderEngine> renderer;
    std::unique_ptr<InteractionEngine> interaction;

    float scroll_y = 0;
    float max_scroll_y = 0;
    int hovered_span = -1;

    // Selection
    TextPosition sel_anchor;
    TextPosition sel_active;
    bool selecting = false;
    int hovered_code_block = -1;  // for copy button
    int copied_code_block = -1;   // shows checkmark after copy
};
```

- [ ] **Step 2: Add hit-test helper for selection**

Add a static helper function before the WndProc in `src/host_adapter.cpp`:

```cpp
static constexpr UINT_PTR TIMER_AUTOSCROLL = 1;
static constexpr UINT_PTR TIMER_COPY_FEEDBACK = 2;

// Hit-test a DIP document coordinate to a TextPosition.
// Returns invalid TextPosition if not over a text block.
static TextPosition hit_test_position(const LayoutDocument& layout, float x, float y) {
    int block_count = static_cast<int>(layout.blocks.size());

    // First: direct block hit
    for (int i = 0; i < block_count; i++) {
        auto& block = layout.blocks[i];
        if (block.text_runs.empty()) continue;
        if (y < block.rect.top || y > block.rect.bottom) continue;

        // Use the first text run's layout for hit-testing
        auto& run = block.text_runs[0];
        if (!run.layout) continue;

        float local_x = x - run.rect.left;
        float local_y = y - run.rect.top;
        BOOL is_trailing = FALSE;
        BOOL is_inside = FALSE;
        DWRITE_HIT_TEST_METRICS htm = {};
        run.layout->HitTestPoint(local_x, local_y, &is_trailing, &is_inside, &htm);

        int offset = static_cast<int>(htm.textPosition);
        if (is_trailing) offset++;
        return TextPosition{i, offset};
    }

    // Snap to nearest block boundary (for gaps between blocks)
    int closest = -1;
    float closest_dist = 1e9f;
    for (int i = 0; i < block_count; i++) {
        auto& block = layout.blocks[i];
        if (block.text_runs.empty()) continue;
        float mid = (block.rect.top + block.rect.bottom) * 0.5f;
        float dist = std::abs(y - mid);
        if (dist < closest_dist) {
            closest_dist = dist;
            closest = i;
        }
    }

    if (closest >= 0) {
        auto& block = layout.blocks[closest];
        if (y < (block.rect.top + block.rect.bottom) * 0.5f) {
            return TextPosition{closest, 0};
        } else {
            int len = 0;
            for (auto& run : block.text_runs) len += static_cast<int>(run.text.size());
            return TextPosition{closest, len};
        }
    }

    return TextPosition{};
}

// Get the full text length for a block
static int block_text_length(const LayoutBlock& block) {
    int len = 0;
    for (auto& run : block.text_runs) len += static_cast<int>(run.text.size());
    return len;
}

// Copy text to clipboard
static bool copy_to_clipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty()) return false;
    if (!OpenClipboard(hwnd)) return false;
    EmptyClipboard();

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hg) {
        void* p = GlobalLock(hg);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(hg);
            SetClipboardData(CF_UNICODETEXT, hg);
        }
    }
    CloseClipboard();
    return true;
}

// Check if a point is inside the code block copy button rect
static bool is_in_copy_button(const LayoutBlock& block, float x, float y) {
    if (block.type != BlockType::CodeFence) return false;
    float btn_size = 24.0f;
    float pad = 6.0f;
    float bx = block.rect.right - btn_size - pad;
    float by = block.rect.top + pad;
    return x >= bx && x <= bx + btn_size && y >= by && y <= by + btn_size;
}

// Clear selection state
static void clear_selection(ViewState* vs) {
    vs->sel_anchor = TextPosition{};
    vs->sel_active = TextPosition{};
    vs->selecting = false;
}
```

- [ ] **Step 3: Add CS_DBLCLKS to window class**

In `ensure_window_class()`, change:

```cpp
    wc.style = CS_HREDRAW | CS_VREDRAW;
```

to:

```cpp
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
```

- [ ] **Step 4: Replace WM_MOUSEMOVE handler**

Replace the existing `case WM_MOUSEMOVE:` block with:

```cpp
    case WM_MOUSEMOVE: {
        if (!vs) break;
        float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                : static_cast<float>(GET_X_LPARAM(lp));
        float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                : static_cast<float>(GET_Y_LPARAM(lp));
        float doc_x = px;
        float doc_y = py + vs->scroll_y;

        if (vs->selecting && vs->layout) {
            // Extend selection
            auto pos = hit_test_position(*vs->layout, doc_x, doc_y);
            if (pos.valid() && pos != vs->sel_active) {
                vs->sel_active = pos;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            // Auto-scroll when dragging beyond viewport
            float viewport_h = vs->renderer ? vs->renderer->dip_height() : 100.0f;
            if (py < 0) {
                SetTimer(hwnd, TIMER_AUTOSCROLL, 50, nullptr);
            } else if (py > viewport_h) {
                SetTimer(hwnd, TIMER_AUTOSCROLL, 50, nullptr);
            } else {
                KillTimer(hwnd, TIMER_AUTOSCROLL);
            }
        } else if (vs->interaction && vs->layout) {
            // Hover detection for links
            auto hit = vs->interaction->hit_test(doc_x, doc_y);
            int new_hover = hit.hit ? hit.span_index : -1;
            if (new_hover != vs->hovered_span) {
                vs->hovered_span = new_hover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            // Hover detection for code block copy button
            int new_code_hover = -1;
            for (int i = 0; i < static_cast<int>(vs->layout->blocks.size()); i++) {
                auto& block = vs->layout->blocks[i];
                if (is_in_copy_button(block, doc_x, doc_y)) {
                    new_code_hover = i;
                    break;
                }
            }
            if (new_code_hover != vs->hovered_code_block) {
                vs->hovered_code_block = new_code_hover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            // Set cursor
            if (vs->hovered_span >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
            } else if (new_code_hover >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            } else {
                // I-beam over text blocks, arrow elsewhere
                bool over_text = false;
                for (auto& block : vs->layout->blocks) {
                    if (block.text_runs.empty()) continue;
                    if (doc_y >= block.rect.top && doc_y <= block.rect.bottom &&
                        doc_x >= block.rect.left && doc_x <= block.rect.right) {
                        over_text = true;
                        break;
                    }
                }
                SetCursor(LoadCursorW(nullptr, over_text ? IDC_IBEAM : IDC_ARROW));
            }
        }
        return 0;
    }
```

- [ ] **Step 5: Add WM_LBUTTONDOWN handler**

Add before the `WM_LBUTTONUP` case:

```cpp
    case WM_LBUTTONDOWN: {
        if (!vs || !vs->layout) break;
        SetFocus(hwnd);
        float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                : static_cast<float>(GET_X_LPARAM(lp));
        float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                : static_cast<float>(GET_Y_LPARAM(lp));
        float doc_x = px;
        float doc_y = py + vs->scroll_y;

        auto pos = hit_test_position(*vs->layout, doc_x, doc_y);
        if (pos.valid()) {
            vs->sel_anchor = pos;
            vs->sel_active = pos;
            vs->selecting = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            clear_selection(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
```

- [ ] **Step 6: Replace WM_LBUTTONUP handler**

Replace the existing `case WM_LBUTTONUP:` block with:

```cpp
    case WM_LBUTTONUP: {
        if (!vs || !vs->layout) break;
        bool was_selecting = vs->selecting;
        vs->selecting = false;
        ReleaseCapture();
        KillTimer(hwnd, TIMER_AUTOSCROLL);

        float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                : static_cast<float>(GET_X_LPARAM(lp));
        float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                : static_cast<float>(GET_Y_LPARAM(lp));
        float doc_x = px;
        float doc_y = py + vs->scroll_y;

        // Check code block copy button first
        for (int i = 0; i < static_cast<int>(vs->layout->blocks.size()); i++) {
            auto& block = vs->layout->blocks[i];
            if (is_in_copy_button(block, doc_x, doc_y)) {
                std::wstring code_text;
                for (auto& run : block.text_runs)
                    code_text += run.text;
                copy_to_clipboard(hwnd, code_text);
                vs->copied_code_block = i;
                SetTimer(hwnd, TIMER_COPY_FEEDBACK, 1000, nullptr);
                clear_selection(vs);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        // Update final active position
        auto pos = hit_test_position(*vs->layout, doc_x, doc_y);
        if (pos.valid()) vs->sel_active = pos;

        if (vs->sel_anchor == vs->sel_active) {
            // No drag — this was a click. Handle links.
            clear_selection(vs);
            if (vs->interaction) {
                auto hit = vs->interaction->hit_test(doc_x, doc_y);
                if (hit.hit) {
                    auto action = vs->interaction->resolve(hit.target);
                    switch (action.action) {
                    case InteractionEngine::Action::ScrollToAnchor:
                        vs->scroll_y = std::clamp(action.scroll_y, 0.0f, vs->max_scroll_y);
                        update_scrollbar(vs);
                        break;
                    case InteractionEngine::Action::OpenExternal:
                        ShellExecuteW(nullptr, L"open", action.target.c_str(),
                                      nullptr, nullptr, SW_SHOW);
                        break;
                    case InteractionEngine::Action::ReloadDocument: {
                        if (!vs->file_path.empty()) {
                            std::wstring dir = vs->file_path;
                            auto fpos = dir.find_last_of(L"\\/");
                            if (fpos != std::wstring::npos) dir = dir.substr(0, fpos + 1);
                            std::wstring full = dir + action.target;
                            load_document(vs, full.c_str());
                        }
                        break;
                    }
                    default: break;
                    }
                }
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
```

- [ ] **Step 7: Add WM_LBUTTONDBLCLK handler**

Add after the `WM_LBUTTONUP` case:

```cpp
    case WM_LBUTTONDBLCLK: {
        if (!vs || !vs->layout) break;
        float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                : static_cast<float>(GET_X_LPARAM(lp));
        float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                : static_cast<float>(GET_Y_LPARAM(lp));
        float doc_y = py + vs->scroll_y;

        auto pos = hit_test_position(*vs->layout, px, doc_y);
        if (pos.valid()) {
            auto& block = vs->layout->blocks[pos.block_index];
            std::wstring full;
            for (auto& run : block.text_runs) full += run.text;
            auto [ws, we] = find_word_boundaries(full, pos.char_offset);
            vs->sel_anchor = TextPosition{pos.block_index, ws};
            vs->sel_active = TextPosition{pos.block_index, we};
            vs->selecting = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
```

- [ ] **Step 8: Add WM_TIMER handler**

Add after `WM_LBUTTONDBLCLK`:

```cpp
    case WM_TIMER: {
        if (wp == TIMER_AUTOSCROLL && vs && vs->selecting) {
            float py = vs->renderer ? vs->renderer->pixel_to_dip_y(
                static_cast<float>(GET_Y_LPARAM(GetMessagePos()))) : 0;
            // Approximate: use last known mouse position direction
            float line = g_theme.fonts().body_size * g_theme.spacing().line_height_factor;
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            float client_y = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(pt.y))
                                          : static_cast<float>(pt.y);
            if (client_y < 0)
                handle_scroll(vs, -line);
            else
                handle_scroll(vs, line);

            // Update selection active to new scroll position
            if (vs->layout) {
                float doc_x = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(pt.x))
                                           : static_cast<float>(pt.x);
                float doc_y = client_y + vs->scroll_y;
                auto pos = hit_test_position(*vs->layout, doc_x, doc_y);
                if (pos.valid()) vs->sel_active = pos;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wp == TIMER_COPY_FEEDBACK && vs) {
            KillTimer(hwnd, TIMER_COPY_FEEDBACK);
            vs->copied_code_block = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
```

- [ ] **Step 9: Update WM_SETCURSOR handler**

Replace the existing `case WM_SETCURSOR:` block with:

```cpp
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            // Cursor is set in WM_MOUSEMOVE; just confirm it
            return TRUE;
        }
        break;
```

- [ ] **Step 10: Build and verify**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe`
Expected: all tests pass. (Selection rendering not yet visible — that's Task 5.)

- [ ] **Step 11: Commit**

```bash
git add src/host_adapter.cpp
git commit -m "feat: add selection mouse handlers and hit-testing"
```

---

### Task 5: Keyboard shortcuts and TC commands

**Files:**
- Modify: `src/host_adapter.cpp`

- [ ] **Step 1: Update WM_KEYDOWN handler**

Replace the existing `case WM_KEYDOWN:` block with:

```cpp
    case WM_KEYDOWN: {
        if (!vs) break;
        float page = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        float line = g_theme.fonts().body_size * g_theme.spacing().line_height_factor;

        // Ctrl+C — copy selection
        if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (vs->layout && vs->sel_anchor.valid() && vs->sel_anchor != vs->sel_active) {
                auto lo = std::min(vs->sel_anchor, vs->sel_active);
                auto hi = std::max(vs->sel_anchor, vs->sel_active);
                auto text = extract_selected_text(*vs->layout, lo, hi);
                copy_to_clipboard(hwnd, text);
            }
            return 0;
        }

        // Ctrl+A — select all
        if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (vs->layout && !vs->layout->blocks.empty()) {
                vs->sel_anchor = TextPosition{0, 0};
                int last = static_cast<int>(vs->layout->blocks.size()) - 1;
                vs->sel_active = TextPosition{last, block_text_length(vs->layout->blocks[last])};
                vs->selecting = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        // Escape — clear selection
        if (wp == VK_ESCAPE) {
            if (vs->sel_anchor.valid()) {
                clear_selection(vs);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        switch (wp) {
        case VK_UP:    handle_scroll(vs, -line); break;
        case VK_DOWN:  handle_scroll(vs, line); break;
        case VK_PRIOR: handle_scroll(vs, -page); break;
        case VK_NEXT:  handle_scroll(vs, page); break;
        case VK_HOME:
            vs->scroll_y = 0;
            update_scrollbar(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case VK_END:
            vs->scroll_y = vs->max_scroll_y;
            update_scrollbar(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        return 0;
    }
```

- [ ] **Step 2: Update ListSendCommand**

Replace the `lc_copy` and `lc_selectall` cases:

```cpp
    case lc_copy: {
        if (!vs->layout || !vs->sel_anchor.valid() || vs->sel_anchor == vs->sel_active)
            return LISTPLUGIN_ERROR;
        auto lo = std::min(vs->sel_anchor, vs->sel_active);
        auto hi = std::max(vs->sel_anchor, vs->sel_active);
        auto text = extract_selected_text(*vs->layout, lo, hi);
        return copy_to_clipboard(vs->hwnd, text) ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
    }

    case lc_selectall: {
        if (!vs->layout || vs->layout->blocks.empty())
            return LISTPLUGIN_ERROR;
        vs->sel_anchor = TextPosition{0, 0};
        int last = static_cast<int>(vs->layout->blocks.size()) - 1;
        vs->sel_active = TextPosition{last, block_text_length(vs->layout->blocks[last])};
        vs->selecting = false;
        InvalidateRect(vs->hwnd, nullptr, FALSE);
        return LISTPLUGIN_OK;
    }
```

- [ ] **Step 3: Build and run tests**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe`
Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/host_adapter.cpp
git commit -m "feat: add Ctrl+C, Ctrl+A, Escape, lc_copy, lc_selectall"
```

---

### Task 6: Selection highlight rendering

**Files:**
- Modify: `src/render_engine.h`
- Modify: `src/render_engine.cpp`
- Modify: `src/host_adapter.cpp` (pass selection to paint)

- [ ] **Step 1: Update paint() signature**

In `src/render_engine.h`, change:

```cpp
    void paint(const LayoutDocument& layout, float scroll_y);
```

to:

```cpp
    void paint(const LayoutDocument& layout, float scroll_y,
               TextPosition sel_start = {}, TextPosition sel_end = {});
```

- [ ] **Step 2: Add paint_selection_highlight private method**

In `src/render_engine.h`, add to private section:

```cpp
    void paint_selection_highlight(const LayoutBlock& block, int block_index,
                                   float offset_y, TextPosition sel_start, TextPosition sel_end);
```

- [ ] **Step 3: Implement paint_selection_highlight**

Add to `src/render_engine.cpp`:

```cpp
void RenderEngine::paint_selection_highlight(const LayoutBlock& block, int block_index,
                                              float offset_y, TextPosition sel_start,
                                              TextPosition sel_end) {
    if (!sel_start.valid() || !sel_end.valid()) return;
    if (sel_start == sel_end) return;
    if (block_index < sel_start.block_index || block_index > sel_end.block_index) return;

    const auto& colors = theme_.palette(dark_mode_);
    auto* brush = get_brush(colors.selection);
    if (!brush) return;

    bool fully_inside = (block_index > sel_start.block_index && block_index < sel_end.block_index);

    if (fully_inside || block.text_runs.empty()) {
        // Highlight entire block rect
        D2D1_RECT_F r = block.rect;
        r.top += offset_y;
        r.bottom += offset_y;
        rt_->FillRectangle(r, brush);
        return;
    }

    // Partial selection — use HitTestTextRange on the first text run
    auto& run = block.text_runs[0];
    if (!run.layout) return;

    int text_len = 0;
    for (auto& tr : block.text_runs) text_len += static_cast<int>(tr.text.size());

    int from = 0;
    int to = text_len;

    if (block_index == sel_start.block_index)
        from = std::clamp(sel_start.char_offset, 0, text_len);
    if (block_index == sel_end.block_index)
        to = std::clamp(sel_end.char_offset, 0, text_len);

    if (from >= to) return;

    UINT32 range_start = static_cast<UINT32>(from);
    UINT32 range_len = static_cast<UINT32>(to - from);

    // Get highlight rectangles from DirectWrite
    UINT32 count = 0;
    run.layout->HitTestTextRange(range_start, range_len, run.rect.left, run.rect.top + offset_y,
                                  nullptr, 0, &count);
    if (count == 0) return;

    std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
    run.layout->HitTestTextRange(range_start, range_len, run.rect.left, run.rect.top + offset_y,
                                  metrics.data(), count, &count);

    for (UINT32 i = 0; i < count; i++) {
        D2D1_RECT_F r = D2D1::RectF(
            metrics[i].left, metrics[i].top,
            metrics[i].left + metrics[i].width,
            metrics[i].top + metrics[i].height);
        rt_->FillRectangle(r, brush);
    }
}
```

- [ ] **Step 4: Update paint() to call selection highlight**

In `src/render_engine.cpp`, update the `paint()` method. Change the signature to match the header, and normalize selection range at the top:

```cpp
void RenderEngine::paint(const LayoutDocument& layout, float scroll_y,
                          TextPosition sel_start, TextPosition sel_end) {
```

Add normalization after `BeginDraw`:

```cpp
    // Normalize selection range
    if (sel_start.valid() && sel_end.valid() && sel_end < sel_start)
        std::swap(sel_start, sel_end);
```

In the block loop, insert the selection highlight call between `paint_block_background` and `paint_block_decoration`:

```cpp
        paint_block_background(block, 0);
        paint_selection_highlight(block, block_idx, 0, sel_start, sel_end);
        paint_block_decoration(block, 0);
        paint_bullet(block, 0);
        paint_text_runs(block, 0);
```

The loop needs a block index counter. Change the loop from:

```cpp
    for (auto& block : layout.blocks) {
```

to:

```cpp
    for (int block_idx = 0; block_idx < static_cast<int>(layout.blocks.size()); block_idx++) {
        auto& block = layout.blocks[block_idx];
```

- [ ] **Step 5: Update host_adapter paint call**

In `src/host_adapter.cpp`, in the `WM_PAINT` handler, change:

```cpp
            vs->renderer->paint(*vs->layout, vs->scroll_y);
```

to:

```cpp
            auto lo = std::min(vs->sel_anchor, vs->sel_active);
            auto hi = std::max(vs->sel_anchor, vs->sel_active);
            vs->renderer->paint(*vs->layout, vs->scroll_y, lo, hi);
```

- [ ] **Step 6: Update screenshot_tool paint call**

In `src/screenshot_main.cpp`, the `paint()` call needs to match the new signature. Since screenshots don't have selection, just pass defaults:

```cpp
        renderer.paint(layout, scroll_y);
```

This still works because `sel_start` and `sel_end` have default values `{}` in the declaration.

- [ ] **Step 7: Build and run all tests**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe`
Expected: all tests pass.

- [ ] **Step 8: Run visual regression tests**

Run: `./scripts/visual-test.sh`
Expected: 27/27 pass (selection highlights don't appear without a selection).

- [ ] **Step 9: Commit**

```bash
git add src/render_engine.h src/render_engine.cpp src/host_adapter.cpp
git commit -m "feat: render selection highlights via HitTestTextRange"
```

---

### Task 7: Code block copy button

**Files:**
- Modify: `src/render_engine.h`
- Modify: `src/render_engine.cpp`
- Modify: `src/host_adapter.cpp`

- [ ] **Step 1: Add copy button state to RenderEngine**

In `src/render_engine.h`, add public methods:

```cpp
    void set_hovered_code_block(int index) { hovered_code_block_ = index; }
    void set_copied_code_block(int index) { copied_code_block_ = index; }
```

Add private members:

```cpp
    int hovered_code_block_ = -1;
    int copied_code_block_ = -1;
```

- [ ] **Step 2: Add paint_copy_button private method**

In `src/render_engine.h`, add to private section:

```cpp
    void paint_copy_button(const LayoutBlock& block, int block_index, float offset_y);
```

- [ ] **Step 3: Implement paint_copy_button**

Add to `src/render_engine.cpp`:

```cpp
void RenderEngine::paint_copy_button(const LayoutBlock& block, int block_index, float offset_y) {
    if (block.type != BlockType::CodeFence) return;
    if (hovered_code_block_ != block_index && copied_code_block_ != block_index) return;

    const auto& colors = theme_.palette(dark_mode_);
    float btn_size = 24.0f;
    float pad = 6.0f;
    float bx = block.rect.right - btn_size - pad;
    float by = block.rect.top + pad + offset_y;

    bool copied = (copied_code_block_ == block_index);

    // Button background
    uint32_t bg_color = copied ? colors.link : colors.code_bg;
    auto* bg_brush = get_brush(bg_color);
    if (bg_brush) {
        D2D1_ROUNDED_RECT rr = {D2D1::RectF(bx, by, bx + btn_size, by + btn_size), 4.0f, 4.0f};
        rt_->FillRoundedRectangle(rr, bg_brush);
    }

    // Icon — draw two overlapping rectangles (copy icon) or a checkmark
    auto* icon_brush = get_brush(colors.text);
    if (!icon_brush) return;

    float cx = bx + btn_size * 0.5f;
    float cy = by + btn_size * 0.5f;

    if (copied) {
        // Checkmark
        auto* white_brush = get_brush(colors.background);
        if (white_brush) {
            D2D1_POINT_2F p1 = {cx - 5.0f, cy};
            D2D1_POINT_2F p2 = {cx - 1.5f, cy + 4.0f};
            D2D1_POINT_2F p3 = {cx + 5.0f, cy - 3.5f};
            rt_->DrawLine(p1, p2, white_brush, 1.5f);
            rt_->DrawLine(p2, p3, white_brush, 1.5f);
        }
    } else {
        // Two overlapping rectangles (copy icon)
        float s = 5.0f;  // half size of small rect
        D2D1_RECT_F back = D2D1::RectF(cx - s + 1.5f, cy - s - 1.0f,
                                         cx + s + 1.5f, cy + s - 1.0f);
        D2D1_RECT_F front = D2D1::RectF(cx - s - 1.5f, cy - s + 1.0f,
                                          cx + s - 1.5f, cy + s + 1.0f);
        rt_->DrawRectangle(back, icon_brush, 1.0f);
        rt_->DrawRectangle(front, icon_brush, 1.0f);
    }
}
```

- [ ] **Step 4: Call paint_copy_button in paint loop**

In `src/render_engine.cpp` paint() method, after `paint_text_runs`, add:

```cpp
        paint_copy_button(block, block_idx, 0);
```

- [ ] **Step 5: Pass hover/copied state from host_adapter**

In `src/host_adapter.cpp`, in the `WM_PAINT` handler, before the paint call add:

```cpp
            vs->renderer->set_hovered_code_block(vs->hovered_code_block);
            vs->renderer->set_copied_code_block(vs->copied_code_block);
```

- [ ] **Step 6: Build and run tests**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe`
Expected: all tests pass.

- [ ] **Step 7: Run visual regression tests**

Run: `./scripts/visual-test.sh`
Expected: 27/27 pass (copy button only appears on hover, which screenshots don't trigger).

- [ ] **Step 8: Commit**

```bash
git add src/render_engine.h src/render_engine.cpp src/host_adapter.cpp
git commit -m "feat: add code block copy button with hover and feedback"
```

---

### Task 8: Clear selection on document load and final cleanup

**Files:**
- Modify: `src/host_adapter.cpp`

- [ ] **Step 1: Clear selection when loading a new document**

In `src/host_adapter.cpp`, in `load_document()`, add after `vs->scroll_y = 0`:

```cpp
    clear_selection(vs);
    vs->hovered_code_block = -1;
    vs->copied_code_block = -1;
```

- [ ] **Step 2: Reset selection state in ListLoadNextW**

In `ListLoadNextW`, after loading the new document, the selection is already cleared by `load_document`. No extra change needed.

- [ ] **Step 3: Build and run full test suite**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe`
Expected: all tests pass.

- [ ] **Step 4: Run visual regression tests**

Run: `./scripts/visual-test.sh`
Expected: 27/27 pass.

- [ ] **Step 5: Commit**

```bash
git add src/host_adapter.cpp
git commit -m "feat: clear selection state on document load"
```

---

### Task 9: Manual integration test

**Files:** None (manual testing only)

- [ ] **Step 1: Build the plugin**

Run: `cmake --build --preset conan-release`

- [ ] **Step 2: Test with screenshot_tool**

Run: `./build/Release/screenshot_tool.exe test_data/sample.md --full --dark`
Expected: renders correctly, no crashes. No selection highlights (no selection state in screenshot tool).

- [ ] **Step 3: Document test matrix for manual TC testing**

When testing in Total Commander, verify:

1. **Click a link** — still opens/scrolls correctly (not broken by selection refactor)
2. **Drag to select text** in a single paragraph — blue highlight appears
3. **Drag across multiple paragraphs** — contiguous highlight through all blocks
4. **Double-click a word** — word is selected
5. **Ctrl+C** with selection — paste into Notepad to verify plain text
6. **Ctrl+A** — entire document highlighted
7. **Ctrl+A then Ctrl+C** — full document text on clipboard
8. **Escape** — selection cleared
9. **Hover over code block** — copy button appears in top-right
10. **Click code block copy button** — checkmark appears, code text on clipboard
11. **Scroll while dragging** — auto-scrolls when mouse exits viewport
12. **I-beam cursor** over text, hand over links, arrow over empty space
13. **TC Edit > Copy** (sends lc_copy) — works with selection
14. **TC Edit > Select All** (sends lc_selectall) — works

- [ ] **Step 4: Final commit if any fixes needed**

```bash
git add -A
git commit -m "fix: address integration test findings"
```

Only commit if changes were needed. Skip if everything works.
