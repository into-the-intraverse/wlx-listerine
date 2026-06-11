// tests/plugin_colorizer/layout/test_colorizer_layout.cpp
#include <doctest/doctest.h>

#include "plugin_colorizer/layout/colorizer_layout.h"
#include "core_dll/colorizer/colorize_result.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/line_index.h"
#include "runtime/parser/link_target.h"
#include "runtime/theme/theme_service.h"
#include "wlx_core/text_modifier.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using wlx::core::colorizer::ColorizeResult;
using wlx::core::colorizer::ColorSpan;
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
                          float viewport_width = 800.0f,
                          bool word_wrap = false) {
    ThemeService theme;
    ColorizeResult colors;  // empty; URL detection runs independent of grammar coloring
    ColorizerDisplayConfig display;
    display.word_wrap = word_wrap;
    // Convert wide source to UTF-8 for the raw_utf8 parameter (ASCII-clean
    // here, so a 1:1 narrowing is sufficient).
    std::string raw_utf8;
    raw_utf8.reserve(source.size());
    for (wchar_t wc : source) {
        raw_utf8.push_back(static_cast<char>(wc));
    }
    return layout_source(dwrite, raw_utf8, colors, theme,
                         /*dark_mode=*/false, viewport_width, display);
}

int count_external_url_spans(const LayoutDocument& doc) {
    int n = 0;
    for (const auto& b : doc.blocks)
        for (const auto& s : b.spans)
            if (s.target.kind == LinkKind::ExternalUrl) ++n;
    return n;
}

// Simulate what RenderEngine::paint does on first paint of each visible block:
// build the deferred per-line IDWriteTextLayout + decorations (URL spans,
// whitespace markers, ...). The no-wrap path defers this via
// doc.materialize_block; the eager (word-wrap) path leaves the hook null, so
// this is a no-op there.
void materialize_all(LayoutDocument& doc) {
    if (!doc.materialize_block) return;
    for (int i = 0; i < static_cast<int>(doc.blocks.size()); ++i)
        doc.materialize_block(doc.blocks[i], i);
}

}  // namespace

TEST_CASE("layout_source: no URL in source produces no ExternalUrl spans") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto layout = run_layout(factory.Get(), L"int main() { return 0; }");
    CHECK(count_external_url_spans(layout) == 0);
}

TEST_CASE("layout_source: line numbers render via the gutter, not per-block bullets") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto layout = run_layout(factory.Get(), L"line one\nline two\nline three");

    // layout_source itself populates the line index and reserves a gutter,
    // so every caller (host + screenshot tool) gets line numbers without a
    // separate build_line_index() call.
    CHECK(layout.line_tops.size() == 3);
    CHECK(layout.gutter_width > 0.0f);

    // Line numbers are drawn by the shared gutter renderer (paint_line_numbers),
    // NOT the markdown list-bullet path (which wrapped 4-digit numbers in a
    // fixed 24px box). So no block should carry bullet text.
    for (const auto& b : layout.blocks)
        CHECK(b.bullet_text.empty());
}

TEST_CASE("layout_source: single URL on one line produces one ExternalUrl span") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto layout = run_layout(factory.Get(),
        L"// see https://example.com/page for details");
    materialize_all(layout);  // URL spans are built lazily, on first paint

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
    // Force wrap by giving a narrow viewport, enabling word_wrap, and using
    // a long URL. word_wrap defaults to false in ColorizerDisplayConfig
    // (NO_WRAP behaviour), so we must opt into wrapping for this scenario.
    const std::wstring src =
        L"// https://example.com/very/long/path/that/will/wrap/across/rows/abcdefghij";
    auto layout = run_layout(factory.Get(), src, /*viewport_width=*/100.0f,
                             /*word_wrap=*/true);

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
    materialize_all(layout);  // URL spans are built lazily, on first paint

    std::wstring captured;
    for (const auto& b : layout.blocks)
        for (const auto& s : b.spans)
            if (s.target.kind == LinkKind::ExternalUrl) {
                captured = s.target.url;
            }

    CHECK(captured == L"https://example.com");  // trailing dot trimmed by scan_urls
}

TEST_CASE("layout_source + build_line_index: one line_top per source line") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto layout = run_layout(factory.Get(), L"a\nbb\nccc\ndddd");

    wlx::runtime::layout::build_line_index(layout);

    REQUIRE(layout.line_tops.size() == 4);
    CHECK(layout.line_tops[0] < layout.line_tops[1]);
    CHECK(layout.line_tops[1] < layout.line_tops[2]);
    CHECK(layout.line_tops[2] < layout.line_tops[3]);
}

TEST_CASE("layout_source: tab_width 0 from unvalidated config is clamped, not a crash") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    // The host and the screenshot tool cast the raw TOML int64 straight into
    // tab_width, so 0 (or negatives) can reach layout. expand_tabs' column
    // math divides by tab_width; the clamp must keep it >= 1.
    ThemeService theme;
    ColorizeResult colors;
    ColorizerDisplayConfig display;
    display.tab_width = 0;
    auto doc = layout_source(factory.Get(), "\tx", colors, theme,
                             /*dark_mode=*/false, /*viewport_width=*/800.0f, display);
    REQUIRE(doc.blocks.size() == 1);
    CHECK(doc.blocks[0].text_runs[0].text == L" x");  // tab expanded with width 1
    materialize_all(doc);  // ctx.tab_width is also clamped (indent-guide stride)
}

TEST_CASE("materialize: tabbed line keeps its pre-expansion text for decorations") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    // "\tx " -> boundary whitespace = the leading tab + the trailing space.
    // materialize must see the PRE-expansion text (the tab), not the expanded
    // run.text (tab-free lines fall back to run.text; tabbed lines must not).
    auto doc = run_layout(factory.Get(), L"\tx ");
    REQUIRE(doc.blocks.size() == 1);
    CHECK(doc.blocks[0].text_runs[0].text == L"    x ");  // tab expanded (width 4)
    materialize_all(doc);
    const auto& markers = doc.blocks[0].ws_markers;
    REQUIRE(markers.size() == 2);
    CHECK(markers[0].is_tab);
    CHECK_FALSE(markers[1].is_tab);
}

// ---- layout_source: skeleton (no color_ranges) ------------------------------

namespace {

// Build a color-LESS skeleton layout (empty ColorizeResult) and capture the
// per-block UTF-8 byte starts via the out-param — exactly the host's open path.
LayoutDocument run_skeleton(IDWriteFactory* dwrite, const std::string& raw_utf8,
                            std::vector<int>& out_line_byte_starts) {
    ThemeService theme;
    ColorizeResult empty;
    ColorizerDisplayConfig display;  // no word_wrap -> lazy/incremental path
    return layout_source(dwrite, raw_utf8, empty, theme,
                         /*dark_mode=*/false, /*viewport_width=*/800.0f, display,
                         /*timings=*/nullptr, &out_line_byte_starts);
}

ColorSpan make_span(uint32_t start, uint32_t length, uint32_t color) {
    ColorSpan s;
    s.start = start; s.length = length; s.color = color;
    s.bg_color = 0; s.has_bg = false; s.modifiers = 0;
    return s;
}

}  // namespace

TEST_CASE("layout_source: empty colors yields a skeleton with no color_ranges") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    std::vector<int> starts;
    auto doc = run_skeleton(factory.Get(), "int a;\nint b;\nint c;", starts);
    REQUIRE(doc.blocks.size() == 3);
    REQUIRE(starts.size() == 3);
    for (const auto& b : doc.blocks) {
        REQUIRE(!b.text_runs.empty());
        CHECK(b.text_runs[0].color_ranges.empty());
    }
    // Byte starts: line0=0, line1=7 ("int a;\n"=7), line2=14.
    CHECK(starts[0] == 0);
    CHECK(starts[1] == 7);
    CHECK(starts[2] == 14);
}

TEST_CASE("non-ASCII line: multiple spans keep correct wchar offsets") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    // "// <e-acute>t<e-acute> abc" — U+00E9 is 2 UTF-8 bytes, so byte offsets
    // diverge from wchar offsets after byte 3. Both spans must convert
    // independently on the same line.
    const std::string raw = "// \xC3\xA9t\xC3\xA9 abc";

    ColorizeResult colors;
    colors.spans.push_back(make_span(3, 5, 0x111111));  // the accented word -> wchar [3,6)
    colors.spans.push_back(make_span(9, 3, 0x222222));  // bytes of "abc" -> wchar [7,10)

    ThemeService theme;
    ColorizerDisplayConfig display;
    auto doc = layout_source(factory.Get(), raw, colors, theme,
                             /*dark_mode=*/false, /*viewport_width=*/800.0f, display);
    REQUIRE(doc.blocks.size() == 1);
    const auto& crs = doc.blocks[0].text_runs[0].color_ranges;
    REQUIRE(crs.size() == 2);
    CHECK(crs[0].start == 3);
    CHECK(crs[0].length == 3);
    CHECK(crs[0].color == 0x111111);
    CHECK(crs[1].start == 7);
    CHECK(crs[1].length == 3);
    CHECK(crs[1].color == 0x222222);
}

TEST_CASE("non-ASCII line: 4-byte emoji (surrogate pair) span offsets") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    // "a<U+1F600>b": the emoji is 4 UTF-8 bytes but TWO wchars (a surrogate pair).
    const std::string raw = "a\xF0\x9F\x98\x80""b";

    ColorizeResult colors;
    colors.spans.push_back(make_span(1, 4, 0x111111));  // the emoji -> wchar [1,3)
    colors.spans.push_back(make_span(5, 1, 0x222222));  // 'b' -> wchar [3,4)

    ThemeService theme;
    ColorizerDisplayConfig display;
    auto doc = layout_source(factory.Get(), raw, colors, theme,
                             /*dark_mode=*/false, /*viewport_width=*/800.0f, display);
    REQUIRE(doc.blocks.size() == 1);
    const auto& crs = doc.blocks[0].text_runs[0].color_ranges;
    REQUIRE(crs.size() == 2);
    CHECK(crs[0].start == 1);
    CHECK(crs[0].length == 2);  // both halves of the surrogate pair
    CHECK(crs[1].start == 3);
    CHECK(crs[1].length == 1);
}

// ---- font modifiers baked into the layout at creation, not at paint ---------

TEST_CASE("modifiers: lazy materialize bakes bold/strikethrough into the line layout") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    const std::string raw = "int a;";

    ColorizeResult colors;
    ColorSpan s = make_span(0, 3, 0xAA0000);  // "int"
    s.modifiers = static_cast<uint8_t>(MOD_BOLD | MOD_STRIKETHROUGH);
    colors.spans.push_back(s);

    ThemeService theme;
    ColorizerDisplayConfig display;  // word_wrap off -> lazy path
    auto doc = layout_source(factory.Get(), raw, colors, theme,
                             /*dark_mode=*/false, /*viewport_width=*/800.0f, display);
    REQUIRE(doc.blocks.size() == 1);
    materialize_all(doc);

    auto* layout = doc.blocks[0].text_runs[0].layout.Get();
    REQUIRE(layout);
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_REGULAR;
    layout->GetFontWeight(0, &weight, nullptr);
    CHECK(weight == DWRITE_FONT_WEIGHT_BOLD);
    BOOL strike = FALSE;
    layout->GetStrikethrough(1, &strike, nullptr);
    CHECK(strike == TRUE);
    // Past the span, the base format is untouched.
    layout->GetFontWeight(4, &weight, nullptr);
    CHECK(weight == DWRITE_FONT_WEIGHT_REGULAR);
}

TEST_CASE("modifiers: eager (word-wrap) path applies them at creation, before measuring") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    const std::string raw = "int a;";

    ColorizeResult colors;
    ColorSpan s = make_span(0, 3, 0xAA0000);
    s.modifiers = MOD_BOLD;
    colors.spans.push_back(s);

    ThemeService theme;
    ColorizerDisplayConfig display;
    display.word_wrap = true;  // eager: layouts built (and measured) up front
    auto doc = layout_source(factory.Get(), raw, colors, theme,
                             /*dark_mode=*/false, /*viewport_width=*/800.0f, display);
    REQUIRE(doc.blocks.size() == 1);
    CHECK(!doc.materialize_block);  // eager docs install no hook

    auto* layout = doc.blocks[0].text_runs[0].layout.Get();
    REQUIRE(layout);
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_REGULAR;
    layout->GetFontWeight(0, &weight, nullptr);
    CHECK(weight == DWRITE_FONT_WEIGHT_BOLD);
}

TEST_CASE("modifiers: URL underline is set on the layout at materialize") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = run_layout(factory.Get(), L"// see https://example.com/x");
    materialize_all(doc);

    REQUIRE(doc.blocks.size() == 1);
    auto* layout = doc.blocks[0].text_runs[0].layout.Get();
    REQUIRE(layout);
    BOOL underline = TRUE;
    layout->GetUnderline(0, &underline, nullptr);   // before the URL
    CHECK(underline == FALSE);
    layout->GetUnderline(7, &underline, nullptr);   // 'h' of https://
    CHECK(underline == TRUE);
}

