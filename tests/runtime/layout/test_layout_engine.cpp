#include <doctest/doctest.h>
#include "runtime/layout/layout_engine.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <string>

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

TEST_CASE("Empty document") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    Document doc;
    auto layout = do_layout(factory.Get(), doc);
    CHECK(layout.blocks.empty());
    CHECK(layout.total_height > 0); // content padding
}

TEST_CASE("Single paragraph") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("Hello world");
    auto layout = do_layout(factory.Get(), doc);

    REQUIRE(!layout.blocks.empty());
    CHECK(layout.blocks[0].type == BlockType::Paragraph);
    CHECK(layout.blocks[0].rect.right > layout.blocks[0].rect.left);
    CHECK(layout.blocks[0].rect.bottom > layout.blocks[0].rect.top);
    CHECK(!layout.blocks[0].text_runs.empty());
}

TEST_CASE("Heading produces anchor") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("# Hello World");
    auto layout = do_layout(factory.Get(), doc);

    REQUIRE(!layout.blocks.empty());
    CHECK(layout.blocks[0].type == BlockType::Heading);
    CHECK(layout.blocks[0].heading_level == 1);

    REQUIRE(!layout.anchors.empty());
    CHECK(layout.anchors[0].slug == L"hello-world");
}

TEST_CASE("Inline code background in a heading aligns to the bold glyphs") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    // Regression: headings bold the whole run. If the code-background rect is
    // measured before the bold is applied, the bold-widened prefix pushes the
    // code text right while the rect stays left of it. Use a long prefix so the
    // bold-vs-regular width gap is clearly more than 1px.
    auto doc = parse("## a long heading prefix `cd`");
    auto layout = do_layout(factory.Get(), doc);

    REQUIRE(!layout.blocks.empty());
    REQUIRE(layout.blocks[0].type == BlockType::Heading);
    auto& run = layout.blocks[0].text_runs[0];
    REQUIRE(run.layout);
    REQUIRE(!run.code_bg_rects.empty());

    // Where does the code ("cd") actually start in the final (bold) layout?
    size_t code_off = run.text.find(L"cd");
    REQUIRE(code_off != std::wstring::npos);
    float hx = 0, hy = 0;
    DWRITE_HIT_TEST_METRICS hm = {};
    run.layout->HitTestTextPosition(static_cast<UINT32>(code_off), FALSE, &hx, &hy, &hm);

    // The code-bg rect insets the hit by a 2px pad (see create_text_layout).
    // It must track the bold glyph position, not the pre-bold one.
    CHECK(std::abs(run.code_bg_rects[0].rect.left - (hx - 2.0f)) < 1.0f);
}

TEST_CASE("H1 and H2 have bottom rule") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("# H1\n\n## H2\n\n### H3");
    auto layout = do_layout(factory.Get(), doc);

    REQUIRE(layout.blocks.size() >= 3);
    CHECK(layout.blocks[0].has_bottom_rule == true);
    CHECK(layout.blocks[1].has_bottom_rule == true);
    CHECK(layout.blocks[2].has_bottom_rule == false);
}

TEST_CASE("Blocks are positioned sequentially") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("Para 1\n\nPara 2\n\nPara 3");
    auto layout = do_layout(factory.Get(), doc);

    REQUIRE(layout.blocks.size() >= 3);
    for (size_t i = 1; i < layout.blocks.size(); i++) {
        CHECK(layout.blocks[i].rect.top >= layout.blocks[i - 1].rect.bottom);
    }
}

TEST_CASE("Total height is positive") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("# Title\n\nSome text.\n\nMore text.");
    auto layout = do_layout(factory.Get(), doc);

    CHECK(layout.total_height > 0);
    CHECK(layout.viewport_width == 800.0f);
}

TEST_CASE("List items have bullets") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("- Item 1\n- Item 2");
    auto layout = do_layout(factory.Get(), doc);

    bool found_bullet = false;
    for (auto& block : layout.blocks) {
        if (block.type == BlockType::ListItem && !block.bullet_text.empty()) {
            found_bullet = true;
            break;
        }
    }
    CHECK(found_bullet);
}

TEST_CASE("Blockquote has left border") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("> quoted text");
    auto layout = do_layout(factory.Get(), doc);

    bool found_quote = false;
    for (auto& block : layout.blocks) {
        if (block.type == BlockType::BlockQuote) {
            CHECK(block.has_left_border);
            found_quote = true;
        }
    }
    CHECK(found_quote);
}

TEST_CASE("Horizontal rule has bottom rule") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("---");
    auto layout = do_layout(factory.Get(), doc);

    bool found_hr = false;
    for (auto& block : layout.blocks) {
        if (block.type == BlockType::HorizontalRule) {
            CHECK(block.has_bottom_rule);
            found_hr = true;
        }
    }
    CHECK(found_hr);
}

TEST_CASE("Code fence has background") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("```\ncode here\n```");
    auto layout = do_layout(factory.Get(), doc);

    bool found_code = false;
    for (auto& block : layout.blocks) {
        if (block.type == BlockType::CodeFence) {
            CHECK(block.has_background);
            found_code = true;
        }
    }
    CHECK(found_code);
}

TEST_CASE("Link produces interactive span") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("[click](https://example.com)");
    auto layout = do_layout(factory.Get(), doc);

    bool found_span = false;
    for (auto& block : layout.blocks) {
        if (!block.spans.empty()) {
            CHECK(block.spans[0].target.kind == LinkKind::ExternalUrl);
            found_span = true;
        }
    }
    CHECK(found_span);
}

TEST_CASE("Different viewport widths produce different layouts") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    auto doc = parse("This is a somewhat long paragraph that should wrap differently at different widths.");

    auto narrow = do_layout(factory.Get(), doc, 200.0f);
    auto wide = do_layout(factory.Get(), doc, 1200.0f);

    CHECK(narrow.viewport_width == 200.0f);
    CHECK(wide.viewport_width == 1200.0f);
    // Narrow layout should be taller due to more wrapping
    CHECK(narrow.total_height >= wide.total_height);
}

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
    // Span rect must lie inside the heading block rect — verifies the
    // local-to-document coordinate translation in layout_heading.
    CHECK(span.rect.left   >= blk.rect.left);
    CHECK(span.rect.right  <= blk.rect.right);
    CHECK(span.rect.top    >= blk.rect.top);
    CHECK(span.rect.bottom <= blk.rect.bottom);
}
