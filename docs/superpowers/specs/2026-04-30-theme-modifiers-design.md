# Helix theme modifiers — design

**Date:** 2026-04-30
**Status:** Draft
**Source TODO:** README.md "Theme modifiers"

## Problem

`HelixTheme` parses Helix-format `.toml` themes but discards the `modifiers` array and the table-form `underline = { ... }` entry. `helix_theme.cpp:103` is an explicit `// TODO: future version` drop. Result: a Helix theme that specifies `"comment" = { fg = "#888", modifiers = ["italic"] }` renders only the foreground colour; the user's expectation that comments render in italic — set by every other tool that consumes Helix themes — is silently violated.

This affects both plugins:

- **wlx-listerine-colorizer** for every source file.
- **wlx-listerine-md** for fenced code blocks (the `Colorizer` instance is shared via `LayoutEngine`).

The downstream rendering primitives (`IDWriteTextLayout::SetFontWeight`, `SetFontStyle`, `SetUnderline`, `SetStrikethrough`) already exist next to the `SetDrawingEffect` call we use today (`render_engine.cpp:472`); the work is plumbing four bits from the parser to those calls.

## Goal

Honour `bold`, `italic`, `underlined`, `crossed_out` from Helix theme files and from the built-in `make_default` themes. Keep the renderer plugin-agnostic. Keep the on-the-wire ColorRange struct compact.

## Non-goals

- Helix terminal-only modifiers (`reversed`, `dim`, `slow_blink`, `rapid_blink`, `hidden`). Parsed and ignored.
- Underline colour and underline style (`curl`, `dashed`, `dotted`, `double_line`). DirectWrite has only one underline kind, painted in the run's foreground brush.
- Per-property scope cascading (e.g. `function.method` inheriting `function`'s italic when only fg is overridden). The existing `HelixTheme::resolve` returns the most-specific match in full and stops; modifiers follow the same rule. Out of scope here.
- Markdown-body emphasis runs (`*italic*`, `**bold**`). Those go through `InlineNode` style flags in `LayoutEngine::create_text_layout` and already render bold/italic. They never produce a `ColorRange`.

## Design

### Data model

A new tiny header `src/text_modifiers.h` defines the bit constants:

```cpp
#pragma once
#include <cstdint>

enum TextModifier : uint8_t {
    MOD_BOLD          = 1 << 0,
    MOD_ITALIC        = 1 << 1,
    MOD_UNDERLINE     = 1 << 2,
    MOD_STRIKETHROUGH = 1 << 3,
};
```

`helix_theme.h` and `layout_engine.h` both `#include "text_modifiers.h"`. Putting the bits in their own header keeps `render_engine.cpp` from pulling in `helix_theme.h`, which would couple the markdown render path to colorizer-core.

Two struct extensions:

- `ResolvedStyle` (`src/colorizer/helix_theme.h:12`) gains `uint8_t modifiers = 0;` next to `has_fg`/`has_bg`.
- `ColorRange` (`src/layout_engine.h:18`) gains `uint8_t modifiers = 0;` next to `has_bg`. One byte; the existing tail padding (`start, length, color, bg_color` = 16 bytes + `has_bg` = 17 → padded to 20) absorbs it, so `sizeof(ColorRange)` is unchanged on x64.

The empty-style guard at `helix_theme.cpp:183` becomes `style.has_fg || style.has_bg || style.modifiers` so a modifier-only theme entry (rare but legal) is preserved through inheritance instead of being dropped as "empty."

### Carrier propagation

Three intermediate carriers already shuttle fg/bg through the colorizer pipeline. Each gets the same field:

- `QueryHighlighter::CaptureStyle` (local struct in `query_highlighter.cpp:156`): copy `style->modifiers` into it at line 173.
- `RawSpan` (`query_highlighter.cpp:7`): store `mods`, propagate from `cs.modifiers` at the `raw.push_back(...)` site (line 194), include in the flatten output at line 211.
- `PerLineSpan` (`colorizer_layout.cpp:178`): store `modifiers`, copied from `sp.modifiers` at line 247, copied into `cr.modifiers` at line 283.

`ColorSpan` (`src/colorizer/colorizer.h:9`) — the public output of `Colorizer::colorize` — also gains `uint8_t modifiers = 0;`. `query_highlighter.cpp` line 211 sets it from the `RawSpan`.

### TOML parsing

`parse_style` in `src/colorizer/helix_theme.cpp:74`. Inside the `as_table()` arm, after the existing `fg` and `bg` reads, two new branches:

**1. `modifiers` array.**

```cpp
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
```

Comparison is case-sensitive — Helix itself is case-sensitive on these names.

**2. `underline` table form.**

```cpp
if ((*tbl)["underline"].is_table()) {
    style.modifiers |= MOD_UNDERLINE;
}
```

Helix accepts either `modifiers = ["underlined"]` or `underline = { color = "#…", style = "line" }`. We OR-in the bit; `underline.color` and `underline.style` are not honoured.

The simple-string form (`"comment" = "#888888"`) path is unchanged. `modifiers` stays 0.

### Rendering

`paint_text_runs` in `src/render_engine.cpp:467-474`. Replace the existing per-range loop with:

```cpp
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
```

Notes:

- A range with `modifiers != 0` but no foreground brush still gets its bold/italic/etc. applied. The brush condition only gates `SetDrawingEffect`.
- DirectWrite font fallback: when the configured `code_family` lacks an italic or bold face, DWrite synthesises a slant or weight automatically. No extra wiring needed.
- `SetUnderline` and `SetStrikethrough` paint in the run's foreground brush. `helix_theme.cpp` discards `underline.color`.
- `render_engine.cpp` includes `text_modifiers.h` (transitively, via `layout_engine.h`'s `ColorRange`). It does not include `helix_theme.h`.

### Built-in default theme

`HelixTheme::make_default` (`src/colorizer/helix_theme.cpp:233`) currently builds entries via:

```cpp
auto fg = [](uint32_t color) -> ResolvedStyle {
    return {color, 0, true, false};
};
```

The lambda gains a default-zero modifier param:

```cpp
auto fg = [](uint32_t color, uint8_t mods = 0) -> ResolvedStyle {
    return {color, 0, true, false, mods};
};
```

Two scopes get modifiers in **both** light and dark variants:

- `s["comment"]            = fg(<existing color>, MOD_ITALIC);`
- `s["keyword.directive"]  = fg(<existing color>, MOD_BOLD);`

Every other entry passes the implicit `0` and behaves exactly as today. Diagnostic/error entries (currently `{0, 0xFF4444, false, true}` with explicit field initialisers) keep their existing braces — the new `modifiers` field default-initialises to `0`, so no edit is required at those sites.

## Data flow

Initial open of a `.cpp` file in the colorizer plugin with a Helix theme that says `"comment" = { fg = "#608b4e", modifiers = ["italic"] }`:

1. `HelixTheme::load` parses `comment` → `ResolvedStyle{fg=0x608B4E, has_fg=true, modifiers=MOD_ITALIC}`.
2. `Colorizer::colorize` runs the tree-sitter query, `QueryHighlighter::highlight` resolves each `comment` capture against the theme; the `MOD_ITALIC` bit copies into `CaptureStyle`, then `RawSpan`, then the public `ColorSpan`.
3. `colorizer_layout.cpp` slices spans onto lines, propagates the bit through `PerLineSpan` into `ColorRange`.
4. `RenderEngine::paint_text_runs` iterates `run.color_ranges`, calls `SetDrawingEffect` with the comment brush and `SetFontStyle(ITALIC)` on the same range. DWrite renders the comment glyphs in the italic face (or synthesises a slant if the font lacks one).

## Error handling

- Unknown modifier strings (`"squigglified"`) — silently dropped. No log noise; community themes occasionally have typos.
- Mixed forms: a theme that uses both `modifiers = ["underlined"]` and `underline = { ... }` on the same scope — both branches OR into the same bit; idempotent.
- Empty `modifiers = []` — loop body runs zero times, bits stay 0, no allocation, no crash.
- Theme parse failure (TOML error) — existing `try/catch` at `helix_theme.cpp:155-159` keeps the file as a no-op; `make_default` fallback still applies.
- Font without italic/bold face — DWrite's synthesis is automatic and acceptable. No special handling.

## Testing

### Unit tests (`tests/test_colorizer_helix_theme.cpp`)

Add four cases:

1. **`HelixTheme parses modifiers array`** — write a temp `.toml` containing one entry per supported modifier (`bold`, `italic`, `underlined`, `crossed_out`); load via `HelixTheme::load`; assert each `ResolvedStyle::modifiers` equals the expected bit.
2. **`HelixTheme parses underline table form`** — temp `.toml` with `[some.scope] underline = { color = "#ff0000", style = "curl" }`; resolved style has `MOD_UNDERLINE` set; foreground unchanged from the entry's `fg` (or 0 if absent).
3. **`HelixTheme ignores terminal-only modifiers`** — temp `.toml` with `modifiers = ["reversed", "dim", "hidden"]` plus `fg = "#fff"`; entry survives the empty-guard at line 183 (because `has_fg`); `modifiers` is 0.
4. **`make_default applies italic to comment and bold to keyword.directive`** — call `make_default(true)` and `make_default(false)`; assert both scopes carry the expected bit, and that an unrelated scope (e.g. `string`) carries 0.

### Unit test for QueryHighlighter propagation (`colorizer-tests`)

Add one assertion to an existing `QueryHighlighter` test (or a new minimal case): an italic-marked theme entry produces a `ColorSpan` with `MOD_ITALIC` set. Don't pile more on top — the rest of the carrier mirroring is mechanical and exercised by visual regression.

### Visual regression

Run `./scripts/visual-test.sh` after implementation. Expected drifts: cases that contain comments or preprocessor directives (the C/C++/Rust/Python cases). Pure-data cases (JSON, TOML, plain text) should not drift. For each case that does drift:

1. Inspect the `_diff.png` to confirm the change is bold-directives or italic-comments and nothing else.
2. Run `bun run update-goldens -- <case_id>` to refresh that case's `_chrome.png` golden.
3. Commit only goldens that match expectation.

A drift on a case that has no comments and no preprocessor directives is a bug, not a golden update.

### Manual smoke

Open one `.cpp` and one `.py` file in TC's lister with the colorizer plugin; verify:

- `// comment` and `# comment` glyphs render visibly slanted.
- `#include`, `#define`, `def` (where applicable) render visibly heavier weight.

Open a `.md` file in the markdown plugin with a fenced ` ```cpp ` block; verify the same.

## Risks

- **Visual regression sweep is large.** ~10 of 27 cases plausibly drift. Each must be inspected before its golden is updated. Mitigation: review the diffs case by case in the implementation PR, not in bulk.
- **Default theme drift surprises users with no `theme = …` config.** They opted into "the built-in defaults" and now get italic comments without changing config. Mitigation: README's existing TODO bullet for theme modifiers signals this is coming; release notes call it out.
- **Font without italic — synthesised slant.** Some monospace fonts (e.g. older Consolas variants on stripped-down Windows installs) render synthesised italic poorly. Mitigation: documented behaviour, not a code fix; users can ship a `.toml` that drops `MOD_ITALIC` from `comment` if they prefer.

## Files touched

### Created
- `src/text_modifiers.h` — four bit constants (~12 lines).

### Modified
- `src/colorizer/helix_theme.h` — `#include "../text_modifiers.h"`; `uint8_t modifiers` on `ResolvedStyle`; updated TODO comment.
- `src/colorizer/helix_theme.cpp` — `parse_style` modifiers/underline branches; empty-style guard; `make_default` italic comment + bold keyword.directive in both light/dark; `fg` lambda gains optional `mods` param.
- `src/colorizer/colorizer.h` — `uint8_t modifiers` on `ColorSpan`.
- `src/colorizer/query_highlighter.cpp` — `modifiers` field on `CaptureStyle` and `RawSpan`; threaded into `result.push_back`.
- `src/colorizer/colorizer_layout.cpp` — `modifiers` field on `PerLineSpan`; propagated from `sp.modifiers` and into `cr.modifiers`.
- `src/layout_engine.h` — `#include "text_modifiers.h"`; `uint8_t modifiers` on `ColorRange`.
- `src/render_engine.cpp` — four conditional setter calls in `paint_text_runs` per-range loop.
- `tests/test_colorizer_helix_theme.cpp` — four new doctest cases.
- `tests/test_colorizer_query_highlighter.cpp` — one assertion that `MOD_ITALIC` from the theme surfaces as `MOD_ITALIC` on the resulting `ColorSpan`.
- README.md — drop the now-resolved "Theme modifiers" TODO bullet.

### Deleted
- The `// modifiers, underline — parsed but ignored` comment at `helix_theme.cpp:103`.
- The `// TODO: support text modifiers in a future version.` comment at `helix_theme.h:11`.

No CMake changes (the new header is referenced via existing include paths; tests are appended to the existing `add_executable` blocks). No Conan changes.
