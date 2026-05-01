# Helix Theme Modifiers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Honour `bold`, `italic`, `underlined`, and `crossed_out` modifiers from Helix `.toml` themes (and from the built-in `make_default` themes) by threading a `uint8_t modifiers` byte from the parser through `ResolvedStyle` / `ColorSpan` / `ColorRange` and wiring four DWrite setter calls into `RenderEngine::paint_text_runs`.

**Architecture:** A new tiny header `src/text_modifiers.h` exposes four bit constants. `helix_theme.h` and `layout_engine.h` both include it; one byte gets carried through every existing fg/bg carrier (`ResolvedStyle`, `CaptureStyle`, `RawSpan`, `ColorSpan`, `PerLineSpan`, `ColorRange`). The renderer applies `SetFontWeight`/`SetFontStyle`/`SetUnderline`/`SetStrikethrough` next to the existing `SetDrawingEffect` call.

**Tech Stack:** C++20, MSVC, CMake 3.20+, Conan 2.x, doctest, Direct2D/DirectWrite, toml++.

---

## File Structure

### Created

- `src/text_modifiers.h` — Header-only. Single `enum TextModifier : uint8_t` with four bit constants. Included by `helix_theme.h` and `layout_engine.h`.

### Modified

- `src/colorizer/helix_theme.h` — `#include "../text_modifiers.h"`; `uint8_t modifiers = 0;` field on `ResolvedStyle`; updated comment.
- `src/colorizer/helix_theme.cpp` — `parse_style` learns to read `modifiers = [...]` and `underline = { ... }`; empty-style guard; `make_default` `fg` lambda gains optional `mods` param; italic added to `comment`, bold added to `keyword.directive` in both light/dark variants.
- `src/colorizer/colorizer.h` — `uint8_t modifiers = 0;` field on `ColorSpan`.
- `src/colorizer/query_highlighter.cpp` — `modifiers` field on `CaptureStyle` and `RawSpan`; threaded into the `result.push_back` site.
- `src/colorizer/colorizer_layout.cpp` — `modifiers` field on `PerLineSpan`; copied from `sp.modifiers` into `pls.modifiers`, then into `cr.modifiers`.
- `src/layout_engine.h` — `#include "text_modifiers.h"`; `uint8_t modifiers = 0;` field on `ColorRange`.
- `src/render_engine.cpp` — Four conditional setter calls in the per-range loop in `paint_text_runs`.
- `tests/test_colorizer_helix_theme.cpp` — Five new doctest cases (TOML `modifiers` array, `underline` table form, terminal-only modifiers ignored, default theme italic comments + bold directives, modifier-only entry survives empty-guard); rewrite the existing `modifiers are parsed but fg still works` test to also verify the bits are now applied.
- `tests/test_colorizer_query_highlighter.cpp` — One assertion that a theme entry with `MOD_ITALIC` produces a `ColorSpan` with `MOD_ITALIC` set.
- `README.md` — Drop the now-resolved "Theme modifiers" TODO bullet.

### Deleted

- `// modifiers, underline — parsed but ignored (TODO: future version)` comment at `src/colorizer/helix_theme.cpp:103`.
- `// TODO: support text modifiers in a future version.` comment at `src/colorizer/helix_theme.h:11`.

---

## Task 1: Add the `text_modifiers.h` header

**Files:**
- Create: `src/text_modifiers.h`

- [ ] **Step 1: Create the header**

Create `src/text_modifiers.h` with:

```cpp
#pragma once

#include <cstdint>

// Bit flags applied to text ranges by the colorizer pipeline.
// Stored on ResolvedStyle, ColorSpan, and ColorRange and consumed
// by RenderEngine::paint_text_runs via DWrite font/decoration setters.
enum TextModifier : uint8_t {
    MOD_BOLD          = 1 << 0,
    MOD_ITALIC        = 1 << 1,
    MOD_UNDERLINE     = 1 << 2,
    MOD_STRIKETHROUGH = 1 << 3,
};
```

- [ ] **Step 2: Build to confirm the header is well-formed**

Run: `cmake --build --preset conan-release`
Expected: clean build (the header is included nowhere yet, so this only checks syntax via inclusion in any later TU; for now no consumers — just verify nothing else broke).

- [ ] **Step 3: Commit**

```bash
git add src/text_modifiers.h
git commit -m "feat: add TextModifier bit constants header"
```

---

## Task 2: Extend `ResolvedStyle` with the modifiers byte

**Files:**
- Modify: `src/colorizer/helix_theme.h`

- [ ] **Step 1: Add the include and the field**

Edit `src/colorizer/helix_theme.h`. Replace lines 1–17 (the includes and the `ResolvedStyle` struct) with:

```cpp
#pragma once

#include "../text_modifiers.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

// A resolved style from a Helix theme. Carries fg/bg colors and a bitset
// of text modifiers (bold/italic/underline/strikethrough); see TextModifier
// in text_modifiers.h.
struct ResolvedStyle {
    uint32_t fg = 0;        // 0x00RRGGBB
    uint32_t bg = 0;        // 0x00RRGGBB
    bool has_fg = false;
    bool has_bg = false;
    uint8_t modifiers = 0;  // OR of TextModifier bits
};
```

Note: the `// TODO: support text modifiers in a future version.` comment is intentionally removed.

- [ ] **Step 2: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build. `make_default` and `parse_style` still produce default-zero `modifiers` because they use brace-enclosed initialisers; the new field default-initialises to 0 so existing call sites keep working.

- [ ] **Step 3: Run existing theme tests to confirm no regression**

Run: `./build/Release/colorizer-tests.exe`
Expected: all existing helix_theme tests pass unchanged.

- [ ] **Step 4: Commit**

```bash
git add src/colorizer/helix_theme.h
git commit -m "feat(colorizer): add modifiers field to ResolvedStyle"
```

---

## Task 3: Parse `modifiers` array and `underline` table form (test-first)

**Files:**
- Modify: `tests/test_colorizer_helix_theme.cpp` (add tests, update existing one)
- Modify: `src/colorizer/helix_theme.cpp` (parse_style)

- [ ] **Step 1: Add four new failing tests**

Append to `tests/test_colorizer_helix_theme.cpp` (after the last existing TEST_CASE):

```cpp
// ===========================================================================
// Modifier parsing
// ===========================================================================

TEST_CASE("HelixTheme load: modifiers array sets bold/italic/underline/strikethrough") {
    TempThemeDir tmp;
    tmp.write("test_mods", R"(
        "a" = { fg = "#FFFFFF", modifiers = ["bold"] }
        "b" = { fg = "#FFFFFF", modifiers = ["italic"] }
        "c" = { fg = "#FFFFFF", modifiers = ["underlined"] }
        "d" = { fg = "#FFFFFF", modifiers = ["crossed_out"] }
        "e" = { fg = "#FFFFFF", modifiers = ["bold", "italic", "underlined", "crossed_out"] }
    )");

    auto theme = HelixTheme::load("test_mods", tmp.path().string());

    auto a = theme.resolve("a"); REQUIRE(a.has_value());
    auto b = theme.resolve("b"); REQUIRE(b.has_value());
    auto c = theme.resolve("c"); REQUIRE(c.has_value());
    auto d = theme.resolve("d"); REQUIRE(d.has_value());
    auto e = theme.resolve("e"); REQUIRE(e.has_value());

    CHECK(a->modifiers == MOD_BOLD);
    CHECK(b->modifiers == MOD_ITALIC);
    CHECK(c->modifiers == MOD_UNDERLINE);
    CHECK(d->modifiers == MOD_STRIKETHROUGH);
    CHECK(e->modifiers == (MOD_BOLD | MOD_ITALIC | MOD_UNDERLINE | MOD_STRIKETHROUGH));
}

TEST_CASE("HelixTheme load: underline table form sets MOD_UNDERLINE") {
    TempThemeDir tmp;
    tmp.write("test_uline", R"(
        "scope_a" = { fg = "#AAAAAA", underline = { color = "#FF0000", style = "curl" } }
    )");

    auto theme = HelixTheme::load("test_uline", tmp.path().string());
    auto s = theme.resolve("scope_a");
    REQUIRE(s.has_value());
    CHECK(s->fg == 0xAAAAAA);
    CHECK((s->modifiers & MOD_UNDERLINE) != 0);
    // color and style are intentionally ignored — no assertion on them.
}

TEST_CASE("HelixTheme load: terminal-only modifiers are silently ignored") {
    TempThemeDir tmp;
    tmp.write("test_term", R"(
        "scope_b" = { fg = "#BBBBBB", modifiers = ["reversed", "dim", "slow_blink", "rapid_blink", "hidden"] }
    )");

    auto theme = HelixTheme::load("test_term", tmp.path().string());
    auto s = theme.resolve("scope_b");
    REQUIRE(s.has_value());
    CHECK(s->fg == 0xBBBBBB);
    CHECK(s->modifiers == 0);
}

TEST_CASE("HelixTheme load: modifier-only entry survives empty-style guard") {
    TempThemeDir tmp;
    tmp.write("test_modonly", R"(
        "scope_c" = { modifiers = ["bold"] }
    )");

    auto theme = HelixTheme::load("test_modonly", tmp.path().string());
    auto s = theme.resolve("scope_c");
    REQUIRE(s.has_value());
    CHECK(!s->has_fg);
    CHECK(!s->has_bg);
    CHECK(s->modifiers == MOD_BOLD);
}
```

Then **rewrite** the existing test at `tests/test_colorizer_helix_theme.cpp:265-274` (the `HelixTheme load: modifiers are parsed but fg still works` case). Replace its body with:

```cpp
TEST_CASE("HelixTheme load: modifiers are parsed and fg still works") {
    TempThemeDir tmp;
    tmp.write("test_mod", R"(
        "keyword" = { fg = "#CC00CC", modifiers = ["bold", "italic"] }
    )");

    auto theme = HelixTheme::load("test_mod", tmp.path().string());
    CHECK(theme.resolve_fg("keyword", 0) == 0xCC00CC);
    auto s = theme.resolve("keyword");
    REQUIRE(s.has_value());
    CHECK(s->modifiers == (MOD_BOLD | MOD_ITALIC));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe`
Expected: the four new cases FAIL (modifiers stays 0); the rewritten case fails on the new `s->modifiers` assertion. Reasons: `parse_style` does not yet read `modifiers` or `underline`, and the empty-style guard at `helix_theme.cpp:183` still drops modifier-only entries.

- [ ] **Step 3: Update `parse_style` in helix_theme.cpp**

In `src/colorizer/helix_theme.cpp`, replace the function `parse_style` at lines 74-107 with:

```cpp
static ResolvedStyle parse_style(
    const toml::node& node,
    const std::unordered_map<std::string, uint32_t>& palette)
{
    ResolvedStyle style;

    if (auto* str = node.as_string()) {
        // Simple string: foreground color only
        auto color = resolve_color(str->get(), palette);
        if (color) {
            style.fg = *color;
            style.has_fg = true;
        }
    } else if (auto* tbl = node.as_table()) {
        // Table form: { fg = "...", bg = "...", modifiers = [...], underline = {...} }
        if (auto fg_val = (*tbl)["fg"].value<std::string>()) {
            auto color = resolve_color(*fg_val, palette);
            if (color) {
                style.fg = *color;
                style.has_fg = true;
            }
        }
        if (auto bg_val = (*tbl)["bg"].value<std::string>()) {
            auto color = resolve_color(*bg_val, palette);
            if (color) {
                style.bg = *color;
                style.has_bg = true;
            }
        }
        if (auto* mods = (*tbl)["modifiers"].as_array()) {
            for (auto&& m : *mods) {
                if (auto* s = m.as_string()) {
                    const std::string& v = s->get();
                    if      (v == "bold")        style.modifiers |= MOD_BOLD;
                    else if (v == "italic")      style.modifiers |= MOD_ITALIC;
                    else if (v == "underlined")  style.modifiers |= MOD_UNDERLINE;
                    else if (v == "crossed_out") style.modifiers |= MOD_STRIKETHROUGH;
                    // others (reversed/dim/blink/hidden) — terminal-only, ignored
                }
            }
        }
        if ((*tbl)["underline"].is_table()) {
            // Helix's table form: underline = { color = "...", style = "..." }.
            // DWrite has only one underline kind, painted in the run's foreground
            // brush, so color and style are intentionally ignored.
            style.modifiers |= MOD_UNDERLINE;
        }
    }

    return style;
}
```

- [ ] **Step 4: Update the empty-style guard**

In `src/colorizer/helix_theme.cpp` at line 183 (inside `load_impl`), change:

```cpp
        if (style.has_fg || style.has_bg) {
            styles[key_str] = style;  // Child overrides parent
        }
```

to:

```cpp
        if (style.has_fg || style.has_bg || style.modifiers) {
            styles[key_str] = style;  // Child overrides parent
        }
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe`
Expected: all five new/modified cases PASS. All existing tests still pass.

- [ ] **Step 6: Commit**

```bash
git add src/colorizer/helix_theme.cpp tests/test_colorizer_helix_theme.cpp
git commit -m "feat(colorizer): parse Helix modifiers array and underline table form"
```

---

## Task 4: Apply italic + bold modifiers in `make_default`

**Files:**
- Modify: `tests/test_colorizer_helix_theme.cpp` (new test)
- Modify: `src/colorizer/helix_theme.cpp` (make_default)

- [ ] **Step 1: Add a failing test**

Append to `tests/test_colorizer_helix_theme.cpp`:

```cpp
TEST_CASE("HelixTheme::make_default: comment is italic, keyword.directive is bold") {
    auto dark  = HelixTheme::make_default(true);
    auto light = HelixTheme::make_default(false);

    auto dc = dark.resolve("comment");           REQUIRE(dc.has_value());
    auto lc = light.resolve("comment");          REQUIRE(lc.has_value());
    auto dd = dark.resolve("keyword.directive"); REQUIRE(dd.has_value());
    auto ld = light.resolve("keyword.directive"); REQUIRE(ld.has_value());

    CHECK((dc->modifiers & MOD_ITALIC) != 0);
    CHECK((lc->modifiers & MOD_ITALIC) != 0);
    CHECK((dd->modifiers & MOD_BOLD)   != 0);
    CHECK((ld->modifiers & MOD_BOLD)   != 0);

    // Unrelated scope still has no modifiers.
    auto ds = dark.resolve("string");
    REQUIRE(ds.has_value());
    CHECK(ds->modifiers == 0);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe`
Expected: FAIL — `dc->modifiers` is 0 (and so on for the other three).

- [ ] **Step 3: Update the `fg` lambda and the two scope entries**

In `src/colorizer/helix_theme.cpp`, find `make_default` (line 233). Replace the lambda at lines 237-239 with:

```cpp
    auto fg = [](uint32_t color, uint8_t mods = 0) -> ResolvedStyle {
        return {color, 0, true, false, mods};
    };
```

In **both** the `if (!dark_mode)` (light) branch and the `else` (dark) branch, change two entries:

Light branch — find:
```cpp
        s["keyword.directive"]  = fg(0xAF00DB);
```
and:
```cpp
        s["comment"]            = fg(0x008000);
```

Replace with:
```cpp
        s["keyword.directive"]  = fg(0xAF00DB, MOD_BOLD);
```
and:
```cpp
        s["comment"]            = fg(0x008000, MOD_ITALIC);
```

Dark branch — find:
```cpp
        s["keyword.directive"]  = fg(0xC586C0);
```
and:
```cpp
        s["comment"]            = fg(0x6A9955);
```

Replace with:
```cpp
        s["keyword.directive"]  = fg(0xC586C0, MOD_BOLD);
```
and:
```cpp
        s["comment"]            = fg(0x6A9955, MOD_ITALIC);
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe`
Expected: PASS. All other tests unchanged.

- [ ] **Step 5: Commit**

```bash
git add src/colorizer/helix_theme.cpp tests/test_colorizer_helix_theme.cpp
git commit -m "feat(colorizer): italic comments, bold directives in default theme"
```

---

## Task 5: Add modifiers to `ColorSpan` and propagate through QueryHighlighter

**Files:**
- Modify: `src/colorizer/colorizer.h` (add field on ColorSpan)
- Modify: `src/colorizer/query_highlighter.cpp` (CaptureStyle, RawSpan, push_back)
- Modify: `tests/test_colorizer_query_highlighter.cpp` (one new test)

- [ ] **Step 1: Add the field on `ColorSpan`**

In `src/colorizer/colorizer.h`, replace the `ColorSpan` struct (lines 9-15) with:

```cpp
struct ColorSpan {
    uint32_t start = 0;       // byte offset in UTF-8 source
    uint32_t length = 0;
    uint32_t color = 0;       // foreground 0x00RRGGBB
    uint32_t bg_color = 0;
    bool has_bg = false;
    uint8_t modifiers = 0;    // OR of TextModifier bits
};
```

(`helix_theme.h` already pulls in `text_modifiers.h`, and `colorizer.h` already includes `helix_theme.h` at line 7, so `MOD_*` constants and `uint8_t` are visible.)

- [ ] **Step 2: Add a failing propagation test**

Append to `tests/test_colorizer_query_highlighter.cpp`:

```cpp
TEST_CASE("QueryHighlighter: theme modifier bits surface on ColorSpan"
    * doctest::skip(!has_c_grammar())) {
    GrammarRegistry reg(L"grammars");

    // Build a theme where comment is italic.
    auto theme = HelixTheme::make_default(true);
    REQUIRE((theme.resolve("comment")->modifiers & MOD_ITALIC) != 0);

    const char* source = "// hi\nint x = 1;";
    auto* tree = reg.parse("c", source);
    auto* query = reg.get_query("c");
    REQUIRE(tree != nullptr);
    REQUIRE(query != nullptr);

    auto spans = QueryHighlighter::highlight(tree, query, theme, source);

    // At least one span over the comment range [0, 5) ("// hi") must carry MOD_ITALIC.
    bool found_italic_comment = false;
    for (const auto& s : spans) {
        if (s.start < 5 && s.start + s.length <= 5 && (s.modifiers & MOD_ITALIC)) {
            found_italic_comment = true;
            break;
        }
    }
    CHECK(found_italic_comment);

    ts_tree_delete(tree);
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe`
Expected: FAIL on the new test case (`s.modifiers` is uninitialised garbage but defaults to 0, so the bit is missing).

- [ ] **Step 4: Update `CaptureStyle`, `RawSpan`, and the push_back site**

In `src/colorizer/query_highlighter.cpp`:

Replace the `RawSpan` struct (lines 7-14) with:

```cpp
struct RawSpan {
    uint32_t start;
    uint32_t end;
    uint32_t pattern_index;
    uint32_t color;
    uint32_t bg_color;
    bool has_bg;
    uint8_t modifiers;
};
```

In `QueryHighlighter::highlight`, replace the local `CaptureStyle` struct (lines 156-161) with:

```cpp
    struct CaptureStyle {
        uint32_t fg = 0;
        uint32_t bg = 0;
        bool has_fg = false;
        bool has_bg = false;
        uint8_t modifiers = 0;
    };
```

Inside the capture-resolution loop, replace lines 167-173 (the `if (style)` block) with:

```cpp
        auto style = theme.resolve(scope);
        if (style) {
            capture_styles[i].fg = style->fg;
            capture_styles[i].has_fg = style->has_fg;
            capture_styles[i].bg = style->bg;
            capture_styles[i].has_bg = style->has_bg;
            capture_styles[i].modifiers = style->modifiers;
        }
```

Update the keep-or-skip predicate at line 193:

```cpp
        if (!cs.has_fg && !cs.has_bg && cs.modifiers == 0) continue;
```

Update the `raw.push_back` at line 194:

```cpp
        raw.push_back({start, end, match.pattern_index, cs.fg, cs.bg, cs.has_bg, cs.modifiers});
```

Update the final flatten output at line 211 (push of `result`):

```cpp
        result.push_back({eff_start, span.end - eff_start, span.color, span.bg_color, span.has_bg, span.modifiers});
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe`
Expected: the new propagation test PASSES; all existing query_highlighter tests still pass.

- [ ] **Step 6: Commit**

```bash
git add src/colorizer/colorizer.h src/colorizer/query_highlighter.cpp tests/test_colorizer_query_highlighter.cpp
git commit -m "feat(colorizer): propagate theme modifiers through QueryHighlighter to ColorSpan"
```

---

## Task 6: Carry modifiers through `colorizer_layout` and `ColorRange`

**Files:**
- Modify: `src/layout_engine.h` (ColorRange field)
- Modify: `src/colorizer/colorizer_layout.cpp` (PerLineSpan field, propagation)

- [ ] **Step 1: Extend `ColorRange`**

In `src/layout_engine.h`, replace lines 1-24 (everything up to and including the `ColorRange` struct) with:

```cpp
#pragma once

#include "document_model.h"
#include "theme_service.h"
#include "text_modifiers.h"

class Colorizer;

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct ColorRange {
    uint32_t start = 0;
    uint32_t length = 0;
    uint32_t color = 0;       // foreground 0x00RRGGBB
    uint32_t bg_color = 0;    // background 0x00RRGGBB
    bool has_bg = false;
    uint8_t modifiers = 0;    // OR of TextModifier bits
};
```

- [ ] **Step 2: Extend `PerLineSpan` and propagate**

In `src/colorizer/colorizer_layout.cpp`:

Replace the `PerLineSpan` struct (lines 178-185) with:

```cpp
    struct PerLineSpan {
        int line_idx = 0;
        int wchar_start = 0;  // in original (pre-expansion) line text
        int wchar_len = 0;
        uint32_t color = 0;
        uint32_t bg_color = 0;
        bool has_bg = false;
        uint8_t modifiers = 0;
    };
```

In the line-slicing loop, replace the `pls` assignment block (lines 241-249) with:

```cpp
                if (wlen > 0 && wstart >= 0 &&
                    wstart < static_cast<int>(lines[li].text.size())) {
                    wlen = std::min(wlen, static_cast<int>(lines[li].text.size()) - wstart);
                    PerLineSpan pls;
                    pls.line_idx    = li;
                    pls.wchar_start = wstart;
                    pls.wchar_len   = wlen;
                    pls.color       = sp.color;
                    pls.bg_color    = sp.bg_color;
                    pls.has_bg      = sp.has_bg;
                    pls.modifiers   = sp.modifiers;
                    line_spans[li].push_back(pls);
                }
```

In the per-line layout loop, replace the `cr` assignment block (lines 277-285) with:

```cpp
            if (exp_len > 0) {
                ColorRange cr;
                cr.start    = static_cast<uint32_t>(exp_start);
                cr.length   = static_cast<uint32_t>(exp_len);
                cr.color    = pls.color;
                cr.bg_color = pls.bg_color;
                cr.has_bg   = pls.has_bg;
                cr.modifiers = pls.modifiers;
                color_ranges.push_back(cr);
            }
```

- [ ] **Step 3: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build. No tests cover this layer directly; downstream visual regression catches errors.

- [ ] **Step 4: Run all tests**

Run: `./build/Release/tests.exe && ./build/Release/colorizer-tests.exe`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/layout_engine.h src/colorizer/colorizer_layout.cpp
git commit -m "feat(colorizer): carry modifiers through colorizer_layout into ColorRange"
```

---

## Task 7: Apply modifiers in the renderer

**Files:**
- Modify: `src/render_engine.cpp` (paint_text_runs per-range loop)

- [ ] **Step 1: Update the per-range loop**

In `src/render_engine.cpp`, replace `paint_text_runs` lines 460-482 with:

```cpp
void RenderEngine::paint_text_runs(const LayoutBlock& block, float offset_y) {
    for (auto& run : block.text_runs) {
        if (!run.layout) continue;

        auto* brush = get_brush(run.color);
        if (!brush) continue;

        // Apply per-range overrides: foreground brush + bold/italic/underline/strikethrough.
        for (auto& cr : run.color_ranges) {
            DWRITE_TEXT_RANGE range = {cr.start, cr.length};
            if (auto* cr_brush = get_brush(cr.color)) {
                run.layout->SetDrawingEffect(cr_brush, range);
            }
            if (cr.modifiers & MOD_BOLD) {
                run.layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
            }
            if (cr.modifiers & MOD_ITALIC) {
                run.layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
            }
            if (cr.modifiers & MOD_UNDERLINE) {
                run.layout->SetUnderline(TRUE, range);
            }
            if (cr.modifiers & MOD_STRIKETHROUGH) {
                run.layout->SetStrikethrough(TRUE, range);
            }
        }

        D2D1_POINT_2F origin = D2D1::Point2F(run.rect.left, run.rect.top + offset_y);

        rt_->DrawTextLayout(
            origin, run.layout.Get(), brush,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
}
```

(`render_engine.cpp` includes `render_engine.h`, which includes `layout_engine.h`, which now includes `text_modifiers.h`. `MOD_*` constants are visible without an explicit include.)

- [ ] **Step 2: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build.

- [ ] **Step 3: Run all tests**

Run: `./build/Release/tests.exe && ./build/Release/colorizer-tests.exe`
Expected: all pass. No new unit test added here — the renderer's effect is observable only via visual regression (next task) and manual smoke.

- [ ] **Step 4: Commit**

```bash
git add src/render_engine.cpp
git commit -m "feat(render): apply Helix theme modifiers in paint_text_runs"
```

---

## Task 8: Visual regression sweep

**Files:**
- Modify: `test_data/cases/**/*_chrome.png` (only those that drift legitimately)

- [ ] **Step 1: Run the visual regression suite**

Run: `./scripts/visual-test.sh`
Expected: pass-or-fail report. Cases containing comments and/or preprocessor directives are expected to drift (italic comments, bold `#include`/`#define`/`def`). Pure-data cases (JSON, TOML, plain text) should NOT drift; if they do, treat as a bug and stop here to investigate.

- [ ] **Step 2: For each drifting case, inspect the diff**

For every case reported as failing similarity, open `test_data/cases/<id>/<id>_diff.png` (and `<id>_actual.png` vs `<id>_chrome.png`). Confirm the visible delta is **only**:
- italic-shaped glyphs in comments, OR
- heavier-weight glyphs in preprocessor / directive keywords.

If a case shows any other delta (e.g. a non-comment span shifted, layout reflowed unexpectedly), STOP and investigate before regenerating its golden.

- [ ] **Step 3: Refresh golden PNGs for legitimate drifts**

For each confirmed-good drift, run:

```bash
bun run update-goldens -- <case_id>
```

Do not run `bun run update-goldens` (the all-cases form) without inspection — that would launder bugs into goldens.

- [ ] **Step 4: Re-run the suite to confirm green**

Run: `./scripts/visual-test.sh`
Expected: all 27 cases PASS at ≥95% similarity.

- [ ] **Step 5: Commit the refreshed goldens**

```bash
git add test_data/cases/
git commit -m "test(visual): refresh goldens for italic comments + bold directives"
```

(If no goldens needed updating because none drifted significantly, skip this commit.)

---

## Task 9: Update README and final verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Drop the resolved TODO bullet**

In `README.md`, find the bullet that begins:

```
- **Theme modifiers** — Helix themes can specify text modifiers (bold, italic, underline) per scope; currently only foreground/background colors are applied
```

Delete the entire bullet (it is one of three bullets under `## 🚧 TODO`). The other two bullets (CPP highlighting on GHA, lazy grammar loading) stay as-is.

- [ ] **Step 2: Full clean rebuild**

Run:
```bash
cmake --build --preset conan-release
```
Expected: clean.

- [ ] **Step 3: Run both test suites**

Run:
```bash
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```
Expected: all tests pass. `tests.exe` count unchanged (the markdown-side tests don't touch helix_theme directly). `colorizer-tests.exe` count is **previous + 6** (5 new in test_colorizer_helix_theme.cpp + 1 new in test_colorizer_query_highlighter.cpp).

- [ ] **Step 4: Manual smoke test in TC (required before merge)**

1. Open a `.cpp` file with the colorizer plugin. Verify `// comment` glyphs render visibly slanted; `#include`, `#define` render visibly heavier than surrounding code.
2. Open a `.py` file. Verify `# comment` glyphs render visibly slanted.
3. Open a `.md` file with the markdown plugin that contains a fenced ` ```cpp ` block with comments and `#include`. Verify the same effects apply inside the code fence.
4. If you have a Helix theme in `config/themes/` that uses `modifiers = ["bold"]` or similar, drop it in and verify the modifier renders.

If any of (1)-(3) fails, investigate before proceeding.

- [ ] **Step 5: Commit the README**

```bash
git add README.md
git commit -m "docs: drop resolved theme-modifiers TODO bullet"
```

- [ ] **Step 6: Summarize the branch**

Run: `git log --oneline master..HEAD`
Expected: ~9 commits cleanly describing the change. Names should match the order of tasks above.

---

## Spec Coverage Check

| Spec section | Task(s) |
|---|---|
| Data model — `text_modifiers.h` | Task 1 |
| Data model — `ResolvedStyle.modifiers` | Task 2 |
| Data model — `ColorSpan.modifiers` | Task 5 |
| Data model — `ColorRange.modifiers` | Task 6 |
| Carrier propagation — `CaptureStyle`, `RawSpan`, push_back | Task 5 |
| Carrier propagation — `PerLineSpan` | Task 6 |
| TOML parsing — `modifiers` array | Task 3 |
| TOML parsing — `underline` table form | Task 3 |
| Empty-style guard update | Task 3 |
| Rendering — four DWrite setters | Task 7 |
| Default theme — italic comment, bold keyword.directive | Task 4 |
| Unit tests — TOML parsing | Task 3 |
| Unit tests — make_default modifiers | Task 4 |
| Unit tests — QueryHighlighter propagation | Task 5 |
| Visual regression — sweep + golden updates | Task 8 |
| Manual smoke | Task 9 |
| README — drop resolved TODO | Task 9 |
