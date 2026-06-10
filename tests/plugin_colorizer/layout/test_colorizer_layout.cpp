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
using wlx::plugin_colorizer::layout::apply_spans_to_range;
using wlx::plugin_colorizer::layout::ByteRange;
using wlx::plugin_colorizer::layout::ColoredDecision;
using wlx::plugin_colorizer::layout::colored_interval_update;
using wlx::plugin_colorizer::layout::ColorizerDisplayConfig;
using wlx::plugin_colorizer::layout::layout_source;
using wlx::plugin_colorizer::layout::viewport_byte_range;
using wlx::runtime::layout::LayoutBlock;
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
    auto doc = layout_source(factory.Get(), L"\tx", "\tx", colors, theme,
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

// ---- apply_spans_to_range (incremental viewport recoloring) -----------------

namespace {

// Build a color-LESS skeleton layout (empty ColorizeResult) and capture the
// per-block UTF-8 byte starts via the out-param — exactly the host's open path.
LayoutDocument run_skeleton(IDWriteFactory* dwrite, const std::string& raw_utf8,
                            std::vector<int>& out_line_byte_starts) {
    ThemeService theme;
    ColorizeResult empty;
    ColorizerDisplayConfig display;  // no word_wrap -> lazy/incremental path
    std::wstring source(raw_utf8.begin(), raw_utf8.end());  // ASCII-only in tests
    return layout_source(dwrite, source, raw_utf8, empty, theme,
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

TEST_CASE("apply_spans_to_range: only in-window blocks get color_ranges") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    const std::string raw = "int a;\nint b;\nint c;";  // 3 lines
    std::vector<int> starts;
    auto doc = run_skeleton(factory.Get(), raw, starts);
    REQUIRE(doc.blocks.size() == 3);

    // One span per line, each covering "int" (3 bytes at the line start).
    ColorizeResult all;
    all.spans.push_back(make_span(starts[0], 3, 0xAA0000));
    all.spans.push_back(make_span(starts[1], 3, 0xAA0000));
    all.spans.push_back(make_span(starts[2], 3, 0xAA0000));

    // Color only the middle line's byte window [7, 13).
    apply_spans_to_range(doc, raw, starts, all,
                         /*byte_lo=*/static_cast<uint32_t>(starts[1]),
                         /*byte_hi=*/static_cast<uint32_t>(starts[2]),
                         /*tab_width=*/4);

    CHECK(doc.blocks[0].text_runs[0].color_ranges.empty());  // outside window
    CHECK(doc.blocks[2].text_runs[0].color_ranges.empty());  // outside window
    REQUIRE(doc.blocks[1].text_runs[0].color_ranges.size() == 1);
    const auto& cr = doc.blocks[1].text_runs[0].color_ranges[0];
    CHECK(cr.start == 0);
    CHECK(cr.length == 3);
    CHECK(cr.color == 0xAA0000);
}

TEST_CASE("apply_spans_to_range: re-applying the same window is idempotent") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    const std::string raw = "int a;\nint b;\nint c;";
    std::vector<int> starts;
    auto doc = run_skeleton(factory.Get(), raw, starts);

    ColorizeResult all;
    all.spans.push_back(make_span(starts[0], 3, 0x112233));

    auto color_line0 = [&] {
        apply_spans_to_range(doc, raw, starts, all, 0,
                             static_cast<uint32_t>(starts[1]), 4);
    };
    color_line0();
    auto first = doc.blocks[0].text_runs[0].color_ranges;
    color_line0();  // re-apply
    const auto& second = doc.blocks[0].text_runs[0].color_ranges;
    REQUIRE(first.size() == second.size());
    REQUIRE(first.size() == 1);
    CHECK(first[0].start == second[0].start);
    CHECK(first[0].length == second[0].length);
    CHECK(first[0].color == second[0].color);
}

TEST_CASE("apply_spans_to_range: clearing a window with no spans empties in-window ranges") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    const std::string raw = "int a;\nint b;\nint c;";
    std::vector<int> starts;
    auto doc = run_skeleton(factory.Get(), raw, starts);

    ColorizeResult all;
    all.spans.push_back(make_span(starts[1], 3, 0xCAFE00));
    // Color the middle line.
    apply_spans_to_range(doc, raw, starts, all,
                         static_cast<uint32_t>(starts[1]),
                         static_cast<uint32_t>(starts[2]), 4);
    REQUIRE(doc.blocks[1].text_runs[0].color_ranges.size() == 1);

    // Re-color the same window with an EMPTY span set -> ranges cleared.
    ColorizeResult none;
    apply_spans_to_range(doc, raw, starts, none,
                         static_cast<uint32_t>(starts[1]),
                         static_cast<uint32_t>(starts[2]), 4);
    CHECK(doc.blocks[1].text_runs[0].color_ranges.empty());
}

TEST_CASE("apply_spans_to_range: whole-doc window matches the eager whole-doc build") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    const std::string raw = "int a;\nint b;\nint c;";
    std::wstring source(raw.begin(), raw.end());

    ColorizeResult colors;
    colors.spans.push_back(make_span(0, 3, 0xAA0000));   // line0 "int"
    colors.spans.push_back(make_span(7, 3, 0xBB0000));   // line1 "int"
    colors.spans.push_back(make_span(14, 3, 0xCC0000));  // line2 "int"

    // Eager whole-doc build (colors passed up front).
    ThemeService theme;
    ColorizerDisplayConfig display;
    auto eager = layout_source(factory.Get(), source, raw, colors, theme,
                               false, 800.0f, display);

    // Skeleton + incremental whole-doc recolor.
    std::vector<int> starts;
    auto incr = run_skeleton(factory.Get(), raw, starts);
    apply_spans_to_range(incr, raw, starts, colors, 0, UINT32_MAX, 4);

    REQUIRE(eager.blocks.size() == incr.blocks.size());
    for (size_t b = 0; b < eager.blocks.size(); ++b) {
        const auto& e = eager.blocks[b].text_runs[0].color_ranges;
        const auto& i = incr.blocks[b].text_runs[0].color_ranges;
        REQUIRE(e.size() == i.size());
        for (size_t k = 0; k < e.size(); ++k) {
            CHECK(e[k].start == i[k].start);
            CHECK(e[k].length == i[k].length);
            CHECK(e[k].color == i[k].color);
        }
    }
}

TEST_CASE("apply_spans_to_range: multi-line span starting before the window colors only the in-window tail") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    const std::string raw = "int a;\nint b;\nint c;";  // starts 0/7/14
    std::vector<int> starts;
    auto doc = run_skeleton(factory.Get(), raw, starts);

    // One span covering ALL three lines (bytes [0, 20)).
    ColorizeResult all;
    all.spans.push_back(make_span(0, 20, 0x123456));

    // Window = the middle line only.
    apply_spans_to_range(doc, raw, starts, all,
                         static_cast<uint32_t>(starts[1]),
                         static_cast<uint32_t>(starts[2]), 4);

    CHECK(doc.blocks[0].text_runs[0].color_ranges.empty());  // before window
    CHECK(doc.blocks[2].text_runs[0].color_ranges.empty());  // after window
    REQUIRE(doc.blocks[1].text_runs[0].color_ranges.size() == 1);
    const auto& cr = doc.blocks[1].text_runs[0].color_ranges[0];
    CHECK(cr.start == 0);    // span's in-window tail covers the whole line
    CHECK(cr.length == 6);   // "int b;"
    CHECK(cr.color == 0x123456);
}

TEST_CASE("non-ASCII line: multiple spans keep correct wchar offsets") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    // "// <e-acute>t<e-acute> abc" — U+00E9 is 2 UTF-8 bytes, so byte offsets
    // diverge from wchar offsets after byte 3. Both spans must convert
    // independently on the same line.
    const std::string raw = "// \xC3\xA9t\xC3\xA9 abc";
    const std::wstring source = L"// \x00E9t\x00E9 abc";

    ColorizeResult colors;
    colors.spans.push_back(make_span(3, 5, 0x111111));  // the accented word -> wchar [3,6)
    colors.spans.push_back(make_span(9, 3, 0x222222));  // bytes of "abc" -> wchar [7,10)

    ThemeService theme;
    ColorizerDisplayConfig display;
    auto doc = layout_source(factory.Get(), source, raw, colors, theme,
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

    // The incremental (windowed) path must use the same byte->wchar mapping.
    std::vector<int> starts;
    ColorizeResult empty;
    auto incr = layout_source(factory.Get(), source, raw, empty, theme,
                              false, 800.0f, display, /*timings=*/nullptr, &starts);
    apply_spans_to_range(incr, raw, starts, colors, 0,
                         static_cast<uint32_t>(raw.size()), 4);
    const auto& icrs = incr.blocks[0].text_runs[0].color_ranges;
    REQUIRE(icrs.size() == 2);
    CHECK(icrs[0].start == 3);
    CHECK(icrs[0].length == 3);
    CHECK(icrs[1].start == 7);
    CHECK(icrs[1].length == 3);
}

TEST_CASE("non-ASCII line: 4-byte emoji (surrogate pair) span offsets") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    // "a<U+1F600>b": the emoji is 4 UTF-8 bytes but TWO wchars (a surrogate pair).
    const std::string raw = "a\xF0\x9F\x98\x80""b";
    const std::wstring source = L"a\U0001F600b";

    ColorizeResult colors;
    colors.spans.push_back(make_span(1, 4, 0x111111));  // the emoji -> wchar [1,3)
    colors.spans.push_back(make_span(5, 1, 0x222222));  // 'b' -> wchar [3,4)

    ThemeService theme;
    ColorizerDisplayConfig display;
    auto doc = layout_source(factory.Get(), source, raw, colors, theme,
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
    const std::wstring source = L"int a;";

    ColorizeResult colors;
    ColorSpan s = make_span(0, 3, 0xAA0000);  // "int"
    s.modifiers = static_cast<uint8_t>(MOD_BOLD | MOD_STRIKETHROUGH);
    colors.spans.push_back(s);

    ThemeService theme;
    ColorizerDisplayConfig display;  // word_wrap off -> lazy path
    auto doc = layout_source(factory.Get(), source, raw, colors, theme,
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
    const std::wstring source = L"int a;";

    ColorizeResult colors;
    ColorSpan s = make_span(0, 3, 0xAA0000);
    s.modifiers = MOD_BOLD;
    colors.spans.push_back(s);

    ThemeService theme;
    ColorizerDisplayConfig display;
    display.word_wrap = true;  // eager: layouts built (and measured) up front
    auto doc = layout_source(factory.Get(), source, raw, colors, theme,
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

TEST_CASE("modifiers: recoloring a materialized block re-bakes them on re-materialize") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    const std::string raw = "int a;";
    std::vector<int> starts;
    auto doc = run_skeleton(factory.Get(), raw, starts);
    REQUIRE(doc.blocks.size() == 1);
    materialize_all(doc);  // layout built with no syntax ranges yet
    REQUIRE(doc.blocks[0].text_runs[0].layout);

    ColorizeResult colors;
    ColorSpan s = make_span(0, 3, 0xAA0000);
    s.modifiers = MOD_BOLD;
    colors.spans.push_back(s);
    apply_spans_to_range(doc, raw, starts, colors, 0,
                         static_cast<uint32_t>(raw.size()), 4);

    // The materialized layout was dropped so the next paint rebuilds it with
    // the new ranges (and their modifiers).
    CHECK(!doc.blocks[0].text_runs[0].layout);
    materialize_all(doc);
    auto* layout = doc.blocks[0].text_runs[0].layout.Get();
    REQUIRE(layout);
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_REGULAR;
    layout->GetFontWeight(0, &weight, nullptr);
    CHECK(weight == DWRITE_FONT_WEIGHT_BOLD);
}

// ---- viewport_byte_range (visible blocks -> source byte range) --------------

namespace {

// Build N synthetic blocks stacked vertically, each `h` DIPs tall starting at
// `y0`, plus a parallel line_byte_starts (one entry per block). Block i covers
// document Y [y0 + i*h, y0 + (i+1)*h). Mirrors the colorizer's one-line-per-block
// grid; only rect.top/bottom matter to viewport_byte_range.
std::vector<LayoutBlock> make_blocks(const std::vector<int>& byte_starts,
                                     float y0 = 4.0f, float h = 10.0f) {
    std::vector<LayoutBlock> blocks;
    blocks.reserve(byte_starts.size());
    for (size_t i = 0; i < byte_starts.size(); ++i) {
        LayoutBlock b;
        float top = y0 + static_cast<float>(i) * h;
        b.rect = D2D1::RectF(0.0f, top, 100.0f, top + h);
        blocks.push_back(std::move(b));
    }
    return blocks;
}

}  // namespace

TEST_CASE("viewport_byte_range: empty blocks -> empty range") {
    std::vector<LayoutBlock> blocks;       // none
    std::vector<int> starts;               // none
    auto vr = viewport_byte_range(blocks, starts, /*raw_size=*/0,
                                  /*scroll_y=*/0.0f, /*viewport_h=*/100.0f,
                                  /*overscan=*/100.0f);
    CHECK(vr.empty);
}

TEST_CASE("viewport_byte_range: empty line_byte_starts -> empty range") {
    auto blocks = make_blocks({0, 10, 20});
    std::vector<int> starts;               // mismatched: zero starts
    auto vr = viewport_byte_range(blocks, starts, /*raw_size=*/30,
                                  0.0f, 100.0f, 100.0f);
    CHECK(vr.empty);
}

TEST_CASE("viewport_byte_range: single block at scroll 0 starts at block0 byte") {
    // One block; raw_size is the doc-end sentinel for the last block's hi.
    const std::vector<int> starts = {0};
    auto blocks = make_blocks(starts);
    auto vr = viewport_byte_range(blocks, starts, /*raw_size=*/6,
                                  /*scroll_y=*/0.0f, /*viewport_h=*/100.0f,
                                  /*overscan=*/100.0f);
    REQUIRE_FALSE(vr.empty);
    CHECK(vr.lo == 0);
    CHECK(vr.hi == 6);   // last block -> raw_size (NOT raw_size + 1)
}

TEST_CASE("viewport_byte_range: scroll 0 small viewport, no overscan -> only block0") {
    // 4 blocks, byte starts 0/10/20/30, raw_size 40. Block0 = Y[4,14),
    // block1 = Y[14,24), ... With scroll 0, viewport_h 10, overscan 0:
    // visible window [0,10) overlaps only block0 (its top 4 <= 10, bottom 14 >= 0;
    // block1 top 14 > 10 -> break). hi = line_byte_starts[1] = 10.
    const std::vector<int> starts = {0, 10, 20, 30};
    auto blocks = make_blocks(starts);
    auto vr = viewport_byte_range(blocks, starts, /*raw_size=*/40,
                                  /*scroll_y=*/0.0f, /*viewport_h=*/10.0f,
                                  /*overscan=*/0.0f);
    REQUIRE_FALSE(vr.empty);
    CHECK(vr.lo == 0);
    CHECK(vr.hi == 10);  // next block's byte start (not the last block)
}

TEST_CASE("viewport_byte_range: scrolled down starts at the right block") {
    // Blocks: 0=Y[4,14) 1=Y[14,24) 2=Y[24,34) 3=Y[34,44). scroll_y 30,
    // viewport_h 10, overscan 0 -> over_top 30, over_bottom 40. Scan:
    //   block0 bottom 14 < 30 -> continue; block1 bottom 24 < 30 -> continue;
    //   block2 bottom 34 (not < 30), top 24 (not > 40) -> first=last=2;
    //   block3 top 34 (not > 40) -> last=3. first=2, last=3.
    const std::vector<int> starts = {0, 10, 20, 30};
    auto blocks = make_blocks(starts);
    auto vr = viewport_byte_range(blocks, starts, /*raw_size=*/40,
                                  /*scroll_y=*/30.0f, /*viewport_h=*/10.0f,
                                  /*overscan=*/0.0f);
    REQUIRE_FALSE(vr.empty);
    CHECK(vr.lo == 20);  // line_byte_starts[2]
    CHECK(vr.hi == 40);  // block3 is the last block -> raw_size
}

TEST_CASE("viewport_byte_range: overscan extends the visible range (hi grows)") {
    // Same scroll/viewport as the no-overscan block0 case, but one screenful of
    // overscan on each side pulls in neighbours. scroll_y 0, viewport_h 10,
    // overscan 10 -> window [-10, 20): block0 Y[4,14) and block1 Y[14,24) (top
    // 14 <= 20) are in; block2 top 24 > 20 -> break. first=0, last=1. hi=20.
    const std::vector<int> starts = {0, 10, 20, 30};
    auto blocks = make_blocks(starts);
    auto vr = viewport_byte_range(blocks, starts, /*raw_size=*/40,
                                  /*scroll_y=*/0.0f, /*viewport_h=*/10.0f,
                                  /*overscan=*/10.0f);
    REQUIRE_FALSE(vr.empty);
    CHECK(vr.lo == 0);
    CHECK(vr.hi == 20);  // overscan pulled in block1, hi = starts[2]
}

TEST_CASE("viewport_byte_range: huge viewport covers ALL blocks (the --full case)") {
    // A huge viewport_h makes every block visible -> [0, raw_size), exactly the
    // whole-document range the tool's --full path produces.
    const std::vector<int> starts = {0, 10, 20, 30};
    auto blocks = make_blocks(starts);
    auto vr = viewport_byte_range(blocks, starts, /*raw_size=*/40,
                                  /*scroll_y=*/0.0f, /*viewport_h=*/1.0e9f,
                                  /*overscan=*/0.0f);
    REQUIRE_FALSE(vr.empty);
    CHECK(vr.lo == 0);
    CHECK(vr.hi == 40);  // last block -> raw_size = whole doc
}

TEST_CASE("viewport_byte_range: scrolled past the end -> empty") {
    // Window sits entirely below every block: nothing visible.
    const std::vector<int> starts = {0, 10, 20, 30};
    auto blocks = make_blocks(starts);  // last bottom = 44
    auto vr = viewport_byte_range(blocks, starts, /*raw_size=*/40,
                                  /*scroll_y=*/1000.0f, /*viewport_h=*/10.0f,
                                  /*overscan=*/0.0f);
    CHECK(vr.empty);
}

TEST_CASE("viewport_byte_range: mismatched lengths bound the scan to min(blocks, starts)") {
    // 4 blocks but only 2 byte starts -> n = 2; a huge viewport visits blocks
    // 0 and 1 only. last = 1, and last+1 (==2) is NOT < starts.size() (==2),
    // so hi falls back to raw_size.
    const std::vector<int> starts = {0, 10};
    auto blocks = make_blocks({0, 10, 20, 30});
    auto vr = viewport_byte_range(blocks, starts, /*raw_size=*/40,
                                  /*scroll_y=*/0.0f, /*viewport_h=*/1.0e9f,
                                  /*overscan=*/0.0f);
    REQUIRE_FALSE(vr.empty);
    CHECK(vr.lo == 0);
    CHECK(vr.hi == 40);  // last+1 out of range -> raw_size
}

// ---- colored_interval_update (skip / union / reset) -------------------------

TEST_CASE("colored_interval_update: initial empty interval -> highlight [vlo,vhi)") {
    // colored interval (0,0) is empty (chi <= clo) -> never skip; record [vlo,vhi).
    auto d = colored_interval_update(/*vlo=*/10, /*vhi=*/20, /*clo=*/0, /*chi=*/0);
    CHECK_FALSE(d.skip);
    CHECK(d.new_lo == 10);
    CHECK(d.new_hi == 20);
}

TEST_CASE("colored_interval_update: visible fully inside colored -> skip") {
    auto d = colored_interval_update(/*vlo=*/20, /*vhi=*/30, /*clo=*/10, /*chi=*/40);
    CHECK(d.skip);
}

TEST_CASE("colored_interval_update: exact-boundary inside is still a skip") {
    // vlo == clo and vhi == chi: the window equals the colored interval -> skip.
    auto d = colored_interval_update(/*vlo=*/10, /*vhi=*/40, /*clo=*/10, /*chi=*/40);
    CHECK(d.skip);
}

TEST_CASE("colored_interval_update: visible extends below -> union") {
    // Overlaps the bottom of the colored interval and extends past it.
    auto d = colored_interval_update(/*vlo=*/30, /*vhi=*/60, /*clo=*/10, /*chi=*/40);
    CHECK_FALSE(d.skip);
    CHECK(d.new_lo == 10);  // min(10, 30)
    CHECK(d.new_hi == 60);  // max(40, 60)
}

TEST_CASE("colored_interval_update: visible extends above -> union") {
    auto d = colored_interval_update(/*vlo=*/0, /*vhi=*/20, /*clo=*/10, /*chi=*/40);
    CHECK_FALSE(d.skip);
    CHECK(d.new_lo == 0);   // min(10, 0)
    CHECK(d.new_hi == 40);  // max(40, 20)
}

TEST_CASE("colored_interval_update: abutting window unions (vlo == chi)") {
    // vlo == chi: not fully inside (vhi > chi), and the overlap/abut test
    // (vlo <= chi && vhi >= clo) holds -> union, not reset.
    auto d = colored_interval_update(/*vlo=*/40, /*vhi=*/50, /*clo=*/10, /*chi=*/40);
    CHECK_FALSE(d.skip);
    CHECK(d.new_lo == 10);
    CHECK(d.new_hi == 50);
}

TEST_CASE("colored_interval_update: disjoint jump above -> reset to [vlo,vhi)") {
    // Window starts strictly past the colored interval's top (vlo > chi) -> reset.
    auto d = colored_interval_update(/*vlo=*/100, /*vhi=*/120, /*clo=*/10, /*chi=*/40);
    CHECK_FALSE(d.skip);
    CHECK(d.new_lo == 100);
    CHECK(d.new_hi == 120);
}

TEST_CASE("colored_interval_update: disjoint jump below -> reset to [vlo,vhi)") {
    // Window ends strictly before the colored interval's bottom (vhi < clo) -> reset.
    auto d = colored_interval_update(/*vlo=*/0, /*vhi=*/5, /*clo=*/10, /*chi=*/40);
    CHECK_FALSE(d.skip);
    CHECK(d.new_lo == 0);
    CHECK(d.new_hi == 5);
}
