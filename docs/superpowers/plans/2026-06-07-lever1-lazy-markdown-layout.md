# Lever 1 — Viewport-Lazy Markdown Layout (estimate + reflow) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop creating an `IDWriteTextLayout` for every markdown block in `ListLoadW`; create them only for blocks in (or near) the viewport, so first-open of a large markdown file drops from ~385 ms (1 MB) to tens of ms.

**Architecture:** The markdown layout engine currently walks the block tree and, for each block, calls `CreateTextLayout` + `GetMetrics` to get the block's height, which the running `y` cursor needs. We split this into two phases. **(1) Estimate pass** (cheap, in `ListLoadW`): walk the tree, build *skeleton* `LayoutBlock`s with an **estimated** height and a stored *recipe* (everything needed to build the real layout later), and install a `materialize_block` closure. Flat top-level blocks (Paragraph / Heading / CodeFence / HR) are made lazy; container subtrees (List / BlockQuote / Table) stay **eager** (measured up front) — this keeps reflow provably correct (see Invariants). **(2) Materialize + reflow** (per paint, host-driven): the host materializes only the visible window's real layouts; when a real height differs from its estimate, every *later* block is translated down by the delta (a pure Y-shift — correct because growth only ever happens at a flat block, never inside an overlapping container). `total_height`/scrollbar refine as you scroll (bounded drift; Lever 2 removes it via background measurement).

**Tech Stack:** C++20, Direct2D/DirectWrite (`IDWriteTextLayout`), `ComPtr`, doctest, CMake/Conan/MSVC.

---

## Invariants (memory safety & correctness — priority #1; do not violate)

1. **Recipe pointer lifetime.** A lazy block's recipe holds `const std::vector<parser::InlineNode>*` pointing into the `Document`'s `BlockNode` tree. The `MdMaterializeCtx` therefore holds a `std::shared_ptr<const parser::Document>` to keep that tree alive for as long as the `LayoutDocument` (and its `materialize_block` closure) lives. **Never** build recipes that point into a `Document` the ctx does not own a `shared_ptr` to.
2. **Reflow is Y-translation only.** `materialize_block` may change a block's height. The only blocks made lazy are **flat top-level** blocks, which are emitted in strict `index == ascending-Y` order with no Y-overlap. Therefore a height change at block *i* is corrected by translating blocks *i+1…end* down by `delta` (Y fields only; X is width-derived and unaffected). Container blocks (BlockQuote container spans its children; Table cells share a row Y-span) are **eager**, so they never trigger a delta and are only ever translated as rigid units — never resized. **Do not make List/BlockQuote/Table lazy in this plan.**
3. **Idempotent materialization.** `materialize_block` must early-return if `lb.text_runs[0].layout` is already set. The host pre-materializes the visible set; `RenderEngine::paint` may also call the hook — the second call must be a no-op.
4. **Eager path unchanged.** `LayoutEngine::layout(..., lazy=false)` (the default) must remain byte-for-byte behavior-identical. All existing `tests.exe` cases call it without `lazy` and must stay green. Lazy is opt-in by the host only.
5. **Estimates are never trusted for paint.** Only *materialized* blocks are painted with real glyphs; an unmaterialized block has `run.layout == nullptr` and `RenderEngine::paint_text_runs` already skips it (`if (!run.layout) continue;`). The estimate only drives geometry/scrollbar, never rendering.

---

## File Structure

| File | Create/Modify | Responsibility |
|------|---------------|----------------|
| `src/runtime/layout/inline_layout.h` / `.cpp` | **Create** | Free function `build_inline_layout(...)` — the body of `LayoutEngine::create_text_layout`, parameterized so both the eager path and the lazy materializer share it (DRY). |
| `src/runtime/layout/code_fence_layout.h` / `.cpp` | **Create** | Free function `build_code_fence_layout(...)` — the layout+colorize core of `layout_code_fence`, shared by eager + lazy. |
| `src/runtime/layout/md_materialize.h` / `.cpp` | **Create** | `MdMaterializeCtx`, `BlockRecipe`, `md_materialize(ctx, block, idx)`, `estimate_*_height(...)`, `apply_height_delta(...)`, `shift_block_y(...)`. |
| `src/runtime/layout/anchor_entry.h` | Modify | Add `int block_index = -1;` so lazy anchors can be re-derived after reflow. |
| `src/runtime/layout/layout_engine.h` / `.cpp` | Modify | Add `lazy` param to `layout()`; delegate `create_text_layout`→`build_inline_layout`, `layout_code_fence`→`build_code_fence_layout`; emit skeleton blocks + recipes + install the hook when lazy. |
| `src/plugin_md/window/host_adapter.cpp` | Modify | `do_layout` opts into lazy; new `materialize_viewport(vs)`; call it from `WM_PAINT` and scroll paths; recompute anchors + scrollbar after reflow. |
| `tests/runtime/layout/test_inline_layout.cpp` | **Create** | Tests for the extracted free functions. |
| `tests/runtime/layout/test_lazy_layout.cpp` | **Create** | Tests for estimate pass, materialization, reflow. |
| `tests/CMakeLists.txt` (or the test glob) | Modify | Register the two new test files if tests aren't auto-globbed. |

> **Before Task 1:** confirm whether `tests/CMakeLists.txt` globs test sources or lists them explicitly (`Grep` for `test_layout_engine` in `tests/CMakeLists.txt` and any `CMakeLists.txt` under `tests/`). If explicit, every "Create test file" task below must also add the file there; if globbed, no action needed.

---

## Task 1: Extract `build_inline_layout` (behavior-preserving refactor)

**Files:**
- Create: `src/runtime/layout/inline_layout.h`, `src/runtime/layout/inline_layout.cpp`
- Modify: `src/runtime/layout/layout_engine.cpp:109-232` (`create_text_layout`), `src/runtime/layout/layout_engine.h:48-62`
- Test: `tests/runtime/layout/test_inline_layout.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/runtime/layout/test_inline_layout.cpp`:

```cpp
#include <doctest/doctest.h>
#include "runtime/layout/inline_layout.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
#include <wrl/client.h>
#include <cstring>

using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;
using namespace wlx::runtime::theme;
using Microsoft::WRL::ComPtr;

static ComPtr<IDWriteFactory> dwf() {
    ComPtr<IDWriteFactory> f;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(f.GetAddressOf()));
    return f;
}

TEST_CASE("build_inline_layout produces a measured layout for a paragraph") {
    auto factory = dwf();
    REQUIRE(factory);
    MarkdownParser p;
    Document doc = p.parse("Hello world", 11);
    REQUIRE(!doc.blocks.empty());

    ThemeService theme;
    ComPtr<IDWriteTextFormat> body;
    factory->CreateTextFormat(theme.fonts().body_family.c_str(), nullptr,
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        theme.fonts().body_size, L"", body.GetAddressOf());
    body->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    auto r = build_inline_layout(factory.Get(), doc.blocks[0].inlines, 800.0f,
                                 theme.palette(false).text, body.Get(),
                                 /*force_bold=*/false, theme.fonts(), theme.palette(false));
    CHECK(r.layout != nullptr);
    CHECK(r.full_text == L"Hello world");
    CHECK(r.height > 0.0f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset conan-release` — Expected: **compile error** `inline_layout.h: No such file` (test cannot link).

- [ ] **Step 3: Write minimal implementation**

Create `src/runtime/layout/inline_layout.h`:

```cpp
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/layout/code_bg_rect.h"
#include "runtime/layout/color_range.h"
#include "runtime/layout/interactive_span.h"
#include "runtime/parser/document.h"
#include "runtime/theme/color_palette.h"
#include "runtime/theme/font_config.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace wlx::runtime::layout {

// Result of laying out a run of inline nodes. Identical shape to the former
// LayoutEngine::TextLayoutResult (kept there as a type alias).
struct InlineLayoutResult {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    std::wstring full_text;
    std::vector<InteractiveSpan> spans;
    std::vector<ColorRange> color_ranges;
    std::vector<CodeBgRect> code_bg_rects;
    float width = 0;
    float height = 0;
};

// Build an IDWriteTextLayout from inline nodes, collecting interactive spans and
// inline-code background rects. Free function so both the eager layout pass and
// the lazy materializer can call it (DRY). Pure w.r.t. the engine — depends only
// on its arguments. `fonts`/`colors` supply code-family/link colors.
InlineLayoutResult build_inline_layout(
    IDWriteFactory* dwrite,
    const std::vector<parser::InlineNode>& inlines,
    float max_width,
    uint32_t default_color,
    IDWriteTextFormat* format,
    bool force_bold,
    const theme::FontConfig& fonts,
    const theme::ColorPalette& colors);

}  // namespace wlx::runtime::layout
```

> Verify the include paths for `color_palette.h` / `font_config.h` exist (`Grep` for `struct FontConfig` and `struct ColorPalette` to get the exact headers; theme_service.h may aggregate them). Adjust includes to the real headers.

Create `src/runtime/layout/inline_layout.cpp` by moving the **exact body** of `LayoutEngine::create_text_layout` (`layout_engine.cpp:113-231`) into it, replacing member access:
- `body_format_.Get()` → the `format` arg is already resolved by the caller; the function receives a non-null `format` (caller passes `body_format_` or a size-specific format). Remove the `format ? format : body_format_.Get()` fallback — require non-null `format`.
- `fonts_.code_family` / `fonts_.code_size` → `fonts.code_family` / `fonts.code_size`.
- `colors_.link` → `colors.link`.
- `dwrite_->` → `dwrite->`.
- Return type `InlineLayoutResult` instead of `TextLayoutResult`.

- [ ] **Step 4: Make `create_text_layout` delegate (keep the eager path identical)**

In `layout_engine.h`, replace the `TextLayoutResult` struct definition with an alias and keep the method signature:

```cpp
#include "runtime/layout/inline_layout.h"
// ...
using TextLayoutResult = InlineLayoutResult;
```

In `layout_engine.cpp`, replace the entire `create_text_layout` body with:

```cpp
LayoutEngine::TextLayoutResult LayoutEngine::create_text_layout(
    const std::vector<InlineNode>& inlines, float max_width, uint32_t default_color,
    IDWriteTextFormat* format, bool force_bold) {
    IDWriteTextFormat* fmt = format ? format : body_format_.Get();
    return build_inline_layout(dwrite_, inlines, max_width, default_color, fmt,
                               force_bold, fonts_, colors_);
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe`
Expected: PASS — the new `build_inline_layout` case **and** all pre-existing `test_layout_engine.cpp` cases (the eager path is unchanged).

- [ ] **Step 6: Commit**

```bash
git add src/runtime/layout/inline_layout.h src/runtime/layout/inline_layout.cpp \
        src/runtime/layout/layout_engine.h src/runtime/layout/layout_engine.cpp \
        tests/runtime/layout/test_inline_layout.cpp
git commit -m @'
refactor(layout): extract build_inline_layout free function

Shared by the eager layout pass and (next) the lazy materializer. No
behavior change — create_text_layout now delegates.
'@
```

---

## Task 2: Extract `build_code_fence_layout` (behavior-preserving refactor)

**Files:**
- Create: `src/runtime/layout/code_fence_layout.h`, `src/runtime/layout/code_fence_layout.cpp`
- Modify: `src/runtime/layout/layout_engine.cpp:520-649` (`layout_code_fence`)
- Test: `tests/runtime/layout/test_inline_layout.cpp` (append)

- [ ] **Step 1: Write the failing test**

Append to `tests/runtime/layout/test_inline_layout.cpp`:

```cpp
#include "runtime/layout/code_fence_layout.h"

TEST_CASE("build_code_fence_layout measures a fenced block") {
    auto factory = dwf();
    REQUIRE(factory);
    ThemeService theme;
    ComPtr<IDWriteTextFormat> code;
    factory->CreateTextFormat(theme.fonts().code_family.c_str(), nullptr,
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        theme.fonts().code_size, L"", code.GetAddressOf());
    code->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    CodeFenceInput in;
    in.code_text = L"int x = 1;\nint y = 2;";
    in.code_language = "";
    in.max_width = 760.0f;
    in.wrap_code = false;
    in.dark_mode = false;
    in.core = nullptr;        // no colorizer -> plain layout, still measures
    in.default_language = "";
    auto r = build_code_fence_layout(factory.Get(), code.Get(), in, theme.palette(false));
    CHECK(r.layout != nullptr);
    CHECK(r.height > 0.0f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset conan-release`
Expected: compile error `code_fence_layout.h: No such file`.

- [ ] **Step 3: Write minimal implementation**

Create `src/runtime/layout/code_fence_layout.h`:

```cpp
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/layout/color_range.h"
#include "runtime/theme/color_palette.h"
#include "wlx_core/abi.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace wlx::runtime::layout {

struct CodeFenceInput {
    std::wstring code_text;       // already trailing-newline-stripped by caller
    std::string  code_language;   // from the fence info string (may be empty)
    std::string  default_language;// theme.config().code_default_language
    float        max_width = 0;   // right - left - 2*padding
    bool         wrap_code = false;
    bool         dark_mode = false;
    WlxCore*     core = nullptr;
};

struct CodeFenceLayoutResult {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    std::vector<ColorRange> color_ranges;
    float height = 0;             // metrics.height (NOT including padding)
};

// Builds the code-fence text layout and resolves syntax color ranges via the
// core colorizer ABI. Shared by the eager layout_code_fence and the lazy
// materializer. `code_format` must already have wrapping set to NO_WRAP.
CodeFenceLayoutResult build_code_fence_layout(
    IDWriteFactory* dwrite,
    IDWriteTextFormat* code_format,
    const CodeFenceInput& in,
    const theme::ColorPalette& colors);

}  // namespace wlx::runtime::layout
```

Create `src/runtime/layout/code_fence_layout.cpp` by moving the body of `layout_code_fence` **from the `CreateTextLayout` call through the `GetMetrics`/color-range computation** (`layout_engine.cpp:533-629`) into it. Specifically the function:
1. `dwrite->CreateTextLayout(in.code_text...)` with `code_format`, `in.max_width`, height `100000.0f`.
2. `if (in.wrap_code) text_layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);`
3. The full `if (in.core)` syntax-highlighting block (lines 545-624) verbatim, with `core_`→`in.core`, `node.code_language`→`in.code_language`, `theme_.config().code_default_language`→`in.default_language`, `dark_mode_`→`in.dark_mode`. (`colors` is unused inside this block — it is here for symmetry/future use; keep the parameter, it is harmless.)
4. `text_layout->GetMetrics(&metrics); result.height = metrics.height;` and `result.color_ranges = std::move(color_ranges);`.

- [ ] **Step 4: Make `layout_code_fence` delegate**

Replace `layout_engine.cpp:533-644` so `layout_code_fence` builds the `CodeFenceInput`, calls `build_code_fence_layout`, then assembles the `LayoutBlock` exactly as before (background, `run.rect` with padding, `block_height = result.height + padding*2`):

```cpp
    CodeFenceInput in;
    in.code_text = code_text;
    in.code_language.clear();
    for (wchar_t wc : node.code_language) in.code_language += static_cast<char>(wc);
    in.default_language = theme_.config().code_default_language;
    in.max_width = right - left - padding * 2;
    in.wrap_code = wrap_code_;
    in.dark_mode = dark_mode_;
    in.core = core_;
    auto res = build_code_fence_layout(dwrite_, code_format_.Get(), in, colors_);
    if (!res.layout) return;

    float block_height = res.height + padding * 2;
    LayoutBlock lb;
    lb.type = BlockType::CodeFence;
    lb.rect = D2D1::RectF(left, y, right, y + block_height);
    lb.has_background = true;
    lb.background_color = colors_.code_bg;
    TextRun run;
    run.text = code_text;
    run.rect = D2D1::RectF(left + padding, y + padding, right - padding, y + padding + res.height);
    run.layout = res.layout;
    run.color = colors_.text;
    run.is_code = true;
    run.color_ranges = std::move(res.color_ranges);
    lb.text_runs.push_back(std::move(run));
    y += block_height + spacing_.paragraph_spacing;
    result_.blocks.push_back(std::move(lb));
```

> The `code_language` (wchar→char) and `code_text` (concat + trailing-`\n` strip) preamble at `layout_engine.cpp:524-531` stays in `layout_code_fence`.

- [ ] **Step 5: Run tests**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe`
Expected: PASS (new case + all existing code-fence cases).

- [ ] **Step 6: Visual regression checkpoint (refactor must be pixel-identical)**

Run: `./scripts/visual-test.sh`
Expected: all 27 cases ≥ 95% (unchanged from baseline — this is a pure refactor).

- [ ] **Step 7: Commit**

```bash
git add src/runtime/layout/code_fence_layout.h src/runtime/layout/code_fence_layout.cpp \
        src/runtime/layout/layout_engine.cpp tests/runtime/layout/test_inline_layout.cpp
git commit -m @'
refactor(layout): extract build_code_fence_layout free function

Shared by eager layout_code_fence and (next) the lazy materializer.
No behavior change; visual regression unchanged.
'@
```

---

## Task 3: Add `AnchorEntry::block_index`

**Files:**
- Modify: `src/runtime/layout/anchor_entry.h`
- Test: covered by Task 5/existing anchor tests.

- [ ] **Step 1: Add the field**

Edit `src/runtime/layout/anchor_entry.h`:

```cpp
struct AnchorEntry {
    std::wstring slug;
    float y_offset = 0;
    int block_index = -1;  // owning Heading block; -1 in eager mode (anchors exact).
                           // Lazy mode sets it so y_offset can be re-derived after reflow.
};
```

- [ ] **Step 2: Verify existing anchor tests still pass**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe -tc="Heading produces anchor"`
Expected: PASS (additive field, default `-1`).

- [ ] **Step 3: Commit**

```bash
git add src/runtime/layout/anchor_entry.h
git commit -m "feat(layout): AnchorEntry carries owning block index for lazy reflow"
```

---

## Task 4: Estimation + materialize context types

**Files:**
- Create: `src/runtime/layout/md_materialize.h`, `src/runtime/layout/md_materialize.cpp`
- Test: `tests/runtime/layout/test_lazy_layout.cpp`

- [ ] **Step 1: Write the failing test (estimation behavior)**

Create `tests/runtime/layout/test_lazy_layout.cpp`:

```cpp
#include <doctest/doctest.h>
#include "runtime/layout/md_materialize.h"
#include "runtime/theme/theme_service.h"

using namespace wlx::runtime::layout;
using namespace wlx::runtime::theme;

TEST_CASE("estimate_inline_height grows with text length and shrinks with width") {
    ThemeService theme;
    float body = theme.fonts().body_size;
    float lh = body * theme.spacing().line_height_factor;

    // One short line fits on a single line at any reasonable width.
    float h_short = estimate_inline_height(/*char_count=*/5, /*avg_advance=*/body * 0.5f,
                                           /*max_width=*/800.0f, /*line_height=*/lh);
    CHECK(h_short == doctest::Approx(lh));

    // A long string wraps to multiple lines; narrower width -> taller.
    float h_wide   = estimate_inline_height(400, body * 0.5f, 800.0f, lh);
    float h_narrow = estimate_inline_height(400, body * 0.5f, 200.0f, lh);
    CHECK(h_narrow > h_wide);
    CHECK(h_wide >= lh);
}

TEST_CASE("estimate_code_fence_height is proportional to line count") {
    float lh = 18.0f, pad = 8.0f;
    CHECK(estimate_code_fence_height(/*lines=*/1, lh, pad) == doctest::Approx(lh + 2 * pad));
    CHECK(estimate_code_fence_height(10, lh, pad) == doctest::Approx(10 * lh + 2 * pad));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset conan-release`
Expected: compile error `md_materialize.h: No such file`.

- [ ] **Step 3: Write the types + estimation**

Create `src/runtime/layout/md_materialize.h`:

```cpp
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/layout/layout_document.h"
#include "runtime/parser/document.h"
#include "runtime/theme/color_palette.h"
#include "runtime/theme/font_config.h"
#include "wlx_core/abi.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wlx::runtime::layout {

// --- estimation (no IDWriteTextLayout created) ---

// Estimated height of an inline run: ceil(estimated_width / max_width) lines,
// each `line_height` tall, minimum one line.
float estimate_inline_height(int char_count, float avg_advance,
                             float max_width, float line_height);

// Estimated height of a no-wrap code fence: lines * code_line_height + 2*padding.
float estimate_code_fence_height(int line_count, float code_line_height, float padding);

// --- deferred materialization ---

struct BlockRecipe {
    enum class Kind { None, Inline, CodeFence } kind = Kind::None;

    // Kind::Inline (Paragraph / Heading)
    const std::vector<parser::InlineNode>* inlines = nullptr;  // into ctx.document
    float max_width = 0;
    uint32_t default_color = 0;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;  // null => ctx.body_format
    bool force_bold = false;
    float left = 0;     // doc-space x of the run (for span offsetting)
    float right = 0;

    // Kind::CodeFence
    std::wstring code_text;
    std::string  code_language;
    bool         wrap_code = false;
    float        code_left = 0;
    float        code_padding = 0;
    float        code_right = 0;
};

struct MdMaterializeCtx {
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite;
    theme::FontConfig fonts;
    theme::ColorPalette colors;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> body_format;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> code_format;
    WlxCore* core = nullptr;
    bool dark_mode = false;
    std::string default_language;
    std::shared_ptr<const parser::Document> document;  // lifetime anchor (Invariant 1)
    std::vector<BlockRecipe> recipes;                  // indexed by block index
};

// Build the real IDWriteTextLayout for block `idx` and update its measured
// height in place (lb.rect.bottom, run.rect, spans offset to doc coords).
// Idempotent: no-op if already materialized or recipe is None (Invariant 3).
void md_materialize(MdMaterializeCtx& ctx, LayoutBlock& lb, int idx);

// --- reflow (Y-translation only; Invariant 2) ---

// Translate every absolute-Y field of one block by dy.
void shift_block_y(LayoutBlock& lb, float dy);

// A block at `from_idx` changed height by `delta`. Translate all later blocks,
// anchors below it, and total_height. Does NOT rebuild line_tops (caller does).
void apply_height_delta(LayoutDocument& doc, int from_idx, float delta);

}  // namespace wlx::runtime::layout
```

Create `src/runtime/layout/md_materialize.cpp` (estimation parts first; `md_materialize`/reflow filled in Task 6):

```cpp
#include "runtime/layout/md_materialize.h"
#include "runtime/layout/inline_layout.h"
#include "runtime/layout/code_fence_layout.h"

#include <algorithm>
#include <cmath>

namespace wlx::runtime::layout {

float estimate_inline_height(int char_count, float avg_advance,
                             float max_width, float line_height) {
    if (max_width <= 1.0f) return line_height;
    float est_width = static_cast<float>(std::max(0, char_count)) * avg_advance;
    int lines = std::max(1, static_cast<int>(std::ceil(est_width / max_width)));
    return static_cast<float>(lines) * line_height;
}

float estimate_code_fence_height(int line_count, float code_line_height, float padding) {
    int lines = std::max(1, line_count);
    return static_cast<float>(lines) * code_line_height + 2.0f * padding;
}

void shift_block_y(LayoutBlock& lb, float dy) {
    lb.rect.top += dy;
    lb.rect.bottom += dy;
    for (auto& r : lb.text_runs) { r.rect.top += dy; r.rect.bottom += dy; }
    lb.bullet_pos.y += dy;
    for (auto& s : lb.spans) { s.rect.top += dy; s.rect.bottom += dy; }
    if (lb.has_trailing_ws) { lb.trailing_ws_rect.top += dy; lb.trailing_ws_rect.bottom += dy; }
    // ws_markers (y relative to run origin) and indent_guides (absolute x, y read
    // from rect at paint) carry no absolute-y state -> nothing to shift.
}

void apply_height_delta(LayoutDocument& doc, int from_idx, float delta) {
    if (delta == 0.0f) return;
    float old_bottom = doc.blocks[from_idx].rect.bottom - delta;  // pre-grow bottom
    for (int i = from_idx + 1; i < static_cast<int>(doc.blocks.size()); ++i)
        shift_block_y(doc.blocks[i], delta);
    for (auto& a : doc.anchors)
        if (a.block_index > from_idx) a.y_offset += delta;  // exact: re-derived in host too
    doc.total_height += delta;
    (void)old_bottom;
}

}  // namespace wlx::runtime::layout
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe -tc="estimate_*"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/layout/md_materialize.h src/runtime/layout/md_materialize.cpp \
        tests/runtime/layout/test_lazy_layout.cpp
git commit -m "feat(layout): estimation + reflow primitives for lazy markdown layout"
```

---

## Task 5: Lazy mode in `LayoutEngine::layout`

**Files:**
- Modify: `src/runtime/layout/layout_engine.h` (add `lazy` param + a lazy ctx member), `src/runtime/layout/layout_engine.cpp`
- Modify: `src/runtime/layout/md_materialize.cpp` (implement `md_materialize`)
- Test: `tests/runtime/layout/test_lazy_layout.cpp` (append)

- [ ] **Step 1: Write the failing test**

Append to `tests/runtime/layout/test_lazy_layout.cpp`:

```cpp
#include "runtime/layout/layout_engine.h"
#include "runtime/parser/markdown_parser.h"
#include <dwrite.h>
#include <wrl/client.h>
#include <cstring>
#include <memory>
using Microsoft::WRL::ComPtr;
using namespace wlx::runtime::parser;

static ComPtr<IDWriteFactory> dwf2() {
    ComPtr<IDWriteFactory> f;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(f.GetAddressOf()));
    return f;
}

TEST_CASE("lazy layout defers paragraph layouts but keeps run text and geometry") {
    auto factory = dwf2();
    REQUIRE(factory);
    MarkdownParser p;
    auto doc = std::make_shared<Document>(p.parse("Hello world\n\nSecond para", 24));
    ThemeService theme;

    LayoutEngine eng(factory.Get(), theme, false);
    auto lazy = eng.layout(*doc, 800.0f, /*wrap_code=*/false, /*gutter=*/0.0f, /*lazy=*/true);

    REQUIRE(lazy.blocks.size() >= 2);
    // Paragraph skeleton: text present, layout deferred.
    CHECK(lazy.blocks[0].type == BlockType::Paragraph);
    CHECK(lazy.blocks[0].text_runs.size() == 1);
    CHECK(lazy.blocks[0].text_runs[0].text == L"Hello world");
    CHECK(lazy.blocks[0].text_runs[0].layout == nullptr);  // deferred
    CHECK(lazy.total_height > 0.0f);
    CHECK(lazy.materialize_block != nullptr);
}

TEST_CASE("materializing a lazy block builds the real layout (height within tolerance)") {
    auto factory = dwf2();
    REQUIRE(factory);
    MarkdownParser p;
    const char* md = "# Heading\n\nA paragraph of text that is long enough to maybe wrap once.";
    auto doc = std::make_shared<Document>(p.parse(md, std::strlen(md)));
    ThemeService theme;

    LayoutEngine eng_e(factory.Get(), theme, false);
    auto eager = eng_e.layout(*doc, 800.0f);

    LayoutEngine eng_l(factory.Get(), theme, false);
    auto lazy = eng_l.layout(*doc, 800.0f, false, 0.0f, true);

    REQUIRE(lazy.blocks.size() == eager.blocks.size());
    for (int i = 0; i < (int)lazy.blocks.size(); ++i)
        lazy.materialize_block(lazy.blocks[i], i);

    // After materializing every block, each block's measured height must match
    // the eager height closely (estimation no longer matters once materialized).
    for (int i = 0; i < (int)eager.blocks.size(); ++i) {
        float eh = eager.blocks[i].rect.bottom - eager.blocks[i].rect.top;
        float lh = lazy.blocks[i].rect.bottom - lazy.blocks[i].rect.top;
        CHECK(lh == doctest::Approx(eh).epsilon(0.02));
    }
}

TEST_CASE("lazy layout keeps list/quote/table eager (Invariant 2)") {
    auto factory = dwf2();
    REQUIRE(factory);
    MarkdownParser p;
    const char* md = "- item one\n- item two\n\n> a quote";
    auto doc = std::make_shared<Document>(p.parse(md, std::strlen(md)));
    ThemeService theme;
    LayoutEngine eng(factory.Get(), theme, false);
    auto lazy = eng.layout(*doc, 800.0f, false, 0.0f, true);

    for (auto& b : lazy.blocks) {
        if (b.type == BlockType::ListItem || b.type == BlockType::TaskList) {
            if (!b.text_runs.empty())
                CHECK(b.text_runs[0].layout != nullptr);  // eager: built up front
        }
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset conan-release`
Expected: compile error — `layout()` has no 5th `lazy` parameter.

- [ ] **Step 3: Implement `md_materialize`**

In `src/runtime/layout/md_materialize.cpp`, implement (uses Task 1/2 free functions):

```cpp
void md_materialize(MdMaterializeCtx& ctx, LayoutBlock& lb, int idx) {
    if (idx < 0 || idx >= static_cast<int>(ctx.recipes.size())) return;
    BlockRecipe& rcp = ctx.recipes[idx];
    if (rcp.kind == BlockRecipe::Kind::None) return;
    if (lb.text_runs.empty() || lb.text_runs[0].layout) return;  // Invariant 3

    if (rcp.kind == BlockRecipe::Kind::Inline) {
        IDWriteTextFormat* fmt = rcp.format ? rcp.format.Get() : ctx.body_format.Get();
        auto r = build_inline_layout(ctx.dwrite.Get(), *rcp.inlines, rcp.max_width,
                                     rcp.default_color, fmt, rcp.force_bold,
                                     ctx.fonts, ctx.colors);
        if (!r.layout) return;
        auto& run = lb.text_runs[0];
        run.layout = r.layout;
        run.color_ranges = std::move(r.color_ranges);
        run.code_bg_rects = std::move(r.code_bg_rects);
        float top = lb.rect.top;
        lb.rect.bottom = top + r.height;
        run.rect = D2D1::RectF(rcp.left, top, rcp.right, top + r.height);
        lb.spans.clear();
        for (auto& s : r.spans) {
            s.rect.left += rcp.left; s.rect.right += rcp.left;
            s.rect.top  += top;      s.rect.bottom += top;
            lb.spans.push_back(std::move(s));
        }
    } else {  // CodeFence
        ComPtr<IDWriteTextFormat> cf = ctx.code_format;
        CodeFenceInput in;
        in.code_text = rcp.code_text;
        in.code_language = rcp.code_language;
        in.default_language = ctx.default_language;
        in.max_width = rcp.code_right - rcp.code_left - rcp.code_padding * 2;
        in.wrap_code = rcp.wrap_code;
        in.dark_mode = ctx.dark_mode;
        in.core = ctx.core;
        auto r = build_code_fence_layout(ctx.dwrite.Get(), cf.Get(), in, ctx.colors);
        if (!r.layout) return;
        auto& run = lb.text_runs[0];
        run.layout = r.layout;
        run.color_ranges = std::move(r.color_ranges);
        float top = lb.rect.top;
        float block_h = r.height + rcp.code_padding * 2;
        lb.rect.bottom = top + block_h;
        run.rect = D2D1::RectF(rcp.code_left + rcp.code_padding, top + rcp.code_padding,
                               rcp.code_right - rcp.code_padding, top + rcp.code_padding + r.height);
    }
}
```

- [ ] **Step 4: Add lazy support to `LayoutEngine`**

In `layout_engine.h`:
- Add to the public `layout` signature a 5th param: `bool lazy = false`.
- Add a private member: `std::shared_ptr<MdMaterializeCtx> md_ctx_;` and `bool lazy_ = false;` (and `#include "runtime/layout/md_materialize.h"`).
- Add a private helper: `bool emit_lazy_block(const parser::BlockNode& block, float& y, float left, float right);` returning true if it emitted a lazy skeleton (Paragraph/Heading/CodeFence), false if the caller should fall back to the eager path (List/BlockQuote/Table/HR).

In `layout_engine.cpp`:
- `layout(...)`: set `lazy_ = lazy;` near the top. If `lazy`, construct `md_ctx_ = std::make_shared<MdMaterializeCtx>()` and fill its non-recipe fields (`dwrite`, `fonts`, `colors`, `body_format`, `code_format`, `core`, `dark_mode`, `default_language`). **Do not set `md_ctx_->document` here** — the engine doesn't own the `shared_ptr<Document>`; the host sets it after `layout()` returns (see Task 7, Invariant 1). Reserve `md_ctx_->recipes` to `doc.blocks` size estimate.
- `layout_block_dispatch`: when `lazy_`, route Paragraph/Heading/CodeFence to `emit_lazy_block`; route List/BlockQuote/Table/HorizontalRule to the existing eager functions unchanged. (HR has no text → eager is already trivially cheap.)
- After `layout_blocks(...)`, if `lazy_`: capture the ctx and install the hook:

```cpp
    if (lazy_) {
        auto ctx = md_ctx_;
        result_.materialize_block = [ctx](LayoutBlock& lb, int idx) {
            md_materialize(*ctx, lb, idx);
        };
    }
```

`emit_lazy_block` implements the **estimate pass**. For a Paragraph (mirror `layout_paragraph`'s spacing):

```cpp
bool LayoutEngine::emit_lazy_block(const BlockNode& block, float& y, float left, float right) {
    const float lh = fonts_.body_size * spacing_.line_height_factor;
    const float avg_body = fonts_.body_size * 0.5f;   // rough monospace-ish advance
    auto concat = [](const std::vector<InlineNode>& ins) {
        std::wstring s;
        for (auto& n : ins) {
            if (n.type == InlineType::SoftBreak) s += L' ';
            else if (n.type == InlineType::HardBreak) s += L'\n';
            else s += n.text;
        }
        return s;
    };

    if (block.type == BlockType::Paragraph) {
        std::wstring text = concat(block.inlines);
        float max_width = right - left;
        float est_h = estimate_inline_height(static_cast<int>(text.size()), avg_body, max_width, lh);
        LayoutBlock lb;
        lb.type = BlockType::Paragraph;
        lb.rect = D2D1::RectF(left, y, right, y + est_h);
        TextRun run; run.text = std::move(text); run.rect = lb.rect; run.color = colors_.text;
        lb.text_runs.push_back(std::move(run));
        BlockRecipe rcp;
        rcp.kind = BlockRecipe::Kind::Inline;
        rcp.inlines = &block.inlines; rcp.max_width = max_width;
        rcp.default_color = colors_.text; rcp.force_bold = false;
        rcp.left = left; rcp.right = right;
        md_ctx_->recipes.resize(result_.blocks.size() + 1);
        md_ctx_->recipes[result_.blocks.size()] = std::move(rcp);
        y += est_h + spacing_.paragraph_spacing;
        result_.blocks.push_back(std::move(lb));
        return true;
    }

    if (block.type == BlockType::Heading) {
        y += spacing_.heading_spacing_above;
        int level = std::clamp(block.heading_level, 1, 6);
        float font_size = kHeadingSizes[level - 1];
        auto fmt = get_body_format(font_size);
        float head_lh = font_size * spacing_.line_height_factor;
        std::wstring text = concat(block.inlines);
        float max_width = right - left;
        float est_h = estimate_inline_height(static_cast<int>(text.size()),
                                             font_size * 0.55f, max_width, head_lh);
        LayoutBlock lb;
        lb.type = BlockType::Heading; lb.heading_level = level;
        lb.rect = D2D1::RectF(left, y, right, y + est_h);
        if (level <= 2) { lb.has_bottom_rule = true; lb.bottom_rule_color = colors_.rule; }
        TextRun run; run.text = std::move(text); run.rect = lb.rect; run.color = colors_.heading;
        lb.text_runs.push_back(std::move(run));
        // Anchor (slug computed eagerly; y is the estimate, re-derived after reflow).
        std::wstring slug = slugify(block.inlines);
        if (!slug.empty())
            result_.anchors.push_back({slug, y, static_cast<int>(result_.blocks.size())});
        BlockRecipe rcp;
        rcp.kind = BlockRecipe::Kind::Inline;
        rcp.inlines = &block.inlines; rcp.max_width = max_width;
        rcp.default_color = colors_.heading; rcp.force_bold = true;
        rcp.format = fmt; rcp.left = left; rcp.right = right;
        md_ctx_->recipes.resize(result_.blocks.size() + 1);
        md_ctx_->recipes[result_.blocks.size()] = std::move(rcp);
        y += est_h + spacing_.heading_spacing_below;
        result_.blocks.push_back(std::move(lb));
        return true;
    }

    if (block.type == BlockType::CodeFence) {
        float padding = spacing_.code_padding;
        std::wstring code_text;
        for (auto& n : block.inlines) code_text += n.text;
        if (!code_text.empty() && code_text.back() == L'\n') code_text.pop_back();
        int line_count = 1 + static_cast<int>(std::count(code_text.begin(), code_text.end(), L'\n'));
        float code_lh = code_unit_line_height();  // measured once; see below
        float est_h = estimate_code_fence_height(line_count, code_lh, padding);
        LayoutBlock lb;
        lb.type = BlockType::CodeFence;
        lb.rect = D2D1::RectF(left, y, right, y + est_h);
        lb.has_background = true; lb.background_color = colors_.code_bg;
        TextRun run; run.text = code_text;
        run.rect = D2D1::RectF(left + padding, y + padding, right - padding, y + est_h - padding);
        run.color = colors_.text; run.is_code = true;
        lb.text_runs.push_back(std::move(run));
        BlockRecipe rcp;
        rcp.kind = BlockRecipe::Kind::CodeFence;
        rcp.code_text = std::move(code_text);
        for (wchar_t wc : block.code_language) rcp.code_language += static_cast<char>(wc);
        rcp.wrap_code = wrap_code_;
        rcp.code_left = left; rcp.code_right = right; rcp.code_padding = padding;
        md_ctx_->recipes.resize(result_.blocks.size() + 1);
        md_ctx_->recipes[result_.blocks.size()] = std::move(rcp);
        y += est_h + spacing_.paragraph_spacing;
        result_.blocks.push_back(std::move(lb));
        return true;
    }

    return false;  // List/BlockQuote/Table/HR -> eager
}
```

Add a tiny one-shot helper `float LayoutEngine::code_unit_line_height()` that lazily measures one code line once per engine and caches it in a member `float code_unit_lh_ = 0.0f;`:

```cpp
float LayoutEngine::code_unit_line_height() {
    if (code_unit_lh_ > 0.0f) return code_unit_lh_;
    ComPtr<IDWriteTextLayout> probe;
    dwrite_->CreateTextLayout(L"X", 1, code_format_.Get(), 1000.0f, 1000.0f, probe.GetAddressOf());
    code_unit_lh_ = fonts_.code_size * 1.3f;  // fallback
    if (probe) { DWRITE_TEXT_METRICS m; probe->GetMetrics(&m); if (m.height > 0) code_unit_lh_ = m.height; }
    return code_unit_lh_;
}
```

> The `recipes.resize(result_.blocks.size() + 1)` pattern keeps `recipes` index-aligned with `result_.blocks`. For eager blocks (List/Quote/Table/HR) emitted via the existing functions, **also** grow `recipes` to keep alignment: after `layout_block_dispatch` returns for an eager block in lazy mode, `if (md_ctx_->recipes.size() < result_.blocks.size()) md_ctx_->recipes.resize(result_.blocks.size());` (default `Kind::None`). Add this top-up in `layout_blocks`' loop when `lazy_`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe -tc="lazy*"` and `-tc="materializing*"` and `-tc="lazy layout keeps*"`
Expected: PASS. Also run the full `./build/Release/tests.exe` — the eager-path cases must remain green (Invariant 4).

- [ ] **Step 5: Commit**

```bash
git add src/runtime/layout/layout_engine.h src/runtime/layout/layout_engine.cpp \
        src/runtime/layout/md_materialize.cpp tests/runtime/layout/test_lazy_layout.cpp
git commit -m @'
feat(layout): lazy markdown layout estimate pass + materialize hook

Flat top-level blocks (paragraph/heading/code-fence) get estimated
heights + recipes; containers stay eager. materialize_block builds the
real IDWriteTextLayout on demand. Eager path unchanged.
'@
```

---

## Task 6: Reflow unit test

**Files:**
- Test: `tests/runtime/layout/test_lazy_layout.cpp` (append)

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("apply_height_delta translates only later blocks and total_height") {
    LayoutDocument doc;
    LayoutBlock a; a.rect = D2D1::RectF(0, 0, 100, 20);   a.text_runs.push_back({}); a.text_runs[0].rect = a.rect;
    LayoutBlock b; b.rect = D2D1::RectF(0, 20, 100, 40);  b.text_runs.push_back({}); b.text_runs[0].rect = b.rect;
    LayoutBlock c; c.rect = D2D1::RectF(0, 40, 100, 60);  c.text_runs.push_back({}); c.text_runs[0].rect = c.rect;
    doc.blocks = {a, b, c};
    doc.total_height = 60;
    doc.anchors.push_back({L"x", 40.0f, 2});   // anchor on block c

    // Block 0 grew by +10 (its own bottom already updated by the materializer).
    doc.blocks[0].rect.bottom = 30;
    apply_height_delta(doc, 0, 10.0f);

    CHECK(doc.blocks[1].rect.top == doctest::Approx(30));   // 20 + 10
    CHECK(doc.blocks[2].rect.top == doctest::Approx(50));   // 40 + 10
    CHECK(doc.blocks[0].rect.top == doctest::Approx(0));    // unchanged
    CHECK(doc.total_height == doctest::Approx(70));
    CHECK(doc.anchors[0].y_offset == doctest::Approx(50));  // block c moved down 10
}
```

- [ ] **Step 2: Run to verify it passes**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe -tc="apply_height_delta*"`
Expected: PASS (the helper already exists from Task 4).

- [ ] **Step 3: Commit**

```bash
git add tests/runtime/layout/test_lazy_layout.cpp
git commit -m "test(layout): reflow translates later blocks + anchors + total_height"
```

---

## Task 7: Host wiring — opt into lazy + materialize the viewport

**Files:**
- Modify: `src/plugin_md/window/host_adapter.cpp` (`do_layout`, new `materialize_viewport`, `WM_PAINT`, scroll paths)

- [ ] **Step 1: `do_layout` builds a lazy layout and anchors the Document**

In `host_adapter.cpp:197-228`, after constructing the `LayoutEngine` and before/at the `engine.layout(...)` calls, pass `lazy=true` and set the ctx's `document` so recipe pointers stay valid (Invariant 1). Because the lazy ctx lives inside the `LayoutDocument::materialize_block` closure, expose it: add a public accessor on `LayoutEngine` — `std::shared_ptr<MdMaterializeCtx> take_md_ctx();` returning `std::move(md_ctx_)` — and call it right after `engine.layout(...)`:

```cpp
    auto layout = std::make_shared<LayoutDocument>(
        engine.layout(*vs->document, viewport_width, vs->wrap_text, gutter_w, /*lazy=*/true));
    if (auto ctx = engine.take_md_ctx())     // keep recipe-pointed Document alive
        ctx->document = vs->document;        // Invariant 1
    wlx::runtime::layout::build_line_index(*layout);
```

> The gutter re-lay branch (`host_adapter.cpp:216-224`) creates a *second* engine+layout; apply the same `lazy=true` + `take_md_ctx()->document = vs->document` there. The layout cache (`store_layout`/`lookup_layout`) stores the `shared_ptr<LayoutDocument>`; its `materialize_block` closure captures the ctx (which now owns the Document), so a cached lazy layout remains self-contained across revisits. **Verify** the cache key still distinguishes the file (`parse_key`) — it does.

- [ ] **Step 2: Add `materialize_viewport`**

Add near `do_layout` in `host_adapter.cpp` (after the `#include "runtime/layout/md_materialize.h"` you must add to the include block):

```cpp
static void materialize_viewport(ViewState* vs) {
    if (!vs->layout || !vs->layout->materialize_block) return;  // eager doc -> nothing to do
    auto& doc = *vs->layout;
    float vp_top = vs->scroll_y;
    float vp_h = vs->renderer ? vs->renderer->dip_height() : 0.0f;
    float vp_bottom = vp_top + vp_h * 2.0f;  // one screenful of overscan below

    bool changed = false;
    for (int i = 0; i < static_cast<int>(doc.blocks.size()); ++i) {
        auto& b = doc.blocks[i];
        if (b.rect.bottom < vp_top) continue;          // above viewport
        if (b.rect.top > vp_bottom) break;             // below (flat blocks are Y-sorted)
        if (b.text_runs.empty() || b.text_runs[0].layout) continue;  // eager/already done
        float old_bottom = b.rect.bottom;
        doc.materialize_block(b, i);
        float delta = b.rect.bottom - old_bottom;
        if (delta != 0.0f) { apply_height_delta(doc, i, delta); changed = true; }
    }

    if (changed) {
        wlx::runtime::layout::build_line_index(doc);
        for (auto& a : doc.anchors)                    // exact re-derivation (Invariant)
            if (a.block_index >= 0 && a.block_index < (int)doc.blocks.size())
                a.y_offset = doc.blocks[a.block_index].rect.top;
        update_scrollbar(vs);                          // total_height changed
    }
}
```

> `apply_height_delta` already shifts anchors approximately; the host re-derivation above makes them exact from the (now-correct) block tops. Both are cheap; keep both (the re-derivation wins).

- [ ] **Step 3: Call it before every paint and after scroll**

In `WM_PAINT` (`host_adapter.cpp:353-369`), call `materialize_viewport(vs)` immediately before `vs->renderer->paint(...)`:

```cpp
        if (vs && vs->renderer && vs->layout) {
            if (vs->renderer->needs_recreate())
                vs->renderer->create_device_resources(hwnd);
            materialize_viewport(vs);     // <-- build visible block layouts + reflow
            vs->renderer->set_hovered_code_block(vs->hovered_code_block);
            ...
```

> Scroll paths (`WM_VSCROLL`, `WM_MOUSEWHEEL`, `handle_scroll`, `Home`/`End`, `lc_setpercent`, `scroll_to_line`, `scroll_to_match`, anchor `ScrollToAnchor`) all end with `InvalidateRect(... FALSE)` → a `WM_PAINT` → `materialize_viewport`. So no extra calls are needed there. The hit-test handlers (`WM_LBUTTONUP` link routing, `WM_MOUSEMOVE` hover) read `block.spans`; those run for the **visible** region, which the most recent `WM_PAINT` already materialized. Add one defensive `materialize_viewport(vs)` at the top of `WM_LBUTTONDOWN`/`WM_LBUTTONUP`/`WM_MOUSEMOVE` **only if** manual testing shows a first-click-before-first-paint miss; otherwise omit (YAGNI).

- [ ] **Step 4: Build + unit + visual + manual**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe && ./build/Release/colorizer-tests.exe`
Expected: all green.

Run: `./scripts/visual-test.sh`
Expected: all 27 cases ≥ 95%. (Lazy first-open renders only the viewport, but the screenshot tool paints the configured viewport — the visible region is materialized before paint, so output matches the eager golden.)

> **Manual smoke (cannot be unit-tested):** load a large markdown file in Total Commander; verify (a) instant open, (b) scrolling reveals correctly-laid-out blocks, (c) the scrollbar thumb settles as you scroll (bounded drift is expected and acceptable — Lever 2 removes it), (d) clicking an in-viewport link works, (e) Ctrl+G goto-line and anchor links land on/near the target.

- [ ] **Step 5: Commit**

```bash
git add src/plugin_md/window/host_adapter.cpp src/runtime/layout/layout_engine.h src/runtime/layout/layout_engine.cpp
git commit -m @'
feat(plugin-md): viewport-lazy layout in ListLoadW

do_layout builds an estimate-only skeleton; the host materializes the
visible block layouts before each paint and reflows by Y-translation.
First-open no longer pays full-document CreateTextLayout.
'@
```

---

## Task 8: Benchmark validation (gate)

**Files:** none (measurement only).

- [ ] **Step 1: Capture BEFORE**

```bash
git stash   # (only if uncommitted) — otherwise checkout the pre-Lever-1 commit in a worktree
```
Build and bench a ≥ 1 MB markdown file (reuse the harness from the prior perf pass, or a large generated file):
Run: `./build/Release/screenshot_tool.exe --bench <big.md> --width 1000 --height 1200 --dark`
Record the median `layout` and `hot path` over 11 runs.

- [ ] **Step 2: Capture AFTER and compare**

Restore the Lever 1 build, rebuild, bench the same file the same way.
Expected: `layout` first-open drops from the ~385 ms class to tens of ms (viewport-bounded); `parse`/`paint` unchanged. Record both in the PR description.

- [ ] **Step 3: Update perf memory**

Append the measured before/after to the project perf memory (`project_perf_baselines` / `project_perf_plan`) noting markdown now has viewport-lazy layout (mirroring the colorizer entry).

---

## Self-Review checklist (run before handing off)

- **Spec coverage:** estimate pass (T5) ✓, materialize (T5) ✓, reflow (T4/T6) ✓, host wiring (T7) ✓, anchors/line_index/scrollbar after reflow (T7) ✓, interaction live spans (no change needed — verified) ✓, layout cache lifetime (T7 note) ✓, eager path preserved (Invariant 4, T1/T2/T5 full-suite runs) ✓.
- **Type consistency:** `InlineLayoutResult`/`TextLayoutResult` alias (T1); `CodeFenceInput`/`CodeFenceLayoutResult` (T2); `BlockRecipe`/`MdMaterializeCtx`/`md_materialize`/`apply_height_delta`/`shift_block_y` (T4) used identically in T5/T6/T7; `take_md_ctx()` added in T7 and declared in `layout_engine.h`.
- **Open risk to watch during execution:** if `recipes` index alignment drifts for eager blocks in lazy mode, `md_materialize` would index the wrong recipe — the T5 Step-3 "top-up" note guards this; add an `assert(md_ctx_->recipes.size() == result_.blocks.size())` after the layout loop while developing.
