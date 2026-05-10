# Markdown / colorizer URL-detection follow-ups — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land four cleanup commits closing out the URL-detection branch follow-ups: split `anchor_y` tests, cover `build_colorizer_menu_context`'s URL-hit path, migrate `EditConfig` handlers to `open_external_url`, and add a `layout_source` URL-detection doctest.

**Architecture:** Four independent commits in ascending blast-radius order (test split → test addition → small refactor → new test file with build wiring). Each commit ships green: full markdown test suite, full colorizer test suite, and visual regression all pass.

**Tech Stack:** C++20, MSVC, doctest, DirectWrite/Direct2D, CMake (Conan), tree-sitter (via `wlx-listerine-core`).

**Spec:** `docs/superpowers/specs/2026-05-10-md-colorizer-followups-design.md`.

---

## File map

| File | Touch | Why |
|---|---|---|
| `tests/runtime/interaction/test_interaction_engine.cpp` | **create** | New home for `anchor_y` tests (Task 1) |
| `tests/runtime/interaction/test_text_selection.cpp` | edit | Remove the six `anchor_y` `TEST_CASE`s (Task 1) |
| `tests/CMakeLists.txt` | edit | Register new test files + `wlx-core` linkage (Tasks 1, 4) |
| `tests/runtime/host/test_context_menu.cpp` | edit | Add 2 `TEST_CASE`s for URL-hit branch (Task 2) |
| `src/plugin_md/window/host_adapter.cpp` | edit | Replace inline ShellExecuteW with helper (Task 3) |
| `src/plugin_colorizer/window/colorizer_host_adapter.cpp` | edit | Replace inline ShellExecuteW with helper (Task 3) |
| `tests/plugin_colorizer/layout/test_colorizer_layout.cpp` | **create** | URL-detection doctests (Task 4) |

---

## Task 1: Split `anchor_y` tests into `test_interaction_engine.cpp`

**Files:**
- Create: `tests/runtime/interaction/test_interaction_engine.cpp`
- Modify: `tests/runtime/interaction/test_text_selection.cpp` (delete lines 349–419)
- Modify: `tests/CMakeLists.txt` (add new file to `tests` target)

### - [ ] Step 1.1: Create `test_interaction_engine.cpp` with the moved tests

Create the file with all six `anchor_y` test cases plus the helpers they depend on.

```cpp
// tests/runtime/interaction/test_interaction_engine.cpp
#include <doctest/doctest.h>
#include "runtime/interaction/interaction_engine.h"
#include "runtime/layout/layout_engine.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
#include <wrl/client.h>
#include <cstring>

using namespace wlx::runtime::interaction;
using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;
using namespace wlx::runtime::theme;

using Microsoft::WRL::ComPtr;

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
    // Defensive: a malformed fragment with a stray leading dash should
    // still resolve, even though slugify ensures stored slugs never have one.
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

TEST_CASE("InteractionEngine::anchor_y - all-dashes fragment returns nullopt") {
    // Stress the !s.empty() guards in normalize_fragment: '---' becomes
    // empty after the leading-dash strip, which must not UB on subsequent
    // operations.
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("# Intro\n\nbody");
    auto layout = do_layout(factory.Get(), doc);
    InteractionEngine eng(layout);
    auto y = eng.anchor_y(L"---");
    CHECK_FALSE(y.has_value());
}
```

### - [ ] Step 1.2: Delete the six `anchor_y` test cases from `test_text_selection.cpp`

Open `tests/runtime/interaction/test_text_selection.cpp` and delete lines 349–419 inclusive (the six `TEST_CASE("InteractionEngine::anchor_y - ...")` blocks). The last surviving line above the deletion should be line 347 (the closing brace of the `find_word_boundaries - path with hyphen and underscore` test) plus its trailing blank line at 348.

After deletion, the file ends with `find_word_boundaries - path with hyphen and underscore`. Don't touch any tests or helpers.

### - [ ] Step 1.3: Trim now-unused includes from `test_text_selection.cpp`

After removing the six tests, only `extract_selected_text` and `find_word_boundaries` test cases remain (plus `TextPosition` cases). They use `text_selection.h`, `layout_engine.h`, `markdown_parser.h`, `theme_service.h` — but no longer use `interaction_engine.h`.

Verify by grepping the surviving file:

```bash
grep -n "InteractionEngine\|anchor_y" tests/runtime/interaction/test_text_selection.cpp
```

Expected: zero matches. If zero, remove the line `#include "runtime/interaction/interaction_engine.h"` from the include block (currently line 3). If any matches appear, leave the include in place — surface what you found to the user.

### - [ ] Step 1.4: Add the new test file to the `tests` target in CMake

Open `tests/CMakeLists.txt`. Find the `add_executable(tests ...)` block (lines 4–23). Insert the new file alphabetically next to `test_text_selection.cpp`:

```cmake
    runtime/interaction/test_interaction_engine.cpp
    runtime/interaction/test_text_selection.cpp
    runtime/interaction/test_url_scanner.cpp
```

### - [ ] Step 1.5: Build

Run from repo root:

```bash
cmake --build --preset conan-release
```

Expected: clean build, no warnings introduced.

### - [ ] Step 1.6: Run the markdown test suite and verify count is unchanged

```bash
./build/Release/tests.exe
```

Expected: `203 | 203 passed | 0 failed`. Total count must remain at 203 (six tests moved, not added).

### - [ ] Step 1.7: Verify the moved tests are actually in the new TU

```bash
./build/Release/tests.exe -tc="InteractionEngine::anchor_y*" -ltc
```

Expected: lists exactly 6 test cases. If fewer, the new file isn't compiled in; if more, a duplicate slipped past Step 1.2.

### - [ ] Step 1.8: Run colorizer tests to confirm nothing else broke

```bash
./build/Release/colorizer-tests.exe
```

Expected: `137 | 137 passed`.

### - [ ] Step 1.9: Run visual regression

```bash
bash scripts/visual-test.sh
```

Expected: 29/29 markdown + 26/26 colorizer tokens + 6/6 colorizer pixels PASS.

### - [ ] Step 1.10: Commit

```bash
git add tests/runtime/interaction/test_interaction_engine.cpp \
        tests/runtime/interaction/test_text_selection.cpp \
        tests/CMakeLists.txt
git commit -m "test(interaction): split anchor_y tests into test_interaction_engine.cpp

The six InteractionEngine::anchor_y test cases lived in
test_text_selection.cpp because that's where they were first written
during the anchor-fragment normalization fix. They cover interaction
engine, not text selection; move them to a dedicated TU so test files
mirror src/runtime/interaction/.

No test count change (203 markdown, 137 colorizer; same as before).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Add URL-hit branch coverage for `build_colorizer_menu_context`

**Files:**
- Modify: `tests/runtime/host/test_context_menu.cpp` (append 2 `TEST_CASE`s)

### - [ ] Step 2.1: Write the failing tests

Append the following two `TEST_CASE`s to the end of `tests/runtime/host/test_context_menu.cpp` (after the existing `build_colorizer_menu_context: forwards force_grammar_id to active` case):

```cpp
TEST_CASE("build_colorizer_menu_context: surfaces ExternalUrl hit on ctx.link") {
    using namespace wlx::runtime::layout;
    using namespace wlx::runtime::parser;
    using namespace wlx::runtime::interaction;

    FakeColorizerView vs;
    vs.layout = std::make_shared<LayoutDocument>();

    LayoutBlock block;
    block.rect = D2D1::RectF(0.0f, 0.0f, 200.0f, 30.0f);
    InteractiveSpan span;
    span.target.kind = LinkKind::ExternalUrl;
    span.target.url  = L"https://example.com/x";
    span.rect = D2D1::RectF(0.0f, 0.0f, 100.0f, 20.0f);
    block.spans.push_back(span);
    vs.layout->blocks.push_back(block);

    vs.interaction = std::make_unique<InteractionEngine>(*vs.layout);

    auto ctx = build_colorizer_menu_context(vs,
        std::vector<LanguageOption>{ {"cpp", L"C++"} },
        50.0f, 10.0f);

    CHECK(ctx.link.present  == true);
    CHECK(ctx.link.url      == L"https://example.com/x");
    CHECK(ctx.link.external == true);
}

TEST_CASE("build_colorizer_menu_context: ignores InternalAnchor hits") {
    // The colorizer view deliberately surfaces ONLY ExternalUrl hits in the
    // menu (unlike the markdown view, which surfaces InternalAnchor and
    // RelativeDoc too). Defend that divergence here.
    using namespace wlx::runtime::layout;
    using namespace wlx::runtime::parser;
    using namespace wlx::runtime::interaction;

    FakeColorizerView vs;
    vs.layout = std::make_shared<LayoutDocument>();

    LayoutBlock block;
    block.rect = D2D1::RectF(0.0f, 0.0f, 200.0f, 30.0f);
    InteractiveSpan span;
    span.target.kind = LinkKind::InternalAnchor;
    span.target.anchor_fragment = L"intro";
    span.rect = D2D1::RectF(0.0f, 0.0f, 100.0f, 20.0f);
    block.spans.push_back(span);
    vs.layout->blocks.push_back(block);

    vs.interaction = std::make_unique<InteractionEngine>(*vs.layout);

    auto ctx = build_colorizer_menu_context(vs,
        std::vector<LanguageOption>{ {"cpp", L"C++"} },
        50.0f, 10.0f);

    CHECK(ctx.link.present == false);
}
```

### - [ ] Step 2.2: Build

```bash
cmake --build --preset conan-release
```

Expected: clean build. If `D2D1::RectF` isn't visible, confirm `<d2d1.h>` is reachable transitively via the existing `runtime/layout/...` includes already in this file (it is, via `interactive_span.h`).

### - [ ] Step 2.3: Run the new tests in isolation

```bash
./build/Release/tests.exe -tc="build_colorizer_menu_context: surfaces ExternalUrl*"
./build/Release/tests.exe -tc="build_colorizer_menu_context: ignores InternalAnchor*"
```

Expected: both PASS.

### - [ ] Step 2.4: Run the full markdown test suite and verify count

```bash
./build/Release/tests.exe
```

Expected: `205 | 205 passed | 0 failed` (203 + 2 new).

### - [ ] Step 2.5: Run colorizer + visual regression

```bash
./build/Release/colorizer-tests.exe
bash scripts/visual-test.sh
```

Expected: colorizer 137/137 PASS; visual 29/29 + 26/26 + 6/6.

### - [ ] Step 2.6: Commit

```bash
git add tests/runtime/host/test_context_menu.cpp
git commit -m "test(host): cover URL-hit branch of build_colorizer_menu_context

Add two test cases to lock in the colorizer menu's URL-link behavior:

  - ExternalUrl hit populates ctx.link with url + external=true
  - InternalAnchor / RelativeDoc hits are deliberately ignored
    (divergence from build_md_menu_context, which surfaces them)

The negative case is a parity-drift trip wire: if the colorizer is
later updated to follow internal anchors, this test fails and forces
an intentional update.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Route `EditConfig` through `open_external_url` in both plugins

**Files:**
- Modify: `src/plugin_md/window/host_adapter.cpp` (~lines 619–626)
- Modify: `src/plugin_colorizer/window/colorizer_host_adapter.cpp` (~lines 737–744)

### - [ ] Step 3.1: Confirm no external dependency on the old trace literal

Run from repo root:

```bash
git grep -n "EditConfig: ShellExecuteW" -- ':!docs/' ':!build/'
```

Expected: exactly two matches — one in `host_adapter.cpp`, one in `colorizer_host_adapter.cpp`. **Stop and surface to the user if any other hit exists** (would mean a saved log filter or external script depends on the literal).

### - [ ] Step 3.2: Confirm the `open_external_url` helper is already a known dependency

```bash
git grep -n "link_actions.h" -- src/plugin_md/window/host_adapter.cpp src/plugin_colorizer/window/colorizer_host_adapter.cpp
```

Expected: both files include `runtime/host/link_actions.h` (they already do — md plugin uses it for `OpenExternal` action, colorizer plugin uses it for in-text URL clicks). If either file is missing the include, add it before editing the body.

### - [ ] Step 3.3: Replace the inline block in `host_adapter.cpp`

Open `src/plugin_md/window/host_adapter.cpp`. Find the `case MenuResult::EditConfig:` arm (around line 619). Replace the body:

**Before:**

```cpp
        case MenuResult::EditConfig: {
            HINSTANCE hi = ShellExecuteW(nullptr, L"open", ctx.config_path.c_str(),
                                         nullptr, nullptr, SW_SHOW);
            if (reinterpret_cast<INT_PTR>(hi) <= 32) {
                WLX_TRACE(L"EditConfig: ShellExecuteW failed (code %lld) for %s",
                          static_cast<long long>(reinterpret_cast<INT_PTR>(hi)),
                          ctx.config_path.c_str());
            }
            break;
        }
```

**After:**

```cpp
        case MenuResult::EditConfig:
            wlx::runtime::host::open_external_url(ctx.config_path);
            break;
```

The braces and the `HINSTANCE` local are gone, so the case no longer needs a compound statement.

### - [ ] Step 3.4: Replace the inline block in `colorizer_host_adapter.cpp`

Open `src/plugin_colorizer/window/colorizer_host_adapter.cpp`. Find the `case MenuResult::EditConfig:` arm (around line 737). Same replacement — exact text in / out as Step 3.3.

### - [ ] Step 3.5: Confirm no `<shellapi.h>` include is now orphaned

For each of the two files, check whether any other call site needs `ShellExecuteW`:

```bash
git grep -n "ShellExecuteW\|<shellapi.h>" -- src/plugin_md/window/host_adapter.cpp
git grep -n "ShellExecuteW\|<shellapi.h>" -- src/plugin_colorizer/window/colorizer_host_adapter.cpp
```

Expected: zero hits for `ShellExecuteW` in both, since `open_external_url` is the only entry point left. If `#include <shellapi.h>` still appears with no users, **leave it alone** (other Win32 system headers may transitively need it; this is not the place to prune includes). The goal is behavior parity, not include hygiene.

### - [ ] Step 3.6: Build

```bash
cmake --build --preset conan-release
```

Expected: clean build.

### - [ ] Step 3.7: Run all test suites

```bash
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
bash scripts/visual-test.sh
```

Expected: markdown 205/205, colorizer 137/137, visual 29/29 + 26/26 + 6/6 — all unchanged from Task 2's final state.

### - [ ] Step 3.8: Commit

```bash
git add src/plugin_md/window/host_adapter.cpp \
        src/plugin_colorizer/window/colorizer_host_adapter.cpp
git commit -m "refactor(plugins): route EditConfig through open_external_url

Both plugins' MenuResult::EditConfig handlers were duplicating the
same ShellExecuteW + WLX_TRACE block to open the config file in the
default editor. Replace with the existing open_external_url helper
(ShellExecuteW with verb=open works identically for URLs and file
paths).

The trace literal changes from 'EditConfig: ShellExecuteW failed' to
'open_external_url: ShellExecuteW failed'; the URL string itself
identifies the path, and WLX_TRACE_TAG already names the plugin, so
no information is lost for diagnostics.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Add `layout_source` URL-detection doctest

**Files:**
- Create: `tests/plugin_colorizer/layout/test_colorizer_layout.cpp`
- Modify: `tests/CMakeLists.txt` (add file + colorizer_layout.cpp source + wlx-core linkage)

### - [ ] Step 4.1: Make the CMake change first (build wiring before test code)

Open `tests/CMakeLists.txt`. Find the `add_executable(colorizer-tests ...)` block (lines 31–45). Add two lines to the source list and one library to `target_link_libraries`:

**Before:**

```cmake
add_executable(colorizer-tests
    ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
    core_dll/colorizer/test_colorizer.cpp
    core_dll/theme/test_colorizer_helix_theme.cpp
    core_dll/grammar/test_colorizer_grammar.cpp
    core_dll/highlighting/test_colorizer_query_highlighter.cpp
    core_dll/grammar/test_colorizer_grammars.cpp
    plugin_colorizer/language/test_colorizer_routing.cpp
    plugin_colorizer/language/test_path_to_language.cpp
    plugin_colorizer/test_colorizer_smoke.cpp
    core_dll/abi/test_wlx_core_abi.cpp
    core_dll/registry/test_core_config.cpp
    core_dll/grammar/test_grammar_cache.cpp
    tools/test_token_json_writer.cpp
)

target_link_libraries(colorizer-tests PRIVATE
    wlx-listerine-core
    doctest::doctest
)
```

**After:**

```cmake
add_executable(colorizer-tests
    ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
    core_dll/colorizer/test_colorizer.cpp
    core_dll/theme/test_colorizer_helix_theme.cpp
    core_dll/grammar/test_colorizer_grammar.cpp
    core_dll/highlighting/test_colorizer_query_highlighter.cpp
    core_dll/grammar/test_colorizer_grammars.cpp
    plugin_colorizer/language/test_colorizer_routing.cpp
    plugin_colorizer/language/test_path_to_language.cpp
    plugin_colorizer/layout/test_colorizer_layout.cpp
    plugin_colorizer/test_colorizer_smoke.cpp
    core_dll/abi/test_wlx_core_abi.cpp
    core_dll/registry/test_core_config.cpp
    core_dll/grammar/test_grammar_cache.cpp
    tools/test_token_json_writer.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_colorizer/layout/colorizer_layout.cpp
)

target_link_libraries(colorizer-tests PRIVATE
    wlx-core
    wlx-listerine-core
    doctest::doctest
)
```

Two source-list additions (the test file + the production source) and one new link library (`wlx-core`). Order matters in `target_link_libraries` — list `wlx-core` before `wlx-listerine-core` so the linker resolves runtime symbols before the engine library that depends on them.

### - [ ] Step 4.2: Trial-build with the wiring change but no new test code yet

```bash
cmake --build --preset conan-release
```

Expected: clean build. **If symbol collisions appear** (anonymous-namespace name clashes between `colorizer_layout.cpp` and `wlx-core`), stop and surface to the user — that's a Stop Condition from the spec.

### - [ ] Step 4.3: Create `test_colorizer_layout.cpp` with the four test cases

```cpp
// tests/plugin_colorizer/layout/test_colorizer_layout.cpp
#include <doctest/doctest.h>

#include "plugin_colorizer/layout/colorizer_layout.h"
#include "core_dll/colorizer/colorize_result.h"
#include "runtime/layout/layout_document.h"
#include "runtime/parser/link_target.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>

using Microsoft::WRL::ComPtr;
using wlx::core::colorizer::ColorizeResult;
using wlx::plugin_colorizer::layout::ColorizerDisplayConfig;
using wlx::plugin_colorizer::layout::layout_source;
using wlx::runtime::layout::LayoutDocument;
using wlx::runtime::parser::LinkKind;
using wlx::runtime::theme::ThemeService;

namespace {

ComPtr<IDWriteFactory> create_dwrite_factory() {
    ComPtr<IDWriteFactory> factory;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    return factory;
}

// Run layout_source with default-constructed theme + display config and an
// empty ColorizeResult so URL detection is exercised in isolation.
LayoutDocument run_layout(IDWriteFactory* dwrite,
                          const std::wstring& source,
                          float viewport_width = 800.0f) {
    ThemeService theme;
    ColorizeResult colors;  // empty; URL detection runs independent of grammar coloring
    ColorizerDisplayConfig display;
    // Convert wide source to UTF-8 for the raw_utf8 parameter (ASCII-clean
    // here, so a 1:1 narrowing is sufficient).
    std::string raw_utf8(source.begin(), source.end());
    return layout_source(dwrite, source, raw_utf8, colors, theme,
                         /*dark_mode=*/false, viewport_width, display);
}

int count_external_url_spans(const LayoutDocument& doc) {
    int n = 0;
    for (const auto& b : doc.blocks)
        for (const auto& s : b.spans)
            if (s.target.kind == LinkKind::ExternalUrl) ++n;
    return n;
}

}  // namespace

TEST_CASE("layout_source: no URL in source produces no ExternalUrl spans") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto layout = run_layout(factory.Get(), L"int main() { return 0; }");
    CHECK(count_external_url_spans(layout) == 0);
}

TEST_CASE("layout_source: single URL on one line produces one ExternalUrl span") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto layout = run_layout(factory.Get(),
        L"// see https://example.com/page for details");

    int total = 0;
    bool url_seen = false;
    for (const auto& b : layout.blocks) {
        for (const auto& s : b.spans) {
            if (s.target.kind != LinkKind::ExternalUrl) continue;
            ++total;
            CHECK(s.target.url == L"https://example.com/page");
            // Span rect must lie within the parent block's rect.
            CHECK(s.rect.left   >= b.rect.left);
            CHECK(s.rect.top    >= b.rect.top);
            CHECK(s.rect.right  <= b.rect.right + 0.5f);   // small fp slack
            CHECK(s.rect.bottom <= b.rect.bottom + 0.5f);
            url_seen = true;
        }
    }
    CHECK(url_seen);
    CHECK(total == 1);
}

TEST_CASE("layout_source: long URL wrapped across rows produces multiple spans with same url") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    // Force wrap by giving a narrow viewport and a long URL.
    const std::wstring src =
        L"// https://example.com/very/long/path/that/will/wrap/across/rows/abcdefghij";
    auto layout = run_layout(factory.Get(), src, /*viewport_width=*/100.0f);

    std::vector<wlx::runtime::layout::InteractiveSpan> url_spans;
    for (const auto& b : layout.blocks)
        for (const auto& s : b.spans)
            if (s.target.kind == LinkKind::ExternalUrl)
                url_spans.push_back(s);

    REQUIRE(url_spans.size() >= 2);
    // All spans share the same URL string.
    for (const auto& s : url_spans) {
        CHECK(s.target.url == L"https://example.com/very/long/path/that/will/wrap/across/rows/abcdefghij");
    }
    // Spans are emitted in the order DirectWrite returns hit-test rects;
    // sorting by .top yields strictly increasing y for a wrapped line.
    std::sort(url_spans.begin(), url_spans.end(),
              [](const auto& a, const auto& b) { return a.rect.top < b.rect.top; });
    for (size_t i = 1; i < url_spans.size(); ++i) {
        CHECK(url_spans[i].rect.top >= url_spans[i - 1].rect.top);
    }
}

TEST_CASE("layout_source: trailing punctuation is excluded from the URL span") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto layout = run_layout(factory.Get(),
        L"// see https://example.com.");

    std::wstring captured;
    for (const auto& b : layout.blocks)
        for (const auto& s : b.spans)
            if (s.target.kind == LinkKind::ExternalUrl) {
                captured = s.target.url;
            }

    CHECK(captured == L"https://example.com");  // trailing dot trimmed by scan_urls
}
```

### - [ ] Step 4.4: Build

```bash
cmake --build --preset conan-release
```

Expected: clean build.

### - [ ] Step 4.5: Run only the new tests first

```bash
./build/Release/colorizer-tests.exe -tc="layout_source: *"
```

Expected: 4 PASS.

If any FAIL, **stop and surface to the user** — per spec Stop Conditions, a real bug here means visual regression was tolerant of a coordinate-math defect; decide before fixing.

### - [ ] Step 4.6: Run the full colorizer suite and verify count

```bash
./build/Release/colorizer-tests.exe
```

Expected: `141 | 141 passed | 0 failed` (137 + 4 new).

### - [ ] Step 4.7: Run markdown + visual regression

```bash
./build/Release/tests.exe
bash scripts/visual-test.sh
```

Expected: markdown 205/205, visual 29/29 + 26/26 + 6/6 — unchanged from Task 3.

### - [ ] Step 4.8: Commit

```bash
git add tests/plugin_colorizer/layout/test_colorizer_layout.cpp \
        tests/CMakeLists.txt
git commit -m "test(colorizer): doctest URL-detection in layout_source

Cover the URL-detection block in colorizer_layout.cpp's layout_source()
with four offset/coordinate-math doctests:

  - no URL in source -> zero ExternalUrl spans
  - single URL on one line -> one span, rect contained in its block
  - URL wrapped across rows -> multiple spans, same target.url, ordered by y
  - trailing punctuation trimmed in span.target.url (scan_urls contract)

Wiring: colorizer-tests now compiles plugin_colorizer/layout/colorizer_layout.cpp
directly and links wlx-core (for layout/url_scanner/parser symbols).
This mirrors how test_layout_engine.cpp exercises layout_engine.cpp via
the markdown 'tests' target.

Colorizer suite: 137 -> 141.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Final verification

### - [ ] Step 5.1: Full suite green

```bash
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
bash scripts/visual-test.sh
```

Expected:
- `tests.exe` — 205/205 PASS
- `colorizer-tests.exe` — 141/141 PASS
- visual — 29/29 markdown + 26/26 colorizer tokens + 6/6 colorizer pixels PASS

### - [ ] Step 5.2: Confirm 4 new commits

```bash
git log --oneline origin/master..HEAD
```

Expected: at least 5 commits — the spec commit (`docs: brainstorming spec ...`) plus the four task commits in order:

```
<sha> test(colorizer): doctest URL-detection in layout_source
<sha> refactor(plugins): route EditConfig through open_external_url
<sha> test(host): cover URL-hit branch of build_colorizer_menu_context
<sha> test(interaction): split anchor_y tests into test_interaction_engine.cpp
<sha> docs: brainstorming spec for md/colorizer URL-detection follow-ups
```

### - [ ] Step 5.3: `git status` clean

```bash
git status
```

Expected: `nothing to commit, working tree clean`.

### - [ ] Step 5.4: Surface push decision to the user

Don't push automatically. Report final state and ask whether to push (`git push origin master`).
