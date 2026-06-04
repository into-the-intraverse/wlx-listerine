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
#include <string>
#include <vector>

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
