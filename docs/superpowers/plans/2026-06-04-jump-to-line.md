# Jump to Line + Markdown Line-Number Gutter — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Ctrl+G "go to line" inline prompt to both WLX plugins, plus a rendered-logical-line gutter for the markdown plugin, driven by one shared `LayoutDocument::line_tops` index.

**Architecture:** A single `std::vector<float> line_tops` on `LayoutDocument` (one Y per logical line) powers both jumping and the gutter. `build_line_index()` fills it from laid-out blocks (hard-break aware, soft-wrap invariant). A shared `goto_line` module holds the pure prompt state machine + the templated `scroll_to_line`. The markdown layout reserves a left gutter column (two-pass: count → re-lay-out) and `RenderEngine` paints the numbers and the screen-space prompt box. Per-plugin WndProcs add a Ctrl+G case and route keys while the prompt is open.

**Tech Stack:** C++20, Direct2D/DirectWrite, doctest, CMake + Conan. Spec: `docs/superpowers/specs/2026-06-04-jump-to-line-design.md`.

---

## File Structure

**New files:**
- `src/runtime/layout/line_index.h` / `.cpp` — `build_line_index(LayoutDocument&)`.
- `src/runtime/host/goto_line.h` / `.cpp` — `GotoPrompt`, `goto_handle_key` (pure), `line_scroll_target` (pure), `scroll_to_line` (templated, header).
- `tests/runtime/layout/test_line_index.cpp` — line-index unit tests.
- `tests/runtime/host/test_goto_line.cpp` — prompt state-machine + clamp unit tests.

**Modified files:**
- `src/runtime/layout/layout_document.h` — add `line_tops`, `gutter_width`.
- `src/runtime/layout/layout_engine.h` / `.cpp` — `layout()` gains a `gutter_width` param; reserves the left column.
- `src/runtime/render/render_engine.h` / `.cpp` — `paint()` gains prompt params; new `paint_line_numbers` + `paint_goto_prompt`.
- `src/runtime/theme/theme_config.h` + `theme/theme_service.cpp` — markdown `line_numbers` flag (default on).
- `src/plugin_md/window/host_adapter.cpp` — `GotoPrompt` in `ViewState`; two-pass `do_layout`; Ctrl+G; prompt-aware `paint`.
- `src/plugin_colorizer/window/colorizer_host_adapter.cpp` — same wiring; `build_line_index` after `layout_source`.
- `src/runtime/CMakeLists.txt`, `tests/CMakeLists.txt` — register new sources/tests.

**Dependency order:** Task 1 → {2, 3, 4, 5, 6 in any order} → {7, 8} → 9. Tasks 2/3 are independent files; 7 (md host) and 8 (colorizer host) touch different files and can run in parallel once their deps land. `CMakeLists.txt` is shared by Tasks 2, 3, 7-tests — serialize those edits.

---

### Task 1: Add line index fields to LayoutDocument

**Files:**
- Modify: `src/runtime/layout/layout_document.h`

- [ ] **Step 1: Add the fields**

Replace the struct body in `src/runtime/layout/layout_document.h`:

```cpp
struct LayoutDocument {
    std::vector<LayoutBlock> blocks;
    std::vector<AnchorEntry> anchors;
    float total_height = 0;
    float viewport_width = 0;

    // Jump-to-line / gutter support.
    // line_tops[n-1] = document-space Y (in DIPs) of the top of logical line n.
    // size() == total logical-line count. Filled by build_line_index().
    std::vector<float> line_tops;
    // Width (DIPs) reserved on the left for the markdown line-number gutter.
    // 0 = no gutter (the colorizer draws its own gutter via bullets).
    float gutter_width = 0;
};
```

- [ ] **Step 2: Build to verify it compiles**

Run: `cmake --build --preset conan-release --target wlx-core`
Expected: builds clean (new fields are unused so far).

- [ ] **Step 3: Commit**

```bash
git add src/runtime/layout/layout_document.h
git commit -m "feat(layout): add line_tops + gutter_width to LayoutDocument"
```

---

### Task 2: build_line_index module

**Files:**
- Create: `src/runtime/layout/line_index.h`
- Create: `src/runtime/layout/line_index.cpp`
- Create: `tests/runtime/layout/test_line_index.cpp`
- Modify: `src/runtime/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/runtime/layout/test_line_index.cpp`:

```cpp
#include "runtime/layout/line_index.h"
#include "runtime/layout/layout_document.h"

#include <doctest/doctest.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>

using namespace wlx::runtime::layout;
using wlx::runtime::parser::BlockType;
using Microsoft::WRL::ComPtr;

static LayoutBlock make_block(BlockType type, float top, const std::wstring& text,
                              IDWriteTextLayout* layout) {
    LayoutBlock b;
    b.type = type;
    b.rect = D2D1::RectF(0.0f, top, 100.0f, top + 10.0f);
    TextRun r;
    r.text = text;
    r.rect = b.rect;
    r.layout = layout;  // ComPtr operator= AddRefs; nullptr is fine
    b.text_runs.push_back(std::move(r));
    return b;
}

TEST_CASE("build_line_index: one logical line per simple block") {
    LayoutDocument doc;
    doc.blocks.push_back(make_block(BlockType::Heading, 0.0f, L"Title", nullptr));
    doc.blocks.push_back(make_block(BlockType::Paragraph, 20.0f, L"para", nullptr));
    doc.blocks.push_back(make_block(BlockType::ListItem, 40.0f, L"item", nullptr));

    build_line_index(doc);

    REQUIRE(doc.line_tops.size() == 3);
    CHECK(doc.line_tops[0] == doctest::Approx(0.0f));
    CHECK(doc.line_tops[1] == doctest::Approx(20.0f));
    CHECK(doc.line_tops[2] == doctest::Approx(40.0f));
}

TEST_CASE("build_line_index: table-row cells sharing a top collapse to one line") {
    LayoutDocument doc;
    doc.blocks.push_back(make_block(BlockType::TableCell, 0.0f, L"a", nullptr));
    doc.blocks.push_back(make_block(BlockType::TableCell, 0.0f, L"b", nullptr));   // same row
    doc.blocks.push_back(make_block(BlockType::TableCell, 18.0f, L"c", nullptr));  // next row

    build_line_index(doc);

    CHECK(doc.line_tops.size() == 2);
}

TEST_CASE("build_line_index: hard breaks in a paragraph add lines; soft wrap does not") {
    ComPtr<IDWriteFactory> factory;
    REQUIRE(SUCCEEDED(DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(factory.GetAddressOf()))));

    ComPtr<IDWriteTextFormat> fmt;
    REQUIRE(SUCCEEDED(factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"", fmt.GetAddressOf())));

    std::wstring text = L"line one\nline two";  // '\n' == hard break
    ComPtr<IDWriteTextLayout> tl;
    REQUIRE(SUCCEEDED(factory->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.size()), fmt.Get(),
        1000.0f, 1000.0f, tl.GetAddressOf())));

    LayoutDocument doc;
    doc.blocks.push_back(make_block(BlockType::Paragraph, 0.0f, text, tl.Get()));

    build_line_index(doc);

    REQUIRE(doc.line_tops.size() == 2);
    CHECK(doc.line_tops[0] == doctest::Approx(0.0f));
    CHECK(doc.line_tops[1] > 0.0f);  // second segment sits below the first
}
```

- [ ] **Step 2: Create the header**

Create `src/runtime/layout/line_index.h`:

```cpp
#pragma once

#include "runtime/layout/layout_document.h"

namespace wlx::runtime::layout {

// Fill doc.line_tops with one document-space Y per logical line, in order.
//
// Numbering model (viewport-independent):
//   * every block contributes at least one logical line at its text-run top;
//   * Paragraph / CodeFence blocks contribute one additional line per embedded
//     hard break ('\n') — located via IDWriteTextLayout::HitTestTextPosition;
//   * soft word-wrap never advances the count;
//   * blocks sharing a top (table-row cells) collapse to a single line.
void build_line_index(LayoutDocument& doc);

}  // namespace wlx::runtime::layout
```

- [ ] **Step 3: Create the implementation**

Create `src/runtime/layout/line_index.cpp`:

```cpp
#include "runtime/layout/line_index.h"

#include <cmath>

namespace wlx::runtime::layout {

using parser::BlockType;

void build_line_index(LayoutDocument& doc) {
    doc.line_tops.clear();
    constexpr float kEps = 0.5f;
    float last = -1.0e9f;

    auto push = [&](float y) {
        if (std::fabs(y - last) > kEps) {
            doc.line_tops.push_back(y);
            last = y;
        }
    };

    for (const auto& block : doc.blocks) {
        if (block.text_runs.empty()) {
            push(block.rect.top);
            continue;
        }

        const TextRun& run = block.text_runs.front();
        const float base = run.rect.top;  // code fences inset the run below the block top
        push(base);

        // Only paragraphs and code fences carry meaningful hard breaks. Other
        // block types (table cells, list items, headings) stay one line so a
        // multi-line table cell can't desync the per-row collapse above.
        const bool enumerate =
            (block.type == BlockType::Paragraph || block.type == BlockType::CodeFence);
        if (!enumerate || !run.layout) continue;

        const std::wstring& text = run.text;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] != L'\n') continue;
            UINT32 offset = static_cast<UINT32>(i + 1);  // first char after the break
            DWRITE_HIT_TEST_METRICS m = {};
            float px = 0.0f, py = 0.0f;
            if (SUCCEEDED(run.layout->HitTestTextPosition(offset, FALSE, &px, &py, &m)))
                push(base + py);
        }
    }
}

}  // namespace wlx::runtime::layout
```

- [ ] **Step 4: Register in CMake**

In `src/runtime/CMakeLists.txt`, add to the `add_library(wlx-core STATIC ...)` list (after `layout/layout_engine.cpp`):

```cmake
    layout/line_index.cpp
```

In `tests/CMakeLists.txt`, add to the `add_executable(tests ...)` list (after `runtime/layout/test_layout_engine.cpp`):

```cmake
    runtime/layout/test_line_index.cpp
```

- [ ] **Step 5: Build and run the test**

Run: `cmake --preset conan-default; cmake --build --preset conan-release --target tests`
Then: `./build/Release/tests.exe --test-case="build_line_index*"`
Expected: 3 test cases PASS.

- [ ] **Step 6: Commit**

```bash
git add src/runtime/layout/line_index.h src/runtime/layout/line_index.cpp tests/runtime/layout/test_line_index.cpp src/runtime/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(layout): build_line_index — logical-line Y index for jump + gutter"
```

---

### Task 3: goto_line module (prompt state + scroll)

**Files:**
- Create: `src/runtime/host/goto_line.h`
- Create: `src/runtime/host/goto_line.cpp`
- Create: `tests/runtime/host/test_goto_line.cpp`
- Modify: `src/runtime/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/runtime/host/test_goto_line.cpp`:

```cpp
#include "runtime/host/goto_line.h"

#include <doctest/doctest.h>

#include <vector>

using namespace wlx::runtime::host;

TEST_CASE("goto_handle_key: digits accumulate, Backspace pops, Enter jumps") {
    GotoPrompt p;
    p.active = true;

    CHECK(goto_handle_key(p, '4').action == GotoAction::Redraw);
    CHECK(p.buffer == L"4");
    goto_handle_key(p, '2');
    CHECK(p.buffer == L"42");
    goto_handle_key(p, VK_BACK);
    CHECK(p.buffer == L"4");
    goto_handle_key(p, '7');
    CHECK(p.buffer == L"47");

    GotoStep s = goto_handle_key(p, VK_RETURN);
    CHECK(s.action == GotoAction::Jump);
    CHECK(s.line == 47);
    CHECK(p.active == false);
    CHECK(p.buffer.empty());
}

TEST_CASE("goto_handle_key: numpad digits work; Escape closes without jumping") {
    GotoPrompt p;
    p.active = true;
    goto_handle_key(p, VK_NUMPAD9);
    CHECK(p.buffer == L"9");

    GotoStep s = goto_handle_key(p, VK_ESCAPE);
    CHECK(s.action == GotoAction::Close);
    CHECK(p.active == false);
    CHECK(p.buffer.empty());
}

TEST_CASE("goto_handle_key: Enter with empty buffer closes, does not jump") {
    GotoPrompt p;
    p.active = true;
    GotoStep s = goto_handle_key(p, VK_RETURN);
    CHECK(s.action == GotoAction::Close);
    CHECK(p.active == false);
}

TEST_CASE("goto_handle_key: buffer capped at 7 digits; non-digits ignored") {
    GotoPrompt p;
    p.active = true;
    for (int i = 0; i < 10; ++i) goto_handle_key(p, '9');
    CHECK(p.buffer.size() == 7);

    GotoStep s = goto_handle_key(p, 'X');
    CHECK(s.action == GotoAction::Ignore);
    CHECK(p.buffer.size() == 7);
}

TEST_CASE("line_scroll_target: clamps to [1,total] and to max_scroll_y") {
    std::vector<float> tops = {0.0f, 100.0f, 200.0f, 300.0f};  // 4 lines
    float max_scroll = 250.0f;

    CHECK(line_scroll_target(tops, 0, max_scroll) == doctest::Approx(0.0f));    // -> line 1
    CHECK(line_scroll_target(tops, 1, max_scroll) == doctest::Approx(0.0f));
    CHECK(line_scroll_target(tops, 3, max_scroll) == doctest::Approx(200.0f));
    CHECK(line_scroll_target(tops, 4, max_scroll) == doctest::Approx(250.0f));  // 300 clamped
    CHECK(line_scroll_target(tops, 99, max_scroll) == doctest::Approx(250.0f)); // -> line 4 clamped
    CHECK(line_scroll_target({}, 5, max_scroll) == doctest::Approx(0.0f));      // empty
}
```

- [ ] **Step 2: Create the header**

Create `src/runtime/host/goto_line.h`:

```cpp
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "runtime/host/scroll_handler.h"

namespace wlx::runtime::host {

// Inline "go to line" prompt state. One per window.
struct GotoPrompt {
    bool active = false;
    std::wstring buffer;  // digits typed so far
};

enum class GotoAction { Ignore, Redraw, Jump, Close };

struct GotoStep {
    GotoAction action = GotoAction::Ignore;
    int line = 0;  // 1-based; valid only when action == Jump
};

// Pure state-machine step for one virtual-key while the prompt is active.
// Mutates `p`; returns what the host should do. No Win32 side effects.
GotoStep goto_handle_key(GotoPrompt& p, unsigned vk);

// Pure: document-space scroll_y that puts logical line `n` (1-based) at the
// viewport top, clamped to [1,total] and to [0,max_scroll_y]. 0 if empty.
float line_scroll_target(const std::vector<float>& line_tops, int n, float max_scroll_y);

// Scroll the view so logical line `n` is at the top. Uses the existing
// Scrollable duck-typing (v.layout->line_tops, v.scroll_y, v.max_scroll_y).
template <Scrollable V>
void scroll_to_line(V& v, int n) {
    if (!v.layout || v.layout->line_tops.empty()) return;
    v.scroll_y = line_scroll_target(v.layout->line_tops, n, v.max_scroll_y);
    update_scrollbar(v);
    if (v.hwnd) InvalidateRect(v.hwnd, nullptr, FALSE);
}

}  // namespace wlx::runtime::host
```

- [ ] **Step 3: Create the implementation**

Create `src/runtime/host/goto_line.cpp`:

```cpp
#include "runtime/host/goto_line.h"

namespace wlx::runtime::host {

namespace {
constexpr size_t kMaxDigits = 7;  // up to 9,999,999 lines — fits int
}

GotoStep goto_handle_key(GotoPrompt& p, unsigned vk) {
    if (!p.active) return {GotoAction::Ignore, 0};

    if (vk >= '0' && vk <= '9') {
        if (p.buffer.size() < kMaxDigits)
            p.buffer.push_back(static_cast<wchar_t>(vk));
        return {GotoAction::Redraw, 0};
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        if (p.buffer.size() < kMaxDigits)
            p.buffer.push_back(static_cast<wchar_t>(L'0' + (vk - VK_NUMPAD0)));
        return {GotoAction::Redraw, 0};
    }
    if (vk == VK_BACK) {
        if (!p.buffer.empty()) p.buffer.pop_back();
        return {GotoAction::Redraw, 0};
    }
    if (vk == VK_RETURN) {
        int line = p.buffer.empty() ? 0 : _wtoi(p.buffer.c_str());
        bool jump = !p.buffer.empty();
        p.active = false;
        p.buffer.clear();
        return jump ? GotoStep{GotoAction::Jump, line} : GotoStep{GotoAction::Close, 0};
    }
    if (vk == VK_ESCAPE) {
        p.active = false;
        p.buffer.clear();
        return {GotoAction::Close, 0};
    }
    return {GotoAction::Ignore, 0};
}

float line_scroll_target(const std::vector<float>& line_tops, int n, float max_scroll_y) {
    if (line_tops.empty()) return 0.0f;
    int total = static_cast<int>(line_tops.size());
    n = std::clamp(n, 1, total);
    return std::clamp(line_tops[static_cast<size_t>(n - 1)], 0.0f, max_scroll_y);
}

}  // namespace wlx::runtime::host
```

- [ ] **Step 4: Register in CMake**

In `src/runtime/CMakeLists.txt`, add to the `wlx-core` source list (with the other `host/` entries):

```cmake
    host/goto_line.cpp
```

In `tests/CMakeLists.txt`, add to the `add_executable(tests ...)` list (after `runtime/host/test_view_actions.cpp`):

```cmake
    runtime/host/test_goto_line.cpp
```

- [ ] **Step 5: Build and run the test**

Run: `cmake --build --preset conan-release --target tests`
Then: `./build/Release/tests.exe --test-case="goto_handle_key*,line_scroll_target*"`
Expected: 5 test cases PASS.

- [ ] **Step 6: Commit**

```bash
git add src/runtime/host/goto_line.h src/runtime/host/goto_line.cpp tests/runtime/host/test_goto_line.cpp src/runtime/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(host): goto_line — pure prompt state machine + scroll_to_line"
```

---

### Task 4: layout_engine reserves the gutter column

**Files:**
- Modify: `src/runtime/layout/layout_engine.h` (the `layout(...)` declaration)
- Modify: `src/runtime/layout/layout_engine.cpp:223-237`

- [ ] **Step 1: Update the declaration**

In `src/runtime/layout/layout_engine.h`, find the `layout(` method declaration and add a defaulted `gutter_width` parameter. It currently reads:

```cpp
    LayoutDocument layout(const parser::Document& doc, float viewport_width, bool wrap_code);
```

Change to:

```cpp
    LayoutDocument layout(const parser::Document& doc, float viewport_width, bool wrap_code,
                          float gutter_width = 0.0f);
```

(If the existing signature uses a different parameter name/order, keep it and append `, float gutter_width = 0.0f`.)

- [ ] **Step 2: Reserve the column in the definition**

In `src/runtime/layout/layout_engine.cpp`, replace the `layout(...)` body at lines 223-237:

```cpp
LayoutDocument LayoutEngine::layout(const Document& doc, float viewport_width, bool wrap_code,
                                    float gutter_width) {
    result_ = LayoutDocument{};
    result_.viewport_width = viewport_width;
    result_.gutter_width = gutter_width;
    wrap_code_ = wrap_code;

    float content_padding = 16.0f;
    float y = content_padding;
    float left = (gutter_width > 0.0f) ? gutter_width : content_padding;
    float right = viewport_width - content_padding;

    layout_blocks(doc.blocks, y, left, right, 0);

    result_.total_height = y + content_padding;
    return std::move(result_);
}
```

- [ ] **Step 3: Build to verify**

Run: `cmake --build --preset conan-release --target wlx-core`
Expected: builds clean. Existing callers pass no `gutter_width`, defaulting to 0 (unchanged behavior).

- [ ] **Step 4: Run the existing layout tests to confirm no regression**

Run: `./build/Release/tests.exe --test-case="*layout*"`
Expected: all existing layout tests still PASS.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/layout/layout_engine.h src/runtime/layout/layout_engine.cpp
git commit -m "feat(layout): layout() reserves an optional left gutter column"
```

---

### Task 5: RenderEngine paints the gutter and the prompt

**Files:**
- Modify: `src/runtime/render/render_engine.h` (paint signature + 2 method decls + 2 cached formats)
- Modify: `src/runtime/render/render_engine.cpp` (paint body + 2 new methods)

- [ ] **Step 1: Extend the header**

In `src/runtime/render/render_engine.h`, change the `paint` declaration (line 32-33) to:

```cpp
    void paint(const layout::LayoutDocument& layout, float scroll_y,
               layout::TextPosition sel_start = {}, layout::TextPosition sel_end = {},
               const std::wstring* goto_input = nullptr, int goto_total = 0);
```

Add `#include <string>` near the other includes if not already present.

In the `private:` method list (after `paint_bullet`), add:

```cpp
    void paint_line_numbers(const layout::LayoutDocument& layout, float scroll_y);
    void paint_goto_prompt(const std::wstring& input, int total);
```

In the private member section (after `bullet_format_`, line 85), add:

```cpp
    ComPtr<IDWriteTextFormat> line_number_format_;
    ComPtr<IDWriteTextFormat> prompt_format_;
```

- [ ] **Step 2: Wire the calls into paint()**

In `src/runtime/render/render_engine.cpp`, update the `paint` signature (line 217-218) to match the header, then add the two calls. The block loop ends at line 254; insert the gutter call before `rt_->SetTransform(...Identity...)` (line 256), and the prompt call after it:

```cpp
    }  // end of: for (block_idx ...)

    if (layout.gutter_width > 0.0f)
        paint_line_numbers(layout, scroll_y);

    rt_->SetTransform(D2D1::Matrix3x2F::Identity());

    if (goto_input)
        paint_goto_prompt(*goto_input, goto_total);

    HRESULT hr = rt_->EndDraw();
```

(Keep the existing `D2DERR_RECREATE_TARGET` handling that follows.)

- [ ] **Step 3: Implement paint_line_numbers**

Add to `src/runtime/render/render_engine.cpp` (e.g. right after the `paint(...)` function):

```cpp
void RenderEngine::paint_line_numbers(const LayoutDocument& layout, float scroll_y) {
    if (!rt_ || layout.line_tops.empty()) return;

    if (!line_number_format_) {
        dwrite_factory_->CreateTextFormat(
            theme_.fonts().code_family.c_str(), nullptr,
            DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            theme_.fonts().code_size, L"", line_number_format_.GetAddressOf());
        if (line_number_format_) {
            line_number_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            line_number_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    if (!line_number_format_) return;

    auto* brush = get_brush(theme_.palette(dark_mode_).muted);
    if (!brush) return;

    float viewport_h = dip_height();
    float line_h = theme_.fonts().code_size * 1.6f;
    float right = layout.gutter_width - 8.0f;  // small right gap before the text column

    for (size_t i = 0; i < layout.line_tops.size(); ++i) {
        float y = layout.line_tops[i];
        if (y - scroll_y + line_h < 0.0f) continue;       // above viewport
        if (y - scroll_y > viewport_h) break;             // below viewport (line_tops ascending)

        wchar_t buf[16];
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%zu", i + 1);
        D2D1_RECT_F r = D2D1::RectF(0.0f, y, right, y + line_h);
        rt_->DrawTextW(buf, static_cast<UINT32>(wcslen(buf)),
                       line_number_format_.Get(), r, brush);
    }
}
```

- [ ] **Step 4: Implement paint_goto_prompt**

Add directly after `paint_line_numbers`:

```cpp
void RenderEngine::paint_goto_prompt(const std::wstring& input, int total) {
    if (!rt_) return;

    if (!prompt_format_) {
        dwrite_factory_->CreateTextFormat(
            theme_.fonts().body_family.c_str(), nullptr,
            DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            theme_.fonts().body_size, L"", prompt_format_.GetAddressOf());
        if (prompt_format_)
            prompt_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (!prompt_format_) return;

    const auto& colors = theme_.palette(dark_mode_);
    float box_w = 260.0f;
    float box_h = theme_.fonts().body_size * 2.2f;
    float vh = dip_height();
    float x0 = 12.0f;
    float y0 = vh - box_h - 12.0f;
    D2D1_RECT_F box = D2D1::RectF(x0, y0, x0 + box_w, y0 + box_h);

    if (auto* bg = get_brush(colors.code_bg)) rt_->FillRectangle(box, bg);
    if (auto* border = get_brush(colors.muted)) rt_->DrawRectangle(box, border, 1.0f);

    wchar_t head[64];
    _snwprintf_s(head, _countof(head), _TRUNCATE, L"Go to line (1-%d): ", total);
    std::wstring text = std::wstring(head) + input + L"▏";  // trailing caret bar

    if (auto* fg = get_brush(colors.text)) {
        D2D1_RECT_F tr = D2D1::RectF(x0 + 8.0f, y0, x0 + box_w - 8.0f, y0 + box_h);
        rt_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()),
                       prompt_format_.Get(), tr, fg);
    }
}
```

- [ ] **Step 5: Build to verify**

Run: `cmake --build --preset conan-release --target wlx-core`
Expected: builds clean. (Existing `paint(...)` callers still compile — new params are defaulted.)

- [ ] **Step 6: Commit**

```bash
git add src/runtime/render/render_engine.h src/runtime/render/render_engine.cpp
git commit -m "feat(render): paint line-number gutter + inline go-to-line prompt"
```

---

### Task 6: Markdown line_numbers config flag

**Files:**
- Modify: `src/runtime/theme/theme_config.h`
- Modify: `src/runtime/theme/theme_service.cpp` (TOML load)

- [ ] **Step 1: Add the field**

In `src/runtime/theme/theme_config.h`, add to `struct ThemeConfig` (after `code_default_language`):

```cpp
    // Markdown line-number gutter (rendered logical lines). Default on.
    bool line_numbers = true;
```

- [ ] **Step 2: Parse it from TOML**

In `src/runtime/theme/theme_service.cpp`, locate where the `[general]` section is parsed (near where `detect_string` / `extensions` are read from the parsed table). Add alongside those reads:

```cpp
    if (auto v = tbl["general"]["line_numbers"].value<bool>())
        config_.line_numbers = *v;
```

(If `[general]` is parsed via a local `toml::table` handle rather than `tbl`, use the same handle the neighboring `detect_string` read uses.)

- [ ] **Step 3: Build to verify**

Run: `cmake --build --preset conan-release --target wlx-core`
Expected: builds clean.

- [ ] **Step 4: Run theme tests for no regression**

Run: `./build/Release/tests.exe --test-case="*theme*"`
Expected: existing theme tests PASS (default `line_numbers == true` doesn't break them).

- [ ] **Step 5: Commit**

```bash
git add src/runtime/theme/theme_config.h src/runtime/theme/theme_service.cpp
git commit -m "feat(theme): markdown line_numbers config flag (default on)"
```

---

### Task 7: Markdown plugin wiring (gutter + Ctrl+G prompt)

**Files:**
- Modify: `src/plugin_md/window/host_adapter.cpp` (includes, `ViewState`, `do_layout`, `WM_KEYDOWN`, `WM_PAINT`)

- [ ] **Step 1: Add includes**

Near the other `runtime/` includes at the top of `src/plugin_md/window/host_adapter.cpp`, add:

```cpp
#include "runtime/layout/line_index.h"
#include "runtime/host/goto_line.h"
```

- [ ] **Step 2: Add the prompt to ViewState**

In `struct ViewState` (line 73-107), after the search members, add:

```cpp
    wlx::runtime::host::GotoPrompt goto_prompt;
```

- [ ] **Step 3: Two-pass layout with gutter**

In `do_layout` (line 162-181), replace the single layout construction (the line that builds `layout` via `engine.layout(...)` and the `vs->layout = layout;` that follows) with the two-pass version. Final `do_layout` body:

```cpp
static void do_layout(ViewState* vs) {
    if (!vs->document || !dwrite_factory()) return;

    float viewport_width = vs->renderer ? vs->renderer->dip_width() : 1.0f;

    LayoutEngine engine(dwrite_factory(), g_theme, vs->dark_mode, g_colorizer_handle);

    // Pass 1: no gutter — lay out and count logical lines.
    auto layout = std::make_shared<LayoutDocument>(
        engine.layout(*vs->document, viewport_width, vs->wrap_text, 0.0f));
    wlx::runtime::layout::build_line_index(*layout);

    // Pass 2: if the gutter is enabled, reserve a column sized to the line count
    // and re-lay out (narrower text column changes wrapping, so rebuild the index).
    if (g_theme.config().line_numbers && !layout->line_tops.empty()) {
        int digits = static_cast<int>(std::to_wstring(layout->line_tops.size()).size());
        float gutter_w = 16.0f + g_theme.fonts().code_size * 0.62f * digits + 8.0f;
        layout = std::make_shared<LayoutDocument>(
            engine.layout(*vs->document, viewport_width, vs->wrap_text, gutter_w));
        wlx::runtime::layout::build_line_index(*layout);
    }

    vs->layout = layout;
    vs->interaction = std::make_unique<InteractionEngine>(*vs->layout);

    update_scrollbar(vs);
    vs->index_dirty = true;
}
```

(Ensure `<string>` is included for `std::to_wstring` — it is transitively, but add it to the includes if the build complains.)

- [ ] **Step 4: Handle Ctrl+G and route keys while the prompt is open**

In the `WM_KEYDOWN` case (line 658), immediately after `if (!vs) break;` (line 660), insert the active-prompt router **before** the `float page` line:

```cpp
        // Go-to-line prompt swallows all keystrokes while active.
        if (vs->goto_prompt.active) {
            auto step = wlx::runtime::host::goto_handle_key(vs->goto_prompt, (unsigned)wp);
            if (step.action == wlx::runtime::host::GotoAction::Jump)
                wlx::runtime::host::scroll_to_line(*vs, step.line);
            else if (step.action != wlx::runtime::host::GotoAction::Ignore)
                InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
```

Then add the Ctrl+G opener as the first branch of the `handled` chain — insert before the `// Ctrl+C` branch (line 666):

```cpp
        // Ctrl+G — open the go-to-line prompt
        if (wp == 'G' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            vs->goto_prompt.active = true;
            vs->goto_prompt.buffer.clear();
            InvalidateRect(hwnd, nullptr, FALSE);
            handled = true;
        }
        // Ctrl+C — copy selection
        else if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
```

(Change the existing `if (wp == 'C' ...)` to `else if (wp == 'C' ...)` as shown.)

- [ ] **Step 5: Pass the prompt to paint()**

In `WM_PAINT` (line 307), replace the `paint` call:

```cpp
            vs->renderer->paint(*vs->layout, vs->scroll_y, sel_lo, sel_hi,
                                vs->goto_prompt.active ? &vs->goto_prompt.buffer : nullptr,
                                static_cast<int>(vs->layout->line_tops.size()));
```

- [ ] **Step 6: Build the markdown plugin**

Run: `cmake --build --preset conan-release --target wlx-listerine-md`
Expected: builds clean.

- [ ] **Step 7: Manual smoke (documented, not automated)**

Build the full solution, open a markdown file in TC Lister:
- Gutter of contiguous numbers appears on the left; a wrapped paragraph shows one number; a fenced code block numbers each code line.
- **Ctrl+G** opens the bottom-left prompt; type a number + Enter scrolls that line to the top; Esc cancels; out-of-range clamps.
- If Ctrl+G does nothing, TC is swallowing it — see Task 8 note about the `WH_GETMESSAGE` fallback (`host_integration.h`).

- [ ] **Step 8: Commit**

```bash
git add src/plugin_md/window/host_adapter.cpp
git commit -m "feat(plugin-md): line-number gutter + Ctrl+G go-to-line prompt"
```

---

### Task 8: Colorizer plugin wiring (Ctrl+G prompt)

**Files:**
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp` (includes, `ColorViewState`, layout build, `WM_KEYDOWN`, `WM_PAINT`)

The colorizer already draws its own line-number gutter (via `bullet_text`), so it leaves `gutter_width == 0` and only needs `line_tops` (for jumping) plus the prompt wiring.

- [ ] **Step 1: Add includes**

Near the other `runtime/` includes at the top of `src/plugin_colorizer/window/colorizer_host_adapter.cpp`, add:

```cpp
#include "runtime/layout/line_index.h"
#include "runtime/host/goto_line.h"
```

- [ ] **Step 2: Add the prompt to ColorViewState**

In `struct ColorViewState` (line 108), add (near `scroll_y` / search members):

```cpp
    wlx::runtime::host::GotoPrompt goto_prompt;
```

- [ ] **Step 3: Build the line index after layout_source**

In the layout-building function, immediately after the `layout` shared_ptr is constructed from `layout_source(...)` (around line 322-324), add:

```cpp
    wlx::runtime::layout::build_line_index(*layout);
```

- [ ] **Step 4: Handle Ctrl+G and route keys (identical pattern to the md plugin)**

In the colorizer `WM_KEYDOWN` case (line 792), after `if (!vs) break;` (line 794), insert the router before the `float page` line:

```cpp
        // Go-to-line prompt swallows all keystrokes while active.
        if (vs->goto_prompt.active) {
            auto step = wlx::runtime::host::goto_handle_key(vs->goto_prompt, (unsigned)wp);
            if (step.action == wlx::runtime::host::GotoAction::Jump)
                wlx::runtime::host::scroll_to_line(*vs, step.line);
            else if (step.action != wlx::runtime::host::GotoAction::Ignore)
                InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
```

Then add the Ctrl+G opener before the `// Ctrl+C` branch (line 800):

```cpp
        // Ctrl+G — open the go-to-line prompt
        if (wp == 'G' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            vs->goto_prompt.active = true;
            vs->goto_prompt.buffer.clear();
            InvalidateRect(hwnd, nullptr, FALSE);
            handled = true;
        }
        // Ctrl+C — copy selection
        else if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
```

(Change the existing `if (wp == 'C' ...)` to `else if (wp == 'C' ...)`.)

- [ ] **Step 5: Pass the prompt to paint()**

In the colorizer `WM_PAINT` (line 470), replace the `paint` call:

```cpp
            vs->renderer->paint(*vs->layout, vs->scroll_y, sel_lo, sel_hi,
                                vs->goto_prompt.active ? &vs->goto_prompt.buffer : nullptr,
                                static_cast<int>(vs->layout->line_tops.size()));
```

- [ ] **Step 6: Build the colorizer plugin**

Run: `cmake --build --preset conan-release --target wlx-listerine-colorizer`
Expected: builds clean.

- [ ] **Step 7: Manual smoke**

Open a source file in TC Lister via the colorizer: **Ctrl+G** opens the prompt; Enter jumps to that source line (its number is visible in the existing gutter); Esc cancels.

- [ ] **Step 8: Commit**

```bash
git add src/plugin_colorizer/window/colorizer_host_adapter.cpp
git commit -m "feat(plugin-colorizer): Ctrl+G go-to-line prompt"
```

---

### Task 9: Visual regression golden for the markdown gutter

**Files:**
- Create: `test_data/cases/NN_line_numbers/` (input `.md` + generated golden), following the existing case layout.

- [ ] **Step 1: Inspect the existing case format**

Run: `ls test_data/cases/ | head; ls test_data/cases/01_headings_atx`
Confirm the per-case file layout (input `.md`, `*_chrome.png` golden, any metadata) before adding a new one.

- [ ] **Step 2: Add a case exercising the gutter**

Create a small markdown input under a new case dir (e.g. `test_data/cases/28_line_numbers/`) containing a heading, a multi-line wrapping paragraph, a fenced code block, and a short list — so the golden shows contiguous numbers, a shared number across a wrapped paragraph, and per-line numbers in the code fence.

- [ ] **Step 3: Generate the golden**

Run: `bun run update-goldens -- 28_line_numbers`
(Adjust the case name to match Step 2.) Confirm the produced PNG shows the gutter as intended.

- [ ] **Step 4: Run the visual suite**

Run: `./scripts/visual-test.sh`
Expected: the new case PASSes (>= 95% similarity) and no existing case regresses. **Note:** existing markdown goldens now include the gutter, so they must be regenerated if the suite compares against pre-gutter PNGs — if many cases fail purely due to the new left column, run `bun run update-goldens` and review the diffs to confirm the only change is the gutter.

- [ ] **Step 5: Commit**

```bash
git add test_data/cases
git commit -m "test(visual): golden for markdown line-number gutter"
```

---

## Self-Review

**Spec coverage:**
- Ctrl+G prompt (both plugins) → Tasks 3, 7, 8. ✓
- Rendered logical-line numbering model → Task 2 (`build_line_index`), tested. ✓
- Markdown gutter (reserve column + paint) → Tasks 4, 5, 7. ✓
- Shared `line_tops` driving jump + gutter + total → Tasks 1, 2, 5. ✓
- Inline screen-space prompt → Task 5 (`paint_goto_prompt`). ✓
- Config `line_numbers` default on → Task 6. ✓
- Colorizer reuses the index, keeps its gutter → Task 8. ✓
- Edge cases (clamp, empty, cap, non-digit) → Task 3 tests. ✓
- Hotkey-delivery verification + `WH_GETMESSAGE` fallback → Tasks 7/8 manual smoke notes. ✓
- Tests (line index, jump math, state machine, visual golden) → Tasks 2, 3, 9. ✓

**Known simplifications (documented, matching the spec's non-goals/edge calls):**
- Hard breaks count only inside Paragraph/CodeFence blocks; a `<br>` inside a list item or blockquote counts as one line. (Spec flagged tables/containers as the judgment-call area.)
- Tables: one logical line per row via top-collapse; a multi-line cell stays one row-line.
- Gutter width uses a monospace 0.62-em estimate per digit + padding (never clips; may be marginally wide). The colorizer's exact measurement is unchanged.

**Type consistency:** `GotoPrompt`, `GotoStep`, `GotoAction`, `goto_handle_key`, `line_scroll_target`, `scroll_to_line`, `build_line_index`, `line_tops`, `gutter_width`, and the `paint(..., const std::wstring*, int)` signature are used identically across Tasks 1-8. `BlockType::{Paragraph,CodeFence,TableCell,Heading,ListItem}` match `block_node.h`. Font fields (`body_family`, `code_family`, `body_size`, `code_size`) match `font_config.h`.
