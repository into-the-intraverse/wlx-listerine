// tests/plugin_colorizer/layout/test_grid_window.cpp
#include <doctest/doctest.h>

#include "plugin_colorizer/layout/colorizer_layout.h"
#include "plugin_colorizer/layout/grid_geometry.h"
#include "plugin_colorizer/layout/grid_window.h"
#include "core_dll/colorizer/colorize_result.h"
#include "runtime/layout/layout_document.h"
#include "runtime/parser/link_target.h"
#include "runtime/theme/theme_service.h"
#include "wlx_core/text_modifier.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using wlx::core::colorizer::ColorizeResult;
using wlx::core::colorizer::ColorSpan;
using wlx::plugin_colorizer::layout::build_grid_line;
using wlx::plugin_colorizer::layout::ColorizerDisplayConfig;
using wlx::plugin_colorizer::layout::ColorsForRange;
using wlx::plugin_colorizer::layout::distribute_spans_to_lines;
using wlx::plugin_colorizer::layout::grid_line_top;
using wlx::plugin_colorizer::layout::grid_total_height;
using wlx::plugin_colorizer::layout::GridGeometry;
using wlx::plugin_colorizer::layout::layout_grid_skeleton;
using wlx::plugin_colorizer::layout::layout_source;
using wlx::plugin_colorizer::layout::MaterializeCtx;
using wlx::plugin_colorizer::layout::PerLineSpan;
using wlx::plugin_colorizer::layout::slide_grid_window;
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

// A colors_for that always returns no spans (plain-text / skeleton case).
ColorsForRange empty_colors() {
    return [](uint32_t, uint32_t) { return ColorizeResult{}; };
}

// Build N numbered lines "line0\nline1\n...lineN-1" (no trailing newline).
std::string numbered_lines(int n) {
    std::string out;
    for (int i = 0; i < n; ++i) {
        if (i) out.push_back('\n');
        out += "line";
        out += std::to_string(i);
    }
    return out;
}

}  // namespace

// ---- Case 1: skeleton structure ---------------------------------------------

TEST_CASE("grid skeleton: line starts, tops, height, no blocks") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    ThemeService theme;
    ColorizerDisplayConfig display;  // line_numbers on, word_wrap off

    // 10 lines: CRLF on line 0, a leading tab on line 1, a UTF-8 multibyte
    // char (e-acute, U+00E9 = 2 bytes) on line 2.
    const std::string raw =
        "line0\r\n"          // line 0: 7 bytes (incl \r\n)   start 0
        "\tindented\n"       // line 1: 10 bytes               start 7
        "caf\xC3\xA9\n"      // line 2: 6 bytes (e-acute 2B)   start 17
        "a3\n"               // line 3: 3 bytes                start 23
        "a4\n"               // line 4                         start 26
        "a5\n"               // line 5                         start 29
        "a6\n"               // line 6                         start 32
        "a7\n"               // line 7                         start 35
        "a8\n"               // line 8                         start 38
        "a9";                // line 9: 2 bytes (no newline)   start 41

    std::vector<int> starts;
    std::shared_ptr<MaterializeCtx> ctx;
    GridGeometry geo;
    auto doc = layout_grid_skeleton(factory.Get(), raw, theme,
                                    /*dark_mode=*/false, /*viewport_width=*/800.0f,
                                    display, &starts, &ctx, &geo);

    // No per-line blocks built; it IS a grid; 10 lines.
    CHECK(doc.blocks.empty());
    CHECK(doc.is_grid());
    CHECK(doc.grid_line_count == 10);
    CHECK(doc.first_block_line == 0);

    // Hand-computed byte starts.
    REQUIRE(starts.size() == 10);
    CHECK(starts[0] == 0);
    CHECK(starts[1] == 7);
    CHECK(starts[2] == 17);
    CHECK(starts[3] == 23);
    CHECK(starts[4] == 26);
    CHECK(starts[5] == 29);
    CHECK(starts[6] == 32);
    CHECK(starts[7] == 35);
    CHECK(starts[8] == 38);
    CHECK(starts[9] == 41);

    // ctx + geo are filled.
    REQUIRE(ctx != nullptr);
    CHECK(ctx->dwrite.Get() == factory.Get());
    CHECK(ctx->fmt != nullptr);
    CHECK(ctx->line_height > 0.0f);
    CHECK(geo.line_count == 10);
    CHECK(geo.line_height == doctest::Approx(ctx->line_height));
    CHECK(geo.top_pad == doctest::Approx(4.0f));

    // line_tops[i] == 4 + i * line_height (arithmetic).
    REQUIRE(doc.line_tops.size() == 10);
    for (int i = 0; i < 10; ++i) {
        CHECK(doc.line_tops[static_cast<size_t>(i)] ==
              doctest::Approx(4.0f + static_cast<float>(i) * geo.line_height));
    }

    // total_height == grid_total_height(geo).
    CHECK(doc.total_height == doctest::Approx(grid_total_height(geo)));
}

// ---- Case 2: parity with the old layout_source skeleton ---------------------

TEST_CASE("grid skeleton parity with layout_source") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    ThemeService theme;
    ColorizerDisplayConfig display;  // word_wrap off -> layout_source lazy path

    const std::string raw =
        "line0\r\n"
        "\tindented\n"
        "caf\xC3\xA9\n"
        "a3\na4\na5\na6\na7\na8\na9";

    // OLD path: layout_source with empty colors (its lazy skeleton).
    std::vector<int> old_starts;
    ColorizeResult empty;
    auto old_doc = layout_source(factory.Get(), raw, empty, theme,
                                 /*dark_mode=*/false, /*viewport_width=*/800.0f,
                                 display, /*timings=*/nullptr, &old_starts);

    // NEW path: grid skeleton.
    std::vector<int> new_starts;
    std::shared_ptr<MaterializeCtx> ctx;
    GridGeometry geo;
    auto new_doc = layout_grid_skeleton(factory.Get(), raw, theme,
                                        /*dark_mode=*/false, /*viewport_width=*/800.0f,
                                        display, &new_starts, &ctx, &geo);

    // line_byte_starts identical.
    REQUIRE(old_starts.size() == new_starts.size());
    for (size_t i = 0; i < old_starts.size(); ++i)
        CHECK(old_starts[i] == new_starts[i]);

    // line_tops: same count + values.
    REQUIRE(old_doc.line_tops.size() == new_doc.line_tops.size());
    for (size_t i = 0; i < old_doc.line_tops.size(); ++i)
        CHECK(old_doc.line_tops[i] == doctest::Approx(new_doc.line_tops[i]));

    // total_height + gutter_width equal (within fp slack).
    CHECK(old_doc.total_height == doctest::Approx(new_doc.total_height).epsilon(0.01));
    CHECK(old_doc.gutter_width == doctest::Approx(new_doc.gutter_width));

    // Surface the actual numbers (the report wants these for the shared input).
    MESSAGE("parity total_height=" << new_doc.total_height
            << " gutter_width=" << new_doc.gutter_width
            << " line_height=" << geo.line_height);
}

// ---- Case 3: slide builds entering, drops leaving, reuses overlap -----------

TEST_CASE("slide builds entering lines and drops leaving ones") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    ThemeService theme;
    ColorizerDisplayConfig display;

    const std::string raw = numbered_lines(100);
    std::vector<int> starts;
    std::shared_ptr<MaterializeCtx> ctx;
    GridGeometry geo;
    auto doc = layout_grid_skeleton(factory.Get(), raw, theme, false, 800.0f,
                                    display, &starts, &ctx, &geo);
    REQUIRE(doc.grid_line_count == 100);
    REQUIRE(doc.blocks.empty());

    // slide to [0,20]: 21 blocks, all materialized, correct text + top.
    slide_grid_window(doc, geo, *ctx, raw, starts, 0, 20, empty_colors());
    REQUIRE(doc.blocks.size() == 21);
    CHECK(doc.first_block_line == 0);
    for (int i = 0; i <= 20; ++i) {
        const auto& b = doc.blocks[static_cast<size_t>(i)];
        REQUIRE(!b.text_runs.empty());
        CHECK(b.text_runs[0].layout != nullptr);
        CHECK(b.text_runs[0].text == L"line" + std::to_wstring(i));
        CHECK(b.rect.top == doctest::Approx(grid_line_top(geo, i)));
    }

    // slide to [40,60]: 21 fresh blocks, window moved, old lines gone.
    slide_grid_window(doc, geo, *ctx, raw, starts, 40, 60, empty_colors());
    REQUIRE(doc.blocks.size() == 21);
    CHECK(doc.first_block_line == 40);
    for (int i = 40; i <= 60; ++i) {
        const auto& b = doc.blocks[static_cast<size_t>(i - 40)];
        REQUIRE(!b.text_runs.empty());
        CHECK(b.text_runs[0].text == L"line" + std::to_wstring(i));
        CHECK(b.rect.top == doctest::Approx(grid_line_top(geo, i)));
    }

    // Capture the IDWriteTextLayout pointers for [50,60] before the overlap slide.
    IDWriteTextLayout* before[11];
    for (int i = 50; i <= 60; ++i)
        before[i - 50] = doc.blocks[static_cast<size_t>(i - 40)].text_runs[0].layout.Get();

    // slide to [50,70] (overlap [50,60]): those layouts MUST be reused (same
    // pointers); [40,49] dropped; [61,70] freshly built.
    slide_grid_window(doc, geo, *ctx, raw, starts, 50, 70, empty_colors());
    REQUIRE(doc.blocks.size() == 21);
    CHECK(doc.first_block_line == 50);
    for (int i = 50; i <= 60; ++i) {
        IDWriteTextLayout* now =
            doc.blocks[static_cast<size_t>(i - 50)].text_runs[0].layout.Get();
        CHECK(now == before[i - 50]);  // SAME pointer == reused (no rebuild)
    }
    for (int i = 61; i <= 70; ++i) {
        const auto& b = doc.blocks[static_cast<size_t>(i - 50)];
        REQUIRE(!b.text_runs.empty());
        CHECK(b.text_runs[0].layout != nullptr);
        CHECK(b.text_runs[0].text == L"line" + std::to_wstring(i));
    }

    // Enter-keep-enter: from [50,70], slide to [40,70].
    // Old window: [50,70] (21 blocks). New window: [40,70] (31 blocks).
    // Lines [50,70] are kept (pointer-equal); lines [40,49] enter at the front.
    // There is only ONE contiguous entering run ([40,49]), so colors_for fires once.

    // Capture pointers for [50..70] (currently doc.blocks[0..20]).
    IDWriteTextLayout* kept[21];
    for (int i = 50; i <= 70; ++i)
        kept[i - 50] = doc.blocks[static_cast<size_t>(i - 50)].text_runs[0].layout.Get();

    int enter_keep_enter_invocations = 0;
    ColorsForRange counting2 = [&](uint32_t, uint32_t) {
        ++enter_keep_enter_invocations;
        return ColorizeResult{};
    };

    slide_grid_window(doc, geo, *ctx, raw, starts, 40, 70, counting2);
    REQUIRE(doc.blocks.size() == 31);
    CHECK(doc.first_block_line == 40);

    // [50,70] must be pointer-equal to what we recorded (reused, not rebuilt).
    for (int i = 50; i <= 70; ++i) {
        IDWriteTextLayout* now =
            doc.blocks[static_cast<size_t>(i - 40)].text_runs[0].layout.Get();
        CHECK(now == kept[i - 50]);
    }
    // [40,49] must be freshly built (non-null, valid text).
    for (int i = 40; i < 50; ++i) {
        const auto& b = doc.blocks[static_cast<size_t>(i - 40)];
        REQUIRE(!b.text_runs.empty());
        CHECK(b.text_runs[0].layout != nullptr);
        CHECK(b.text_runs[0].text == L"line" + std::to_wstring(i));
    }
    // Only one contiguous entering run ([40,49]) -> exactly 1 colors_for call.
    CHECK(enter_keep_enter_invocations == 1);
}

// ---- Case 4: colors_for invoked once per contiguous entering run ------------

TEST_CASE("slide colors entering ranges via colors_for, once per contiguous run") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    ThemeService theme;
    ColorizerDisplayConfig display;

    const std::string raw = numbered_lines(100);
    std::vector<int> starts;
    std::shared_ptr<MaterializeCtx> ctx;
    GridGeometry geo;
    auto doc = layout_grid_skeleton(factory.Get(), raw, theme, false, 800.0f,
                                    display, &starts, &ctx, &geo);
    REQUIRE(doc.grid_line_count == 100);

    // colors_for counts invocations and paints a known color over the "line"
    // word (4 bytes at the line start) of line 45 whenever its byte range is
    // requested.
    const uint32_t crafted = 0xABCDEF;
    int invocations = 0;
    const int byte45 = starts[45];
    ColorsForRange counting = [&](uint32_t lo, uint32_t hi) {
        ++invocations;
        ColorizeResult r;
        if (static_cast<uint32_t>(byte45) >= lo &&
            static_cast<uint32_t>(byte45) < hi) {
            ColorSpan s;
            s.start = static_cast<uint32_t>(byte45);
            s.length = 4;  // "line"
            s.color = crafted;
            r.spans.push_back(s);
        }
        return r;
    };

    // First slide [40,60]: one contiguous entering run -> exactly 1 invocation.
    invocations = 0;
    slide_grid_window(doc, geo, *ctx, raw, starts, 40, 60, counting);
    CHECK(invocations == 1);
    REQUIRE(doc.blocks.size() == 21);
    {
        const auto& b45 = doc.blocks[static_cast<size_t>(45 - 40)];
        REQUIRE(!b45.text_runs.empty());
        const auto& crs = b45.text_runs[0].color_ranges;
        bool found = false;
        for (const auto& cr : crs)
            if (cr.color == crafted) found = true;
        CHECK(found);
    }

    // slide [50,70]: [50,60] reused, only [61,70] enters -> 1 invocation.
    invocations = 0;
    slide_grid_window(doc, geo, *ctx, raw, starts, 50, 70, counting);
    CHECK(invocations == 1);
}

// ---- Case 5: edge cases -----------------------------------------------------

TEST_CASE("slide edges: empty file, single huge line, window past EOF, empty window") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    ThemeService theme;
    ColorizerDisplayConfig display;

    // Empty source -> one empty line.
    {
        std::vector<int> starts;
        std::shared_ptr<MaterializeCtx> ctx;
        GridGeometry geo;
        auto doc = layout_grid_skeleton(factory.Get(), "", theme, false, 800.0f,
                                        display, &starts, &ctx, &geo);
        REQUIRE(doc.grid_line_count == 1);
        REQUIRE(starts.size() == 1);
        CHECK(starts[0] == 0);
        slide_grid_window(doc, geo, *ctx, "", starts, 0, 0, empty_colors());
        REQUIRE(doc.blocks.size() == 1);
        REQUIRE(!doc.blocks[0].text_runs.empty());
        CHECK(doc.blocks[0].text_runs[0].layout != nullptr);  // placeholder layout
        CHECK(doc.blocks[0].text_runs[0].text.empty());        // empty line text
    }

    // Single 50k-char line -> slide [0,0] builds it (no crash, layout non-null).
    {
        std::string huge(50000, 'x');
        std::vector<int> starts;
        std::shared_ptr<MaterializeCtx> ctx;
        GridGeometry geo;
        auto doc = layout_grid_skeleton(factory.Get(), huge, theme, false, 800.0f,
                                        display, &starts, &ctx, &geo);
        REQUIRE(doc.grid_line_count == 1);
        slide_grid_window(doc, geo, *ctx, huge, starts, 0, 0, empty_colors());
        REQUIRE(doc.blocks.size() == 1);
        REQUIRE(!doc.blocks[0].text_runs.empty());
        CHECK(doc.blocks[0].text_runs[0].layout != nullptr);
        CHECK(doc.blocks[0].text_runs[0].text.size() == 50000);
    }

    // Empty window (first > last): clears blocks.
    {
        const std::string raw = numbered_lines(10);
        std::vector<int> starts;
        std::shared_ptr<MaterializeCtx> ctx;
        GridGeometry geo;
        auto doc = layout_grid_skeleton(factory.Get(), raw, theme, false, 800.0f,
                                        display, &starts, &ctx, &geo);
        // First fill a window, then collapse it.
        slide_grid_window(doc, geo, *ctx, raw, starts, 0, 5, empty_colors());
        REQUIRE(doc.blocks.size() == 6);
        slide_grid_window(doc, geo, *ctx, raw, starts, 5, 4, empty_colors());  // first>last
        CHECK(doc.blocks.empty());
    }

    // Window clamped at the last line: slide to the final line only.
    {
        const std::string raw = numbered_lines(10);
        std::vector<int> starts;
        std::shared_ptr<MaterializeCtx> ctx;
        GridGeometry geo;
        auto doc = layout_grid_skeleton(factory.Get(), raw, theme, false, 800.0f,
                                        display, &starts, &ctx, &geo);
        slide_grid_window(doc, geo, *ctx, raw, starts, 9, 9, empty_colors());
        REQUIRE(doc.blocks.size() == 1);
        CHECK(doc.first_block_line == 9);
        CHECK(doc.blocks[0].text_runs[0].text == L"line9");

        // Genuinely past EOF: the API clamps rather than indexing OOB.
        slide_grid_window(doc, geo, *ctx, raw, starts, 9, 50, empty_colors());
        REQUIRE(doc.blocks.size() == 1);              // clamped to last line
        CHECK(doc.blocks[0].text_runs[0].text == L"line9");
        slide_grid_window(doc, geo, *ctx, raw, starts, 20, 50, empty_colors());
        CHECK(doc.blocks.empty());                    // first past EOF -> empty
    }
}

// ---- Case 6: decoration parity with the old lazy materialize path -----------

TEST_CASE("decoration parity: URL + tabs line materializes identically to the old lazy path") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    ThemeService theme;
    ColorizerDisplayConfig display;
    // Exercise every decoration: whitespace markers (All) + indent guides +
    // trailing-ws highlight. The line has a URL, a tab, and trailing spaces.
    display.show_whitespace =
        wlx::plugin_colorizer::layout::ShowWhitespace::All;
    display.show_indent_guides = true;
    display.highlight_trailing = true;

    const std::string raw = "see https://example.com\tend   ";

    // OLD path: layout_source lazy skeleton, then materialize block 0.
    std::vector<int> old_starts;
    ColorizeResult empty;
    auto old_doc = layout_source(factory.Get(), raw, empty, theme, false, 800.0f,
                                 display, /*timings=*/nullptr, &old_starts);
    REQUIRE(old_doc.blocks.size() == 1);
    REQUIRE(old_doc.materialize_block);
    old_doc.materialize_block(old_doc.blocks[0], 0);
    const auto& old_run = old_doc.blocks[0].text_runs[0];

    // NEW path: build_grid_line with empty spans.
    std::vector<int> new_starts;
    std::shared_ptr<MaterializeCtx> ctx;
    GridGeometry geo;
    auto new_doc = layout_grid_skeleton(factory.Get(), raw, theme, false, 800.0f,
                                        display, &new_starts, &ctx, &geo);
    REQUIRE(new_doc.grid_line_count == 1);
    std::vector<PerLineSpan> no_spans;
    LayoutBlock lb =
        build_grid_line(0, geo, *ctx, raw, new_starts, no_spans);
    const auto& new_run = lb.text_runs[0];

    // run.text equal (decode + tab-expand identical).
    CHECK(old_run.text == new_run.text);

    // color_ranges count equal, and the URL range (link color + MOD_UNDERLINE)
    // present in both with the same start/length.
    REQUIRE(old_run.color_ranges.size() == new_run.color_ranges.size());
    auto find_url = [](const wlx::runtime::layout::TextRun& run, uint32_t link) {
        for (const auto& cr : run.color_ranges)
            if (cr.color == link && (cr.modifiers & MOD_UNDERLINE)) return &cr;
        return static_cast<const wlx::runtime::layout::ColorRange*>(nullptr);
    };
    const auto* old_url = find_url(old_run, ctx->link_color);
    const auto* new_url = find_url(new_run, ctx->link_color);
    REQUIRE(old_url != nullptr);
    REQUIRE(new_url != nullptr);
    CHECK(old_url->start == new_url->start);
    CHECK(old_url->length == new_url->length);

    // InteractiveSpan (URL hit rects) count equal.
    CHECK(old_doc.blocks[0].spans.size() == lb.spans.size());
    CHECK(!lb.spans.empty());
    CHECK(lb.spans[0].target.kind == LinkKind::ExternalUrl);

    // Whitespace markers count equal (and non-empty: there are spaces + a tab).
    CHECK(old_doc.blocks[0].ws_markers.size() == lb.ws_markers.size());
    CHECK(!lb.ws_markers.empty());

    // Trailing-ws highlight present in both.
    CHECK(old_doc.blocks[0].has_trailing_ws == lb.has_trailing_ws);
    CHECK(lb.has_trailing_ws);
}
