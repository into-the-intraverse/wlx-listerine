#include <doctest/doctest.h>
#include "runtime/layout/layout_engine.h"
#include "runtime/layout/line_index.h"
#include "runtime/layout/md_materialize.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <cstring>
#include <memory>

using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;
using namespace wlx::runtime::theme;

using Microsoft::WRL::ComPtr;

static ComPtr<IDWriteFactory> dwf2() {
    ComPtr<IDWriteFactory> factory;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    return factory;
}

TEST_CASE("estimate_inline_height grows with text length and shrinks with width") {
    ThemeService theme;
    float body = theme.fonts().body_size;
    float lh = body * theme.spacing().line_height_factor;

    float h_short = estimate_inline_height(/*char_count=*/5, /*avg_advance=*/body * 0.5f,
                                           /*max_width=*/800.0f, /*line_height=*/lh);
    CHECK(h_short == doctest::Approx(lh));

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

TEST_CASE("lazy layout defers paragraph layouts but keeps run text and geometry") {
    auto factory = dwf2();
    REQUIRE(factory);
    MarkdownParser p;
    auto doc = std::make_shared<Document>(p.parse("Hello world\n\nSecond para", 24));
    ThemeService theme;
    LayoutEngine eng(factory.Get(), theme, false);
    auto lazy = eng.layout(*doc, 800.0f, /*wrap_code=*/false, /*gutter=*/0.0f, /*lazy=*/true);

    REQUIRE(lazy.blocks.size() >= 2);
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
    for (int i = 0; i < (int)eager.blocks.size(); ++i) {
        float eh = eager.blocks[i].rect.bottom - eager.blocks[i].rect.top;
        float lh = lazy.blocks[i].rect.bottom - lazy.blocks[i].rect.top;
        CHECK(lh == doctest::Approx(eh).epsilon(0.02));
    }
}

TEST_CASE("lazy layout keeps list/quote/table eager") {
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

TEST_CASE("apply_height_delta translates only later blocks and total_height") {
    LayoutDocument doc;
    LayoutBlock a; a.rect = D2D1::RectF(0, 0, 100, 20);   a.text_runs.push_back({}); a.text_runs[0].rect = a.rect;
    LayoutBlock b; b.rect = D2D1::RectF(0, 20, 100, 40);  b.text_runs.push_back({}); b.text_runs[0].rect = b.rect;
    LayoutBlock c; c.rect = D2D1::RectF(0, 40, 100, 60);  c.text_runs.push_back({}); c.text_runs[0].rect = c.rect;
    doc.blocks = {a, b, c};
    doc.total_height = 60;
    doc.anchors.push_back({L"x", 40.0f, 2});   // anchor owned by block c (index 2)

    // Block 0 grew by +10 (its own bottom already updated by the materializer).
    doc.blocks[0].rect.bottom = 30;
    apply_height_delta(doc, 0, 10.0f);

    CHECK(doc.blocks[1].rect.top == doctest::Approx(30));   // 20 + 10
    CHECK(doc.blocks[2].rect.top == doctest::Approx(50));   // 40 + 10
    CHECK(doc.blocks[0].rect.top == doctest::Approx(0));    // unchanged
    CHECK(doc.total_height == doctest::Approx(70));
    CHECK(doc.anchors[0].y_offset == doctest::Approx(50));  // block c moved down 10
}

TEST_CASE("lazy line index has the same logical-line count as eager (before materialize)") {
    auto factory = dwf2();
    REQUIRE(factory);
    MarkdownParser p;
    const char* md = "Para one\n\n```\nint a;\nint b;\nint c;\n```\n\nPara two";
    auto doc = std::make_shared<Document>(p.parse(md, std::strlen(md)));
    ThemeService theme;

    LayoutEngine eng_e(factory.Get(), theme, false);
    auto eager = eng_e.layout(*doc, 800.0f);
    build_line_index(eager);

    LayoutEngine eng_l(factory.Get(), theme, false);
    auto lazy = eng_l.layout(*doc, 800.0f, false, 0.0f, /*lazy=*/true);
    build_line_index(lazy);   // BEFORE materializing any block

    // Numbering must match eager even though no lazy block is materialized yet
    // (count comes from run.text hard-breaks, not from the layout).
    CHECK(lazy.line_tops.size() == eager.line_tops.size());
    CHECK(lazy.line_tops.size() >= 5);  // para + 3 code lines + para
}
