# Interaction Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land four small interaction-layer fixes — `_`/`-` in word boundaries; clickable URLs in the colorizer; trailing-dash anchor matching; clickable links inside headings — as five independent commits each shipping with passing tests + visual regression.

**Architecture:** Each commit is a vertical slice. §1 and §3 are one-function changes. §4 extends `LayoutEngine::create_text_layout` to take an optional format pointer so heading layout can route through the same path paragraphs use, picking up interactive spans for free. §2 adds a new `runtime/interaction/url_scanner.{h,cpp}` consumed by `colorizer_layout.cpp`, plus a tiny `runtime/host/link_actions.h` that lifts only the `OpenExternal` branch of the md plugin's link dispatcher so the colorizer can reuse it. The colorizer's `ColorViewState` gains an `InteractionEngine` and the existing `build_colorizer_menu_context` is extended to populate `ctx.link` from a hit-test.

**Tech Stack:** C++20, MSVC, doctest, Direct2D/DirectWrite, Conan, CMake. Visual regression via `screenshot_tool` + Playwright/Chrome via `bun run update-goldens`.

**Spec:** `docs/superpowers/specs/2026-05-09-interaction-polish-design.md` (commit `b1bc25c`).

**Key file inventory verified up-front:**
- `find_word_boundaries` lives at `src/runtime/interaction/text_selection.cpp:61`.
- `LayoutEngine::slugify` strips trailing dashes at `src/runtime/layout/layout_engine.cpp:101`.
- `LayoutEngine::layout_heading` builds its own `IDWriteTextLayout` at `src/runtime/layout/layout_engine.cpp:275-332` and never calls `create_text_layout`.
- `create_text_layout` is private (declared `src/runtime/layout/layout_engine.h:53`).
- `LayoutBlock` already has `std::vector<InteractiveSpan> spans` (`src/runtime/layout/layout_block.h:24`).
- Colorizer host's `ColorViewState` does **not** have an `interaction` field today — the spec was wrong; the plan adds it.
- `ColorizerViewLike` concept (`src/runtime/host/context_menu.h:101-107`) does **not** require `interaction` — plan extends it.
- Existing TC plugin define `WLX_TRACE_TAG` before `#include "runtime/diagnostics/wlx_trace.h"`. A header-inline `open_external_url` therefore gets each plugin's tag at the include site — that's fine.
- Markdown visual-test fixtures live in `test_data/cases/`; case 28 already exists (`28_search_counter`). New md fixture must use number ≥ 29.
- Colorizer pixel smokes live in `test_data/colorizer_smokes/`. Anything that's not `.png/.flags/_diff.txt` is treated as an input by `scripts/visual-test.sh` stage 3.
- `tests` executable is registered in `tests/CMakeLists.txt` (current line 22 closes the source list).
- `wlx-core` library source list lives in `src/runtime/CMakeLists.txt`.

**Work order:** §1 → §3 → §4 → §2-prep → §2-colorizer. Each commit is independently shippable; the build + `tests.exe` + `colorizer-tests.exe` + `scripts/visual-test.sh` must all pass before moving to the next task group.

---

## Build / verify reference

Use these commands throughout the plan; they're not repeated in every step.

- **Build:** `cmake --build --preset conan-release`
- **Markdown unit tests:** `./build/Release/tests.exe`
- **Colorizer unit tests:** `./build/Release/colorizer-tests.exe`
- **Visual regression (full):** `./scripts/visual-test.sh`
- **Update a single Chrome golden:** `bun run update-goldens -- <case_name>`
- **Update all goldens:** `bun run update-goldens`

If `cmake --build` fails because Conan deps are stale, regenerate:
```
conan install . --output-folder=build --build=missing -s build_type=Release -s compiler.cppstd=20
cmake --preset conan-default
cmake --build --preset conan-release
```

---

## Task 1: §1 — Word boundaries on `_` and `-`

**Files:**
- Modify: `src/runtime/interaction/text_selection.cpp:61-87` (the `find_word_boundaries` function)
- Modify: `tests/runtime/interaction/test_text_selection.cpp` (add new test cases at end)

- [ ] **Step 1.1: Add failing tests for `_` and `-` word boundaries**

Append to `tests/runtime/interaction/test_text_selection.cpp` after the existing `find_word_boundaries` tests (line 251):

```cpp
TEST_CASE("find_word_boundaries - underscore is part of word") {
    std::wstring text = L"foo_bar baz";
    // offset inside "foo_bar"
    auto [s1, e1] = find_word_boundaries(text, 0);
    CHECK(s1 == 0);
    CHECK(e1 == 7);
    auto [s2, e2] = find_word_boundaries(text, 4);  // on the 'b' after '_'
    CHECK(s2 == 0);
    CHECK(e2 == 7);
    auto [s3, e3] = find_word_boundaries(text, 3);  // on the '_' itself
    CHECK(s3 == 0);
    CHECK(e3 == 7);
}

TEST_CASE("find_word_boundaries - hyphen is part of word") {
    std::wstring text = L"kebab-case word";
    auto [s, e] = find_word_boundaries(text, 0);
    CHECK(s == 0);
    CHECK(e == 10);
    auto [s2, e2] = find_word_boundaries(text, 5);  // on the '-'
    CHECK(s2 == 0);
    CHECK(e2 == 10);
    auto [s3, e3] = find_word_boundaries(text, 6);  // on the 'c' after '-'
    CHECK(s3 == 0);
    CHECK(e3 == 10);
}

TEST_CASE("find_word_boundaries - leading underscore") {
    std::wstring text = L"_leading rest";
    auto [s, e] = find_word_boundaries(text, 0);
    CHECK(s == 0);
    CHECK(e == 8);
}

TEST_CASE("find_word_boundaries - trailing hyphen") {
    std::wstring text = L"trailing- rest";
    auto [s, e] = find_word_boundaries(text, 4);
    CHECK(s == 0);
    CHECK(e == 9);  // includes the trailing '-'
}

TEST_CASE("find_word_boundaries - cli-style double-dash flag") {
    std::wstring text = L"run --flag value";
    auto [s, e] = find_word_boundaries(text, 5);  // on the 'f' inside "--flag"
    CHECK(s == 4);
    CHECK(e == 10);  // "--flag"
}

TEST_CASE("find_word_boundaries - mixed underscore and hyphen") {
    std::wstring text = L"foo-bar_baz qux";
    auto [s, e] = find_word_boundaries(text, 5);  // inside the token
    CHECK(s == 0);
    CHECK(e == 11);  // "foo-bar_baz"
}

TEST_CASE("find_word_boundaries - dot is still a word break") {
    // .  is iswpunct true and NOT in our word-char allow-list,
    // so foo.txt selects just "foo" or just "txt"
    std::wstring text = L"foo.txt rest";
    auto [s, e] = find_word_boundaries(text, 0);
    CHECK(s == 0);
    CHECK(e == 3);  // "foo"
    auto [s2, e2] = find_word_boundaries(text, 4);
    CHECK(s2 == 4);
    CHECK(e2 == 7);  // "txt"
}
```

- [ ] **Step 1.2: Run the tests and confirm they fail**

```
cmake --build --preset conan-release
./build/Release/tests.exe -tc='find_word_boundaries - underscore is part of word'
```
Expected: FAIL — current `iswpunct` rejects `_` and `-`.

- [ ] **Step 1.3: Implement the fix**

Edit `src/runtime/interaction/text_selection.cpp` lines 66-68 — replace the existing `is_word_char` lambda:

```cpp
auto is_word_char = [](wchar_t c) {
    if (c == L'_' || c == L'-') return true;
    return !iswspace(c) && !iswpunct(c);
};
```

- [ ] **Step 1.4: Run all markdown tests, confirm green**

```
cmake --build --preset conan-release
./build/Release/tests.exe
```
Expected: ALL PASS — including the seven new tests and the four pre-existing `find_word_boundaries` tests.

- [ ] **Step 1.5: Run visual regression (must stay green)**

```
./scripts/visual-test.sh
```
Expected: PASS — word-boundary changes do not affect rendering.

- [ ] **Step 1.6: Commit**

```
git add src/runtime/interaction/text_selection.cpp tests/runtime/interaction/test_text_selection.cpp
git commit -m "$(cat <<'EOF'
feat(text-selection): treat _ and - as word characters on double-click

Both md and colorizer plugins use find_word_boundaries; iswpunct rejects
_ and - so identifiers like foo_bar, kebab-case, --flag, foo-bar_baz used
to fragment on double-click. Whitelist them ahead of the iswpunct guard.

Tests cover middle, leading, and trailing positions plus a regression
that foo.txt still splits at the dot (. is intentionally NOT added).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: §3 — Anchor fragment normalization

**Files:**
- Modify: `src/runtime/interaction/interaction_engine.cpp:67-73` (the `anchor_y` function)
- Test: `tests/runtime/interaction/test_text_selection.cpp` (file is the existing home for interaction-engine tests; add a new doctest section). If the test grows beyond ~10 cases consider splitting later — out of scope here.

- [ ] **Step 2.1: Add failing tests for fragment normalization**

Append to `tests/runtime/interaction/test_text_selection.cpp` after the previous task's tests (need a new factory and helper — keep them local to the new TEST_CASEs):

```cpp
#include "runtime/interaction/interaction_engine.h"

// Reuses create_dwrite_factory(), parse(), do_layout() defined earlier in this file.

TEST_CASE("InteractionEngine::anchor_y - exact match") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("# General Code Formatting\n\nbody");
    auto layout = do_layout(factory.Get(), doc);
    InteractionEngine eng(layout);
    auto y = eng.anchor_y(L"general-code-formatting");
    CHECK(y.has_value());
}

TEST_CASE("InteractionEngine::anchor_y - trailing dash on fragment is normalized") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("# General Code Formatting\n\nbody");
    auto layout = do_layout(factory.Get(), doc);
    InteractionEngine eng(layout);
    // GitHub-style TOC links carry the trailing dash for headings ending
    // in whitespace + non-alnum; ours strip it from stored slugs but
    // anchor_y must still resolve it.
    auto y = eng.anchor_y(L"general-code-formatting-");
    CHECK(y.has_value());
}

TEST_CASE("InteractionEngine::anchor_y - mixed-case fragment is normalized") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("# Style Guide\n\nbody");
    auto layout = do_layout(factory.Get(), doc);
    InteractionEngine eng(layout);
    auto y1 = eng.anchor_y(L"Style-Guide");
    auto y2 = eng.anchor_y(L"STYLE-GUIDE");
    auto y3 = eng.anchor_y(L"style-guide");
    CHECK(y1.has_value());
    CHECK(y2.has_value());
    CHECK(y3.has_value());
}

TEST_CASE("InteractionEngine::anchor_y - leading dash on fragment is normalized") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("# Intro\n\nbody");
    auto layout = do_layout(factory.Get(), doc);
    InteractionEngine eng(layout);
    auto y = eng.anchor_y(L"-intro");
    CHECK(y.has_value());
}

TEST_CASE("InteractionEngine::anchor_y - unknown fragment returns nullopt") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("# Intro\n\nbody");
    auto layout = do_layout(factory.Get(), doc);
    InteractionEngine eng(layout);
    auto y = eng.anchor_y(L"does-not-exist");
    CHECK_FALSE(y.has_value());
}
```

- [ ] **Step 2.2: Run tests, confirm new ones fail**

```
cmake --build --preset conan-release
./build/Release/tests.exe -tc='InteractionEngine::anchor_y - trailing dash*'
```
Expected: FAIL on the trailing-dash, mixed-case, and leading-dash cases. The exact-match and unknown-fragment cases should pass already.

- [ ] **Step 2.3: Implement `normalize_fragment` and use it in `anchor_y`**

Replace the body of `src/runtime/interaction/interaction_engine.cpp:67-73` so the file ends like this (only the bottom changes; keep `#include`s and the rest of the file untouched):

```cpp
static std::wstring normalize_fragment(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    while (!s.empty() && s.front() == L'-') s.erase(s.begin());
    while (!s.empty() && s.back()  == L'-') s.pop_back();
    return s;
}

std::optional<float> InteractionEngine::anchor_y(const std::wstring& slug) const {
    std::wstring needle = normalize_fragment(slug);
    for (auto& anchor : layout_.anchors) {
        if (anchor.slug == needle)
            return anchor.y_offset;
    }
    return std::nullopt;
}
```

Add `#include <cwctype>` to the top of the file if not already present (needed for `towlower`).

- [ ] **Step 2.4: Run all markdown tests**

```
./build/Release/tests.exe
```
Expected: ALL PASS.

- [ ] **Step 2.5: Run visual regression**

```
./scripts/visual-test.sh
```
Expected: PASS — fragment matching is runtime click logic and does not affect rendering.

- [ ] **Step 2.6: Commit**

```
git add src/runtime/interaction/interaction_engine.cpp tests/runtime/interaction/test_text_selection.cpp
git commit -m "$(cat <<'EOF'
fix(interaction): normalize anchor fragments to match GitHub-style slugs

slugify lowercases and strips leading/trailing dashes when storing the
anchor; the lookup side just compared the raw URL fragment, so TOC links
written GitHub-style (e.g. #general-code-formatting- with trailing dash
because the heading ends with whitespace + a back-reference link) never
resolved. anchor_y now runs the same lowercase + dash-strip on the
incoming fragment before comparison; URL-decoding is intentionally
deferred (see spec Non-Goals).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: §4 — Heading link spans (extend `create_text_layout`)

**Files:**
- Modify: `src/runtime/layout/layout_engine.h:53-54` (extend `create_text_layout` signature)
- Modify: `src/runtime/layout/layout_engine.cpp:106-217` (use the new format param)
- Modify: `src/runtime/layout/layout_engine.cpp:275-332` (rewrite `layout_heading`)
- Test: `tests/runtime/layout/test_layout_engine.cpp` (add heading-link-span assertion)
- Add: `test_data/cases/29_heading_with_inline_link.md`
- Add: `test_data/cases/29_heading_with_inline_link_chrome.png` (Chrome golden, generated by `bun run update-goldens`)

- [ ] **Step 3.1: Add failing test for spans inside a heading**

Open `tests/runtime/layout/test_layout_engine.cpp` and append (use the same pattern existing tests use — `MarkdownParser`, `LayoutEngine`, `IDWriteFactory`):

```cpp
TEST_CASE("layout_heading - inline link produces an interactive span") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    MarkdownParser p;
    const char* md = "## Style Guide [^](#table-of-contents)\n";
    auto doc = p.parse(md, std::strlen(md));
    ThemeService theme;
    LayoutEngine eng(factory.Get(), theme, false);
    auto layout = eng.layout(doc, 800.0f);

    int heading_idx = -1;
    for (int i = 0; i < (int)layout.blocks.size(); i++) {
        if (layout.blocks[i].type == BlockType::Heading) {
            heading_idx = i;
            break;
        }
    }
    REQUIRE(heading_idx >= 0);

    auto& blk = layout.blocks[heading_idx];
    REQUIRE(blk.spans.size() == 1);
    auto& span = blk.spans[0];
    CHECK(span.target.kind == LinkKind::InternalAnchor);
    CHECK(span.target.anchor_fragment == L"table-of-contents");
    CHECK(span.rect.right > span.rect.left);
    CHECK(span.rect.bottom > span.rect.top);
}
```

If `test_layout_engine.cpp` doesn't already have the helpers `create_dwrite_factory()`/`MarkdownParser`/etc. in scope, look at the existing tests in that file and follow their pattern (most likely: they `#include` the same headers as `test_text_selection.cpp` does at lines 1-11). Reuse the same factory helper if defined locally; otherwise define one inline before the new TEST_CASE.

- [ ] **Step 3.2: Run the new test, confirm it fails**

```
cmake --build --preset conan-release
./build/Release/tests.exe -tc='layout_heading - inline link*'
```
Expected: FAIL with `blk.spans.size() == 0`.

- [ ] **Step 3.3: Extend `create_text_layout` signature**

Edit `src/runtime/layout/layout_engine.h:53-54`. Replace the declaration with:

```cpp
TextLayoutResult create_text_layout(const std::vector<parser::InlineNode>& inlines,
                                    float max_width, uint32_t default_color,
                                    IDWriteTextFormat* format = nullptr);
```

Then in `src/runtime/layout/layout_engine.cpp`, change the definition signature (around line 108) to match:

```cpp
LayoutEngine::TextLayoutResult LayoutEngine::create_text_layout(
    const std::vector<InlineNode>& inlines, float max_width, uint32_t default_color,
    IDWriteTextFormat* format) {
```

And inside the body, change the `CreateTextLayout` call (around line 148-151) to use the supplied format when non-null:

```cpp
    IDWriteTextFormat* fmt = format ? format : body_format_.Get();
    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = dwrite_->CreateTextLayout(
        full_text.c_str(), static_cast<UINT32>(full_text.size()),
        fmt, max_width, 100000.0f,
        layout.GetAddressOf());
```

That's all that changes inside `create_text_layout` — paragraph callers still pass three args and behave identically.

- [ ] **Step 3.4: Rewrite `layout_heading` to route through `create_text_layout`**

Replace the entire `LayoutEngine::layout_heading` function (`src/runtime/layout/layout_engine.cpp:275-332`) with:

```cpp
void LayoutEngine::layout_heading(const BlockNode& node, float& y, float left, float right) {
    y += spacing_.heading_spacing_above;

    int level = std::clamp(node.heading_level, 1, 6);
    float font_size = kHeadingSizes[level - 1];

    auto fmt = get_body_format(font_size);
    if (!fmt) return;

    float max_width = right - left;
    auto tlr = create_text_layout(node.inlines, max_width, colors_.heading, fmt.Get());
    if (!tlr.layout) return;

    // Bold the entire heading.
    DWRITE_TEXT_RANGE all = {0, static_cast<UINT32>(tlr.full_text.size())};
    tlr.layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, all);

    LayoutBlock lb;
    lb.type = BlockType::Heading;
    lb.heading_level = level;
    lb.rect = D2D1::RectF(left, y, right, y + tlr.height);

    TextRun run;
    run.text = tlr.full_text;
    run.rect = lb.rect;
    run.layout = tlr.layout;
    run.color = colors_.heading;
    run.color_ranges = std::move(tlr.color_ranges);
    run.code_bg_rects = std::move(tlr.code_bg_rects);
    lb.text_runs.push_back(std::move(run));

    // Offset interactive span rects to document coordinates and append.
    for (auto& s : tlr.spans) {
        s.rect.left  += left;
        s.rect.right += left;
        s.rect.top   += y;
        s.rect.bottom += y;
        lb.spans.push_back(std::move(s));
    }

    // H1/H2 bottom border
    if (level <= 2) {
        lb.has_bottom_rule = true;
        lb.bottom_rule_color = colors_.rule;
    }

    // Anchor
    std::wstring slug = slugify(node.inlines);
    if (!slug.empty()) {
        result_.anchors.push_back({slug, y});
    }

    y += tlr.height + spacing_.heading_spacing_below;
    result_.blocks.push_back(std::move(lb));
}
```

Key invariants preserved from the old code:
- `y` advance pattern (`heading_spacing_above` before, `heading_spacing_below` after).
- H1/H2 bottom rule with `colors_.rule`.
- Anchor recorded via `slugify(node.inlines)` at the heading's top-y.
- Heading bolded uniformly via `SetFontWeight` over the full range.

The change: text construction now goes through `create_text_layout`, which (a) builds inline-aware layout (per-range italic/strikethrough/code, links underlined and link-colored already), (b) extracts `tlr.spans` for any inline links, (c) carries `color_ranges` / `code_bg_rects` through. The font *size* flows in via the `fmt` argument so `tlr.height` and `tlr.spans` rects are measured at heading size — no post-hoc re-measurement needed.

- [ ] **Step 3.5: Run all markdown tests**

```
cmake --build --preset conan-release
./build/Release/tests.exe
```
Expected: ALL PASS — including the new heading-span test from Step 3.1 and all existing tests (paragraphs unaffected because the new `format` param defaults to `nullptr`, which preserves the prior behaviour).

- [ ] **Step 3.6: Run full visual regression — investigate any regressions**

```
./scripts/visual-test.sh
```
Expected: Stage 1 (markdown) PASS for the existing 27 cases. The new path *may* improve heading similarity to Chrome — the old `layout_heading` ignored per-inline italic/strikethrough/code while the paragraph path honours them; new code now does too, which is closer to Chrome's behaviour.

If a heading-bearing case (`01_headings_atx`, `02_headings_setext`, `19_headings_with_emphasis` if present, etc.) **drops** below 95% similarity, do **not** blindly regenerate goldens. The goldens are Chrome-rendered references; regenerating them re-runs Chrome and won't fix a regression in our output. Instead:

1. Open `test_data/cases/<name>_diff.png` and our `<name>.png` next to `<name>_chrome.png`.
2. If our heading text suddenly italic/code-formats wrongly, the per-inline overrides flowing through `create_text_layout` are misapplied — review the inline list for that case.
3. If vertical positioning shifted, double-check Step 3.4 — the `lb.rect` is built from `tlr.height`, the same source paragraphs use; metric drift between the old direct-build and `create_text_layout` should be zero for an identical inline list.
4. Only regenerate a Chrome golden if the previously-saved one was itself stale (e.g. older Chrome version) — and that's a separate concern from this task.

- [ ] **Step 3.7: Add the new visual fixture**

Create `test_data/cases/29_heading_with_inline_link.md`:

```markdown
# Table of Contents

- [Style Guide](#style-guide)

## Style Guide [^](#table-of-contents)

This section explains the conventions. The `[^]` link in the heading
above should jump back to the table of contents.

### Sub-section [^](#table-of-contents)

Inline link in an `h3` should also be clickable.
```

- [ ] **Step 3.8: Generate the Chrome golden for the new case**

```
bun run update-goldens -- 29_heading_with_inline_link
```
Expected: produces `test_data/cases/29_heading_with_inline_link_chrome.png` plus an `_chrome` diff baseline.

If `update-goldens` fails because Playwright is missing, the user must run `bun install` then `bunx playwright install chromium` first (these are prerequisites of the visual harness, not in this plan's scope).

- [ ] **Step 3.9: Run visual regression and confirm the new case is at ≥95% similarity**

```
./scripts/visual-test.sh
```
Expected: Stage 1 PASS, including `29_heading_with_inline_link`.

- [ ] **Step 3.10: Commit**

```
git add src/runtime/layout/layout_engine.h \
        src/runtime/layout/layout_engine.cpp \
        tests/runtime/layout/test_layout_engine.cpp \
        test_data/cases/29_heading_with_inline_link.md \
        test_data/cases/29_heading_with_inline_link_chrome.png
# If you regenerated 01/02 goldens in Step 3.6, add those PNGs as well.
git commit -m "$(cat <<'EOF'
feat(layout): heading text now emits interactive spans for inline links

layout_heading used to build its own minimal IDWriteTextLayout and
never extracted block.spans, so [^](#anchor) inside ## Heading text
was visually rendered as a link but ignored by InteractionEngine's
hit_test. Route heading layout through create_text_layout (extended
with an optional format pointer so heading font size flows in before
measurement); spans, color_ranges, and code_bg_rects are now carried
through identically to the paragraph path. Bold + bottom-rule layer
on top after, as before.

New visual fixture 29_heading_with_inline_link covers the regression.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: §2-prep — `url_scanner` module + `link_actions.h` + md plugin refactor

This is the foundation for Task 5 (the colorizer wiring). It introduces no user-visible change yet — the URL scanner is unused, and the md plugin's link-open behaviour is identical, just routed through a new helper.

**Files:**
- Add: `src/runtime/interaction/url_scanner.h`
- Add: `src/runtime/interaction/url_scanner.cpp`
- Add: `tests/runtime/interaction/test_url_scanner.cpp`
- Add: `src/runtime/host/link_actions.h`
- Modify: `src/runtime/CMakeLists.txt` (register `url_scanner.cpp`)
- Modify: `tests/CMakeLists.txt` (register `test_url_scanner.cpp` under the `tests` target)
- Modify: `src/plugin_md/window/host_adapter.cpp:268-294` (route OpenExternal through `open_external_url`)

- [ ] **Step 4.1: Create `url_scanner.h`**

Write `src/runtime/interaction/url_scanner.h`:

```cpp
#pragma once

#include <string_view>
#include <vector>

namespace wlx::runtime::interaction {

// One URL match within a source buffer.
// Offsets are wchar (UTF-16) code units, NOT bytes.
struct UrlMatch {
    int start = 0;  // inclusive
    int end   = 0;  // exclusive
};

// Hand-rolled URL scanner — recognizes http://, https://, ftp://, file://
// schemes (case-insensitive). Refuses to start a match inside an alphanumeric
// run (so `parsehttp://` does NOT match). Trims a trailing run of
// .,;:!?)]}> from each match (so `See https://x/y.` doesn't capture the
// trailing period and `(see https://x/y)` doesn't capture the closing paren).
//
// std::wregex is intentionally avoided: lookbehind support varies, and a
// linear scan is faster and more predictable for this shape.
std::vector<UrlMatch> scan_urls(std::wstring_view text);

}  // namespace wlx::runtime::interaction
```

- [ ] **Step 4.2: Create `url_scanner.cpp`**

Write `src/runtime/interaction/url_scanner.cpp`:

```cpp
#include "runtime/interaction/url_scanner.h"

#include <cwctype>
#include <string_view>

namespace wlx::runtime::interaction {

namespace {

constexpr std::wstring_view kSchemes[] = {
    L"https://",
    L"http://",
    L"file://",
    L"ftp://",
};

bool ascii_iequals(wchar_t a, wchar_t b) {
    if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a - L'A' + L'a');
    if (b >= L'A' && b <= L'Z') b = static_cast<wchar_t>(b - L'A' + L'a');
    return a == b;
}

// Returns the matched scheme length (including '://') if `text` at `i` begins
// with a known scheme, else 0.
int match_scheme(std::wstring_view text, int i) {
    for (auto& scheme : kSchemes) {
        if (i + static_cast<int>(scheme.size()) > static_cast<int>(text.size()))
            continue;
        bool ok = true;
        for (int k = 0; k < static_cast<int>(scheme.size()); ++k) {
            if (!ascii_iequals(text[i + k], scheme[k])) { ok = false; break; }
        }
        if (ok) return static_cast<int>(scheme.size());
    }
    return 0;
}

// True if the char ends a URL body — whitespace or one of the brackets/quotes
// that conventionally bound a URL literal.
bool is_url_terminator(wchar_t c) {
    if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') return true;
    switch (c) {
        case L'<': case L'>': case L'"': case L'`':
        case L'{': case L'}': case L'|': case L'\\':
        case L'^':
            return true;
        default:
            return false;
    }
}

bool is_trailing_punct(wchar_t c) {
    switch (c) {
        case L'.': case L',': case L';': case L':':
        case L'!': case L'?':
        case L')': case L']': case L'}': case L'>':
            return true;
        default:
            return false;
    }
}

}  // namespace

std::vector<UrlMatch> scan_urls(std::wstring_view text) {
    std::vector<UrlMatch> out;
    int n = static_cast<int>(text.size());
    int i = 0;

    while (i < n) {
        // Cheap pre-filter: only scheme prefixes start with h or f.
        wchar_t c0 = text[i];
        if (c0 != L'h' && c0 != L'H' && c0 != L'f' && c0 != L'F') {
            ++i;
            continue;
        }

        // Boundary check: previous char must not be alphanumeric.
        if (i > 0) {
            wchar_t prev = text[i - 1];
            if (iswalnum(prev)) { ++i; continue; }
        }

        int scheme_len = match_scheme(text, i);
        if (scheme_len == 0) { ++i; continue; }

        // Body: non-terminator characters.
        int j = i + scheme_len;
        while (j < n && !is_url_terminator(text[j])) ++j;

        // Refuse degenerate matches (just the scheme with no body).
        if (j == i + scheme_len) { i = j; continue; }

        // Trim trailing punctuation.
        int end = j;
        while (end > i + scheme_len && is_trailing_punct(text[end - 1]))
            --end;

        // Refuse if trim erased the body.
        if (end == i + scheme_len) { i = j; continue; }

        out.push_back({i, end});
        i = j;  // continue past whatever raw body we consumed (incl. trimmed punct)
    }

    return out;
}

}  // namespace wlx::runtime::interaction
```

- [ ] **Step 4.3: Register `url_scanner.cpp` in the runtime library**

Edit `src/runtime/CMakeLists.txt` and insert one line in the `add_library(wlx-core STATIC ...)` source list, alphabetically ordered next to the other `interaction/` entry (right after `interaction/interaction_engine.cpp`):

```
    interaction/text_selection.cpp
    interaction/url_scanner.cpp
```

So the resulting block reads:
```cmake
add_library(wlx-core STATIC
    io/file_service.cpp
    parser/markdown_parser.cpp
    layout/layout_engine.cpp
    render/render_engine.cpp
    interaction/interaction_engine.cpp
    theme/theme_service.cpp
    cache/cache_service.cpp
    interaction/text_selection.cpp
    interaction/url_scanner.cpp
    search/search_index.cpp
    ...
```

- [ ] **Step 4.4: Build to verify the new TU compiles**

```
cmake --build --preset conan-release
```
Expected: clean build with `url_scanner.cpp` linked into `wlx-core.lib`.

- [ ] **Step 4.5: Add unit tests for `url_scanner`**

Write `tests/runtime/interaction/test_url_scanner.cpp`:

```cpp
#include <doctest/doctest.h>
#include "runtime/interaction/url_scanner.h"

using namespace wlx::runtime::interaction;

TEST_CASE("scan_urls - empty input") {
    auto matches = scan_urls(L"");
    CHECK(matches.empty());
}

TEST_CASE("scan_urls - bare https URL") {
    std::wstring text = L"https://example.com/path";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(m[0].start == 0);
    CHECK(m[0].end == static_cast<int>(text.size()));
}

TEST_CASE("scan_urls - URL inside surrounding text") {
    std::wstring text = L"See https://example.com/page for details.";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(m[0].start == 4);
    CHECK(m[0].end == 31);  // up to '/page' end, before space
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"https://example.com/page");
}

TEST_CASE("scan_urls - trailing period is trimmed") {
    std::wstring text = L"go to https://example.com/page.";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"https://example.com/page");
}

TEST_CASE("scan_urls - trailing close-paren is trimmed") {
    std::wstring text = L"(see https://example.com/page)";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"https://example.com/page");
}

TEST_CASE("scan_urls - trailing close-bracket and exclamation are trimmed") {
    std::wstring text = L"[https://example.com/!]";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"https://example.com/");
}

TEST_CASE("scan_urls - URL with port and query string") {
    std::wstring text = L"  https://api.example.com:8080/v1/items?id=42&limit=10  ";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) ==
          L"https://api.example.com:8080/v1/items?id=42&limit=10");
}

TEST_CASE("scan_urls - URL inside an identifier is rejected") {
    std::wstring text = L"parsehttp://x.org/y";
    auto m = scan_urls(text);
    CHECK(m.empty());
}

TEST_CASE("scan_urls - http and https side by side") {
    std::wstring text = L"a http://x.org/ b https://y.org/ c";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 2);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"http://x.org/");
    CHECK(text.substr(m[1].start, m[1].end - m[1].start) == L"https://y.org/");
}

TEST_CASE("scan_urls - file:// scheme") {
    std::wstring text = L"local: file:///C:/tmp/x.txt yes";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"file:///C:/tmp/x.txt");
}

TEST_CASE("scan_urls - ftp:// scheme") {
    std::wstring text = L"ftp://files.example.com/archive.zip";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(m[0].start == 0);
    CHECK(m[0].end == static_cast<int>(text.size()));
}

TEST_CASE("scan_urls - URL inside a JSON string literal") {
    std::wstring text = LR"(    "url": "https://files.pythonhosted.org/pkg/x.whl",)";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) ==
          L"https://files.pythonhosted.org/pkg/x.whl");
}

TEST_CASE("scan_urls - URL inside a C-style comment") {
    std::wstring text = L"// see https://docs.example.com for usage";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) ==
          L"https://docs.example.com");
}

TEST_CASE("scan_urls - case-insensitive scheme") {
    std::wstring text = L"HTTPS://EXAMPLE.COM/PATH";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(m[0].start == 0);
    CHECK(m[0].end == static_cast<int>(text.size()));
}

TEST_CASE("scan_urls - scheme alone is not a match") {
    std::wstring text = L"https:// alone";
    auto m = scan_urls(text);
    CHECK(m.empty());
}

TEST_CASE("scan_urls - line with no URL produces nothing") {
    std::wstring text = L"this line has no url at all";
    auto m = scan_urls(text);
    CHECK(m.empty());
}
```

- [ ] **Step 4.6: Register the test file in `tests/CMakeLists.txt`**

Edit `tests/CMakeLists.txt`. Inside the `add_executable(tests ...)` source list, add the new file alphabetically near the other interaction test:

```cmake
    runtime/interaction/test_text_selection.cpp
    runtime/interaction/test_url_scanner.cpp
```

- [ ] **Step 4.7: Build and run the new tests**

```
cmake --build --preset conan-release
./build/Release/tests.exe -tc='scan_urls*'
```
Expected: ALL 16 new test cases PASS.

- [ ] **Step 4.8: Create `link_actions.h`**

Write `src/runtime/host/link_actions.h`:

```cpp
#pragma once

#include "runtime/diagnostics/wlx_trace.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace wlx::runtime::host {

// Open `url` in the system default handler via ShellExecuteW.
// Logs a WLX_TRACE diagnostic on failure (using whatever WLX_TRACE_TAG
// the including translation unit defined). Header-inline so the trace
// macro picks up the per-plugin tag at the include site.
inline void open_external_url(const std::wstring& url) {
    HINSTANCE hi = ShellExecuteW(nullptr, L"open", url.c_str(),
                                 nullptr, nullptr, SW_SHOW);
    if (reinterpret_cast<INT_PTR>(hi) <= 32) {
        WLX_TRACE(L"open_external_url: ShellExecuteW failed (code %lld) for %s",
                  static_cast<long long>(reinterpret_cast<INT_PTR>(hi)),
                  url.c_str());
    }
}

}  // namespace wlx::runtime::host
```

- [ ] **Step 4.9: Switch the markdown plugin's `OpenExternal` branch to call `open_external_url`**

Edit `src/plugin_md/window/host_adapter.cpp`. Add the include after the existing diagnostics include (somewhere near `#include "runtime/diagnostics/wlx_trace.h"`):

```cpp
#include "runtime/host/link_actions.h"
```

Replace lines 274-283 (the `OpenExternal` case body) with:

```cpp
    case InteractionEngine::Action::OpenExternal:
        wlx::runtime::host::open_external_url(action.target);
        break;
```

The `ScrollToAnchor` and `ReloadDocument` cases keep their existing inline bodies — they touch md-specific `ViewState` members and stay where they are.

- [ ] **Step 4.10: Build, run all md tests, run visual regression**

```
cmake --build --preset conan-release
./build/Release/tests.exe
./scripts/visual-test.sh
```
Expected: ALL PASS. md plugin behaviour is identical (same ShellExecuteW, same trace tag — the macro expands at the call site so the tag is still `wlx-md`).

- [ ] **Step 4.11: Commit**

```
git add src/runtime/interaction/url_scanner.h \
        src/runtime/interaction/url_scanner.cpp \
        src/runtime/host/link_actions.h \
        src/runtime/CMakeLists.txt \
        tests/runtime/interaction/test_url_scanner.cpp \
        tests/CMakeLists.txt \
        src/plugin_md/window/host_adapter.cpp
git commit -m "$(cat <<'EOF'
feat(runtime): url_scanner + link_actions helper, md plugin uses helper

Adds a hand-rolled URL scanner (http/https/ftp/file, case-insensitive,
trims trailing .,;:!?)]}>, refuses inside-identifier matches via the
preceding-char alnum guard) plus a small inline open_external_url
helper that wraps ShellExecuteW + WLX_TRACE on failure. The md plugin
swaps its OpenExternal branch to call the helper — no user-visible
change yet; this is the foundation for the colorizer URL feature.

16 doctest cases cover the scanner: bare URLs, surrounding text,
trailing-punct trim variants, port/query strings, identifier guard,
multiple URLs per line, all four schemes, case-insensitive scheme,
scheme-only, JSON-string and C-comment containment.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: §2 — Colorizer URL detection, click routing, menu integration

**Files:**
- Modify: `src/plugin_colorizer/layout/colorizer_layout.cpp` (per-line URL scan, populate `lb.spans`, override color/underline)
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp` (add `interaction` to `ColorViewState`, build it after layout, hover-cursor change, click handling, OpenLink/CopyLinkAddress menu cases)
- Modify: `src/runtime/host/context_menu.h` (extend `ColorizerViewLike` concept; teach `build_colorizer_menu_context` to populate `ctx.link` from a hit-test)
- Add: `test_data/colorizer_smokes/smoke_url_in_toml.toml`
- Add: `test_data/colorizer_smokes/smoke_url_in_toml.toml_golden.png` (golden, generated by running screenshot_tool then copy)

- [ ] **Step 5.1: Add the `interaction` field to `ColorViewState`**

Edit `src/plugin_colorizer/window/colorizer_host_adapter.cpp` around the `ColorViewState` definition (currently around line 103-150).

Add this include near the existing `#include "runtime/interaction/text_selection.h"` (line 26):

```cpp
#include "runtime/interaction/interaction_engine.h"
#include "runtime/interaction/url_scanner.h"
#include "runtime/host/link_actions.h"
```

Inside `struct ColorViewState`, after `std::unique_ptr<SearchHud> hud;` (around line 112), add:

```cpp
    std::unique_ptr<wlx::runtime::interaction::InteractionEngine> interaction;
```

- [ ] **Step 5.2: Construct `interaction` after layout**

Find the colorizer layout-completion site in `colorizer_host_adapter.cpp` — it's wherever `vs->layout = std::make_shared<LayoutDocument>(...)` happens after a `layout_source` call (search for `layout_source(` and `vs->layout =`). At every site that assigns `vs->layout`, immediately after the assignment add:

```cpp
    vs->interaction = std::make_unique<InteractionEngine>(*vs->layout);
```

(`InteractionEngine` is already brought in via `using namespace wlx::runtime::interaction;` at the top of the file — line 63 — so the unqualified name compiles.)

If there are multiple layout-completion sites (e.g. initial load and re-layout on resize/recolorize), make sure every one rebuilds `interaction`. Search-and-verify:

```
grep -n "vs->layout = std::make_shared\|vs->layout = layout_source\|->layout = " src/plugin_colorizer/window/colorizer_host_adapter.cpp
```

- [ ] **Step 5.3: Wire URL detection into colorizer layout**

Edit `src/plugin_colorizer/layout/colorizer_layout.cpp`. Add includes at the top (after the existing block_node include, line 7):

```cpp
#include "runtime/interaction/url_scanner.h"
#include "runtime/layout/interactive_span.h"
#include "wlx_core/text_modifier.h"
```

Inside the per-line loop (around line 270, after `text_layout` is created and `block_height` is computed but before the `LayoutBlock lb;` constructor call), insert URL detection. The right insertion point is just after the `if (text_layout) { ... }` block at line 313:

```cpp
        // ---- URL detection ----
        // Scan the expanded line text for URLs; emit one InteractiveSpan
        // per pixel-row covered, plus a ColorRange that overrides
        // foreground to palette.link and adds MOD_UNDERLINE.
        std::vector<wlx::runtime::layout::InteractiveSpan> url_spans;
        if (text_layout && !expanded.empty() &&
            expanded.find(L':') != std::wstring::npos) {  // cheap pre-filter

            auto url_matches = wlx::runtime::interaction::scan_urls(expanded);
            for (const auto& m : url_matches) {
                int len = m.end - m.start;
                if (len <= 0) continue;

                // Style override: link color + underline.
                wlx::runtime::layout::ColorRange link_cr;
                link_cr.start     = static_cast<uint32_t>(m.start);
                link_cr.length    = static_cast<uint32_t>(len);
                link_cr.color     = palette.link;
                link_cr.has_bg    = false;
                link_cr.modifiers = MOD_UNDERLINE;
                color_ranges.push_back(link_cr);

                // Interactive rect(s).
                UINT32 hit_count = 0;
                text_layout->HitTestTextRange(
                    static_cast<UINT32>(m.start), static_cast<UINT32>(len),
                    0, 0, nullptr, 0, &hit_count);
                if (hit_count > 0) {
                    std::vector<DWRITE_HIT_TEST_METRICS> hits(hit_count);
                    text_layout->HitTestTextRange(
                        static_cast<UINT32>(m.start), static_cast<UINT32>(len),
                        0, 0, hits.data(), hit_count, &hit_count);

                    std::wstring url(expanded.data() + m.start, static_cast<size_t>(len));
                    for (auto& h : hits) {
                        wlx::runtime::layout::InteractiveSpan span;
                        span.target.kind = wlx::runtime::parser::LinkKind::ExternalUrl;
                        span.target.url  = url;
                        // Local rects (will be offset to document coords below).
                        span.rect = D2D1::RectF(h.left, h.top,
                                                h.left + h.width, h.top + h.height);
                        url_spans.push_back(std::move(span));
                    }
                }
            }
        }
```

The `color_ranges` vector is the local variable already constructed earlier in the loop (it's `std::move`d into `run.color_ranges` later). Pushing the URL `ColorRange` onto it before the move is critical: the render engine paints `color_ranges` in order, so URL color/underline lands on top of any earlier syntax color, which is the desired visual.

After the existing `lb.text_runs.push_back(std::move(run));` line (around line 328), translate the URL spans into document coordinates and append to `lb.spans`:

```cpp
        // Offset URL span rects to document coordinates and append.
        for (auto& s : url_spans) {
            s.rect.left   += code_left;
            s.rect.right  += code_left;
            s.rect.top    += y;
            s.rect.bottom += y;
            lb.spans.push_back(std::move(s));
        }
```

- [ ] **Step 5.4: Hover cursor — hand on URLs**

Edit `src/plugin_colorizer/window/colorizer_host_adapter.cpp`. Find the `WM_MOUSEMOVE` case (search for `case WM_MOUSEMOVE`). Just before the existing text-selection cursor logic, add:

```cpp
        if (vs->layout && vs->interaction) {
            float doc_x = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                       : static_cast<float>(GET_X_LPARAM(lp));
            float doc_y_local = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                             : static_cast<float>(GET_Y_LPARAM(lp));
            float doc_y_doc = doc_y_local + vs->scroll_y;
            auto hit = vs->interaction->hit_test(doc_x, doc_y_doc);
            if (hit.hit) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return 0;
            }
        }
```

If the existing `WM_MOUSEMOVE` already computes `doc_x` and a doc-y, reuse those local names instead of re-deriving — match the surrounding pattern. The point is the early-return on hit so the rest of `WM_MOUSEMOVE` (selection-drag, beam-cursor) doesn't run.

- [ ] **Step 5.5: Click → open URL**

In the same file, find the `WM_LBUTTONDOWN` case (`case WM_LBUTTONDOWN`). At the start of the handler, before the existing text-selection drag-start, add:

```cpp
        if (vs->layout && vs->interaction) {
            float doc_x = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                       : static_cast<float>(GET_X_LPARAM(lp));
            float doc_y_local = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                             : static_cast<float>(GET_Y_LPARAM(lp));
            float doc_y_doc = doc_y_local + vs->scroll_y;
            auto hit = vs->interaction->hit_test(doc_x, doc_y_doc);
            if (hit.hit && hit.target.kind == wlx::runtime::parser::LinkKind::ExternalUrl) {
                wlx::runtime::host::open_external_url(hit.target.url);
                return 0;
            }
        }
```

Reuse local names from the surrounding code if they already exist; this is the pattern.

- [ ] **Step 5.6: Extend the `ColorizerViewLike` concept**

Edit `src/runtime/host/context_menu.h` lines 101-107. Add an `interaction` requirement so the templated `build_colorizer_menu_context` can hit-test:

```cpp
template <typename V>
concept ColorizerViewLike = requires(V& v) {
    { v.sel_anchor } -> std::same_as<wlx::runtime::layout::TextPosition&>;
    { v.sel_active } -> std::same_as<wlx::runtime::layout::TextPosition&>;
    v.layout;
    v.interaction;
    { v.force_grammar_id } -> std::same_as<std::string&>;
};
```

- [ ] **Step 5.7: Teach `build_colorizer_menu_context` to populate `ctx.link`**

In the same file (`src/runtime/host/context_menu.h`), update the templated function (currently lines 154-163) to accept doc coordinates and run a hit-test, mirroring the markdown plugin's pattern at lines 110-152:

```cpp
template <ColorizerViewLike V>
MenuContext build_colorizer_menu_context(V& vs, std::vector<LanguageOption> langs,
                                         float doc_x, float doc_y) {
    MenuContext ctx;
    ctx.has_selection = vs.sel_anchor.valid()
                     && vs.sel_anchor != vs.sel_active;
    ctx.languages = std::move(langs);
    ctx.active_grammar_id  = vs.force_grammar_id;
    ctx.auto_detect_active = vs.force_grammar_id.empty();

    if (vs.layout && vs.interaction) {
        auto hit = vs.interaction->hit_test(doc_x, doc_y);
        if (hit.hit && hit.target.kind == wlx::runtime::parser::LinkKind::ExternalUrl) {
            ctx.link.present  = true;
            ctx.link.url      = hit.target.url;
            ctx.link.external = true;
        }
    }
    return ctx;
}
```

- [ ] **Step 5.8: Update the colorizer's `WM_CONTEXTMENU` call site**

Edit `src/plugin_colorizer/window/colorizer_host_adapter.cpp` around line 677. The current call:

```cpp
        auto ctx = build_colorizer_menu_context(*vs, std::move(langs));
```

becomes:

```cpp
        // Convert the screen-space WM_CONTEXTMENU coords into doc coords.
        POINT client_pt = screen_pt;
        ScreenToClient(hwnd, &client_pt);
        float doc_x = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(client_pt.x))
                                   : static_cast<float>(client_pt.x);
        float doc_y_local = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(client_pt.y))
                                         : static_cast<float>(client_pt.y);
        float doc_y_doc = doc_y_local + vs->scroll_y;
        auto ctx = build_colorizer_menu_context(*vs, std::move(langs), doc_x, doc_y_doc);
```

- [ ] **Step 5.9: Wire `OpenLink` and `CopyLinkAddress` menu cases**

Same file, around lines 719-724. Replace the explicit no-op block:

```cpp
        case MenuResult::OpenLink:
        case MenuResult::CopyLinkAddress:
        case MenuResult::CopyCodeBlock:
        case MenuResult::None:
        default:
            break;  // colorizer has no links or code-block boundaries
```

with:

```cpp
        case MenuResult::OpenLink:
            if (ctx.link.present)
                wlx::runtime::host::open_external_url(ctx.link.url);
            break;

        case MenuResult::CopyLinkAddress:
            if (ctx.link.present) {
                // Reuse the existing clipboard helper. Signature:
                //   bool copy_to_clipboard(HWND owner, const std::wstring& text)
                // declared in runtime/host/clipboard.h.
                wlx::runtime::host::copy_to_clipboard(hwnd, ctx.link.url);
            }
            break;

        case MenuResult::CopyCodeBlock:
        case MenuResult::None:
        default:
            break;  // colorizer has no code-block boundaries
```

If `runtime/host/clipboard.h` isn't already included from this TU, add `#include "runtime/host/clipboard.h"` near the other host includes.

- [ ] **Step 5.10: Build everything and run all tests**

```
cmake --build --preset conan-release
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```
Expected: ALL PASS. The colorizer-tests existed before this change and exercise the shared core; they should remain green.

- [ ] **Step 5.11: Add the colorizer visual fixture**

Create `test_data/colorizer_smokes/smoke_url_in_toml.toml`:

```toml
# Sample lockfile fragment — exercises URL detection inside TOML strings.
[[package]]
name = "certifi"
version = "2026.2.25"

[[package.wheels]]
url = "https://files.pythonhosted.org/packages/9a/3c/c17fb3ca/certifi.whl"
hash = "sha256:027692e4402ad994f1c42e52a4997a9763c646b73e4096e4d5d6db8af1d6f0fa"
upload-time = "2026-02-25T02:54:15.766Z"

[[package.wheels]]
url = "https://files.pythonhosted.org/packages/de/ad/beef/wheel.whl"

# Plain comment with a URL: see https://example.com/docs for details.
# Also a parenthesized one (https://docs.example.com/api).
```

- [ ] **Step 5.12: Generate the colorizer pixel golden**

Per `scripts/visual-test.sh` stage 3, the harness runs:

```
./build/Release/screenshot_tool.exe test_data/colorizer_smokes/smoke_url_in_toml.toml --full --dark
```

That produces `smoke_url_in_toml.toml_dark.png` next to the source file. Inspect it visually — the URLs should be underlined and tinted with the theme's link color (a blue/cyan, depending on the dark-theme palette).

If the output looks correct, copy it to the golden:

```
cp test_data/colorizer_smokes/smoke_url_in_toml.toml_dark.png \
   test_data/colorizer_smokes/smoke_url_in_toml.toml_golden.png
```

(That's the same staging convention the existing colorizer smokes follow — `smoke_dark.cpp_golden.png` etc.)

- [ ] **Step 5.13: Run the full visual regression**

```
./scripts/visual-test.sh
```
Expected: All three stages PASS, including the new colorizer smoke at ≥95% similarity.

If existing colorizer smokes (`smoke_dark.cpp_golden.png` etc.) drop in similarity, inspect the diff. The most likely cause is that those samples contain URLs in comments and the plan's URL detection is now styling them differently. If the underlining + link-color treatment is correct and intended, regenerate those goldens too:

```
./build/Release/screenshot_tool.exe test_data/colorizer_smokes/smoke_dark.cpp --full --dark
cp test_data/colorizer_smokes/smoke_dark.cpp_dark.png test_data/colorizer_smokes/smoke_dark.cpp_golden.png
# repeat for any other affected smoke
./scripts/visual-test.sh
```

- [ ] **Step 5.14: Manual smoke check (optional but recommended)**

This is a manual test the user runs to validate end-to-end behaviour in TC. Steps the user runs:

1. Build, copy the produced `.wlx64`s and `wlx-listerine-core.dll` into the TC plugin install dir (or run the existing `scripts/package.ps1` if that's how staging is wired).
2. Open a real `.lock` / `Cargo.lock` / `uv.lock` in TC's lister using the colorizer plugin.
3. Hover any URL — cursor should change to a hand.
4. Click the URL — system browser should open.
5. Right-click the URL — context menu should show "Open Link" and "Copy Link Address" enabled; both should work.
6. Open a markdown doc whose TOC links carry trailing `-` (e.g. one of the user's existing docs) — clicking jumps.
7. Open a markdown doc with a heading like `## Heading [^](#top)` — clicking the `^` scrolls to top.
8. Double-click inside `kebab-case-name` and `snake_case_name` in both plugins — full token selected.

- [ ] **Step 5.15: Commit**

```
git add src/plugin_colorizer/layout/colorizer_layout.cpp \
        src/plugin_colorizer/window/colorizer_host_adapter.cpp \
        src/runtime/host/context_menu.h \
        test_data/colorizer_smokes/smoke_url_in_toml.toml \
        test_data/colorizer_smokes/smoke_url_in_toml.toml_golden.png
# Add any regenerated colorizer smoke goldens too.
git commit -m "$(cat <<'EOF'
feat(plugin-colorizer): detect and click URLs in any colorized text

The colorizer now scans each laid-out line for http/https/ftp/file URLs
and emits InteractiveSpans into LayoutBlock::spans plus a ColorRange
that overrides foreground to palette.link with MOD_UNDERLINE — visible
across every grammar (TOML lockfiles, JSON, YAML, comments in C++,
plain-text fallback). InteractionEngine is wired into ColorViewState;
WM_MOUSEMOVE switches the cursor to a hand over URL spans, WM_LBUTTONDOWN
opens via ShellExecuteW (using the shared open_external_url helper),
and the right-click context menu's OpenLink / CopyLinkAddress branches
are now functional. ColorizerViewLike concept gained an interaction
requirement; build_colorizer_menu_context now hit-tests at the cursor.

Visual fixture smoke_url_in_toml.toml exercises TOML strings + comments.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-review summary

- §1 covered by Task 1 (`is_word_char` whitelist + 7 tests).
- §3 covered by Task 2 (`normalize_fragment` + 5 tests).
- §4 covered by Task 3 (extend `create_text_layout` signature; rewrite `layout_heading`; new doctest + visual fixture).
- §2 covered by Tasks 4 + 5 (scanner + tests + helper, then colorizer wiring + visual fixture).
- Each section's spec test plan is honoured: doctest cases per item, two new visual regression fixtures (`29_heading_with_inline_link.md` + `smoke_url_in_toml.toml`), manual smoke list at Step 5.14.
- No placeholders ("TBD", "implement later", "add validation"). Every code step shows the actual code.
- Type consistency: `UrlMatch` uses `int start/end` everywhere; `open_external_url` signature stable; `create_text_layout`'s new fourth parameter is `IDWriteTextFormat*` consistently in `.h` declaration, `.cpp` definition, and `layout_heading`'s call site.
- Each task ends with a single self-contained commit. Order matches the spec's implementation-order section.

---

## Execution

Plan complete and saved to `docs/superpowers/plans/2026-05-09-interaction-polish.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
