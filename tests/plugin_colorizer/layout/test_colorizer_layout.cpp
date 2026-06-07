// tests/plugin_colorizer/layout/test_colorizer_layout.cpp
#include <doctest/doctest.h>

#include "plugin_colorizer/layout/colorizer_layout.h"
#include "core_dll/colorizer/colorize_result.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/line_index.h"
#include "runtime/parser/link_target.h"
#include "runtime/theme/theme_service.h"

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
