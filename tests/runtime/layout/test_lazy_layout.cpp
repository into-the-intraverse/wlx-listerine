#include <doctest/doctest.h>
#include "runtime/layout/layout_engine.h"
#include "runtime/layout/line_index.h"
#include "runtime/layout/md_materialize.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

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

TEST_CASE("materialize_viewport builds the in-range blocks and reports change") {
    auto factory = dwf2();
    REQUIRE(factory);
    MarkdownParser p;
    const char* md = "# Heading\n\nFirst paragraph.\n\nSecond paragraph.\n\nThird paragraph.";
    auto doc = std::make_shared<Document>(p.parse(md, std::strlen(md)));
    ThemeService theme;
    LayoutEngine eng(factory.Get(), theme, false);
    auto lazy = eng.layout(*doc, 800.0f, /*wrap_code=*/false, /*gutter=*/0.0f, /*lazy=*/true);
    auto ctx = eng.take_md_ctx();
    REQUIRE(static_cast<bool>(ctx));  // a lazy layout must yield a materialize ctx
                                      // (cast: doctest can't stringify shared_ptr on MSVC)
    ctx->document = doc;          // keep inline ptrs alive
    REQUIRE(lazy.blocks.size() >= 2);

    // A huge viewport puts every block in range -> all materialize.
    bool changed = materialize_viewport(lazy, /*scroll_y=*/0.0f, /*viewport_h=*/100000.0f);
    CHECK(changed);
    for (auto& b : lazy.blocks)
        if (!b.text_runs.empty())
            CHECK(b.text_runs[0].layout != nullptr);
    CHECK(std::isfinite(lazy.total_height));
    CHECK(lazy.total_height > 0.0f);

    // Idempotent: a second call has nothing to (re)materialize.
    CHECK_FALSE(materialize_viewport(lazy, 0.0f, 100000.0f));
}

TEST_CASE("materialize_viewport shifts out-of-range blocks by the net delta") {
    auto factory = dwf2();
    REQUIRE(factory);
    MarkdownParser p;
    const char* md = "# Heading\n\npara one\n\npara two\n\npara three";
    auto doc = std::make_shared<Document>(p.parse(md, std::strlen(md)));
    ThemeService theme;
    LayoutEngine eng(factory.Get(), theme, false);
    auto lazy = eng.layout(*doc, 800.0f, false, 0.0f, /*lazy=*/true);
    auto ctx = eng.take_md_ctx();
    REQUIRE(static_cast<bool>(ctx));
    ctx->document = doc;
    REQUIRE(lazy.blocks.size() == 4);

    std::vector<float> old_tops;
    for (auto& b : lazy.blocks) old_tops.push_back(b.rect.top);
    float old_total = lazy.total_height;

    // A small viewport materializes only the leading block(s); the heading
    // estimate differs from its measured height, so a reflow must happen.
    bool changed = materialize_viewport(lazy, 0.0f, /*viewport_h=*/30.0f);
    REQUIRE(changed);

    // Every still-unmaterialized block is translated by exactly the net delta.
    float d = lazy.total_height - old_total;
    bool saw_unmaterialized = false;
    for (size_t j = 0; j < lazy.blocks.size(); ++j) {
        if (lazy.blocks[j].text_runs[0].layout) continue;  // materialized: height changed
        saw_unmaterialized = true;
        CHECK(lazy.blocks[j].rect.top == doctest::Approx(old_tops[j] + d));
    }
    CHECK(saw_unmaterialized);  // the viewport must not have covered everything

    // Tops stay ordered after the shift.
    for (size_t j = 1; j < lazy.blocks.size(); ++j)
        CHECK(lazy.blocks[j].rect.top >= lazy.blocks[j - 1].rect.top);
}

TEST_CASE("anchor of a heading inside a quote reflows (eager path sets block_index)") {
    auto factory = dwf2();
    REQUIRE(factory);
    MarkdownParser p;
    // The quoted heading takes the EAGER heading path inside a lazy document;
    // its anchor must still carry a block index so reflow can re-derive its Y.
    const char* md = "# Top\n\nsome paragraph text\n\n> ## Inner Heading\n\ntail";
    auto doc = std::make_shared<Document>(p.parse(md, std::strlen(md)));
    ThemeService theme;
    LayoutEngine eng(factory.Get(), theme, false);
    auto lazy = eng.layout(*doc, 800.0f, false, 0.0f, /*lazy=*/true);
    auto ctx = eng.take_md_ctx();
    REQUIRE(static_cast<bool>(ctx));
    ctx->document = doc;

    const AnchorEntry* inner = nullptr;
    for (auto& a : lazy.anchors)
        if (a.slug == L"inner-heading") inner = &a;
    REQUIRE(inner);
    REQUIRE(inner->block_index >= 0);
    CHECK(lazy.blocks[inner->block_index].type == BlockType::Heading);

    materialize_viewport(lazy, 0.0f, 100000.0f);

    // After reflow the anchor tracks its heading block's top exactly.
    CHECK(inner->y_offset ==
          doctest::Approx(lazy.blocks[inner->block_index].rect.top));
}

TEST_CASE("materialize_viewport is a no-op on an eager document") {
    auto factory = dwf2();
    REQUIRE(factory);
    MarkdownParser p;
    auto doc = std::make_shared<Document>(p.parse("Hello world", 11));
    ThemeService theme;
    LayoutEngine eng(factory.Get(), theme, false);
    auto eager = eng.layout(*doc, 800.0f);   // eager -> materialize_block == null
    CHECK(eager.materialize_block == nullptr);
    CHECK_FALSE(materialize_viewport(eager, 0.0f, 1000.0f));
}

TEST_CASE("md eviction: far-off materialized blocks drop layouts but keep exact geometry") {
    // Build a lazy document tall enough for >5 "screens" (viewport_h = 50 DIPs).
    // Mix: paragraphs, a code fence, and a list item (eager, but InlineFixed-backed
    // since Stage-2b, so it evicts and re-materializes like the rest).
    auto factory = dwf2();
    REQUIRE(factory);
    MarkdownParser p;
    const char* md =
        "Paragraph one of many.\n\n"
        "Paragraph two of many.\n\n"
        "Paragraph three of many.\n\n"
        "Paragraph four of many.\n\n"
        "Paragraph five of many.\n\n"
        "Paragraph six of many.\n\n"
        "Paragraph seven of many.\n\n"
        "Paragraph eight of many.\n\n"
        "- list item (eager, now InlineFixed-backed)\n\n"
        "```\nint x = 0;\nint y = 1;\n```\n\n"
        "Paragraph nine of many.\n\n"
        "Paragraph ten of many.";
    auto doc = std::make_shared<Document>(p.parse(md, std::strlen(md)));
    ThemeService theme;
    LayoutEngine eng(factory.Get(), theme, false);
    auto lazy = eng.layout(*doc, 800.0f, /*wrap_code=*/false, /*gutter=*/0.0f, /*lazy=*/true);
    auto ctx = eng.take_md_ctx();
    REQUIRE(static_cast<bool>(ctx));
    ctx->document = doc;

    const float vp_h = 50.0f;  // deliberately tiny so 2-screen zone is small

    // Step 1: materialize the top of the document.
    materialize_viewport(lazy, 0.0f, vp_h, &ctx->recipes);

    // Record which blocks got materialized and snapshot their geometry.
    struct BlockSnap {
        bool had_layout = false;
        D2D1_RECT_F block_rect = {};
        D2D1_RECT_F run_rect = {};
        std::wstring run_text;
    };
    std::vector<BlockSnap> snaps;
    snaps.reserve(lazy.blocks.size());
    for (auto& b : lazy.blocks) {
        BlockSnap s;
        s.had_layout = !b.text_runs.empty() && b.text_runs[0].layout != nullptr;
        s.block_rect = b.rect;
        if (!b.text_runs.empty()) {
            s.run_rect = b.text_runs[0].rect;
            s.run_text = b.text_runs[0].text;
        }
        snaps.push_back(s);
    }

    // Snapshot line_tops before the far-jump (eviction must not rebuild the index).
    std::vector<float> line_tops_before = lazy.line_tops;

    // Step 2: jump far — scroll to bottom so early blocks are far outside the
    // keep zone (> 2 * vp_h above the new viewport top).
    float far_scroll = lazy.total_height - vp_h;
    if (far_scroll < 0) far_scroll = 0;
    materialize_viewport(lazy, far_scroll, vp_h, &ctx->recipes);

    // Step 3: verify early recipe-backed blocks that had layouts now have them
    // evicted, but geometry and text are unchanged.
    bool saw_evicted_recipe = false;
    for (size_t bi = 0; bi < lazy.blocks.size(); ++bi) {
        auto& b = lazy.blocks[bi];
        if (!snaps[bi].had_layout) continue;  // was not materialized before -> skip

        bool recipe_backed = bi < ctx->recipes.size() &&
                             ctx->recipes[bi].kind != BlockRecipe::Kind::None;
        bool is_far = b.rect.bottom < (far_scroll - vp_h * kEvictScreens);
        if (!recipe_backed || !is_far) continue;

        // Recipe-backed and far away: must be evicted (paragraphs, code fence, and
        // — since Stage-2b — the list item all qualify).
        REQUIRE_FALSE(b.text_runs.empty());
        CHECK(b.text_runs[0].layout == nullptr);
        CHECK(b.text_runs[0].color_ranges.empty());
        CHECK(b.text_runs[0].code_bg_rects.empty());
        CHECK(b.spans.empty());
        CHECK(b.ws_markers.empty());
        CHECK_FALSE(b.has_trailing_ws);

        // Geometry and text MUST be unchanged.
        CHECK(b.rect.top    == doctest::Approx(snaps[bi].block_rect.top));
        CHECK(b.rect.bottom == doctest::Approx(snaps[bi].block_rect.bottom));
        CHECK(b.text_runs[0].rect.top    == doctest::Approx(snaps[bi].run_rect.top));
        CHECK(b.text_runs[0].rect.bottom == doctest::Approx(snaps[bi].run_rect.bottom));
        CHECK(b.text_runs[0].text == snaps[bi].run_text);

        saw_evicted_recipe = true;
    }
    CHECK(saw_evicted_recipe);    // at least one block must have been evicted

    // line_tops must be identical (eviction must not rebuild the index).
    REQUIRE(lazy.line_tops.size() == line_tops_before.size());
    for (size_t i = 0; i < line_tops_before.size(); ++i)
        CHECK(lazy.line_tops[i] == doctest::Approx(line_tops_before[i]));

    // Step 4: scroll back to top — evicted blocks must re-materialize with the
    // same geometry (recipe rebuild must reproduce the measured height exactly).
    materialize_viewport(lazy, 0.0f, vp_h, &ctx->recipes);

    for (size_t bi = 0; bi < lazy.blocks.size(); ++bi) {
        if (!snaps[bi].had_layout) continue;
        bool recipe_backed = bi < ctx->recipes.size() &&
                             ctx->recipes[bi].kind != BlockRecipe::Kind::None;
        if (!recipe_backed) continue;
        auto& b = lazy.blocks[bi];
        // Block must be materialized again after scrolling back into range.
        // (Only check blocks that scroll back into the viewport+overscan window.)
        float re_vp_top    = 0.0f;
        float re_vp_bottom = vp_h * 2.0f;
        if (b.rect.bottom < re_vp_top || b.rect.top > re_vp_bottom) continue;

        REQUIRE_FALSE(b.text_runs.empty());
        CHECK(b.text_runs[0].layout != nullptr);
        // Rects must be unchanged (re-materialization is idempotent).
        CHECK(b.rect.top    == doctest::Approx(snaps[bi].block_rect.top));
        CHECK(b.rect.bottom == doctest::Approx(snaps[bi].block_rect.bottom));
    }

    // Re-entry must reproduce the index byte-for-byte: recipe rebuild yields the
    // same measured heights, so no reflow and no fractional drift. Exact equality
    // (not Approx) — heights are deterministic, delta on re-entry is exactly 0.
    CHECK(lazy.line_tops == line_tops_before);
}

TEST_CASE("md eviction: nullptr recipes disables eviction entirely") {
    auto factory = dwf2();
    REQUIRE(factory);
    MarkdownParser p;
    const char* md =
        "Para A.\n\nPara B.\n\nPara C.\n\nPara D.\n\nPara E.\n\n"
        "Para F.\n\nPara G.\n\nPara H.\n\nPara I.\n\nPara J.";
    auto doc = std::make_shared<Document>(p.parse(md, std::strlen(md)));
    ThemeService theme;
    LayoutEngine eng(factory.Get(), theme, false);
    auto lazy = eng.layout(*doc, 800.0f, false, 0.0f, /*lazy=*/true);
    auto ctx = eng.take_md_ctx();
    REQUIRE(static_cast<bool>(ctx));
    ctx->document = doc;

    const float vp_h = 50.0f;

    // Materialize from the top WITHOUT recipes (no eviction).
    materialize_viewport(lazy, 0.0f, vp_h, /*recipes=*/nullptr);

    // Record which blocks were materialized.
    std::vector<bool> had_layout;
    for (auto& b : lazy.blocks)
        had_layout.push_back(!b.text_runs.empty() && b.text_runs[0].layout != nullptr);

    // Jump far without recipes — early materialized blocks must keep their layouts.
    float far_scroll = lazy.total_height - vp_h;
    if (far_scroll < 0) far_scroll = 0;
    materialize_viewport(lazy, far_scroll, vp_h, /*recipes=*/nullptr);

    for (size_t bi = 0; bi < lazy.blocks.size(); ++bi) {
        if (!had_layout[bi]) continue;
        REQUIRE_FALSE(lazy.blocks[bi].text_runs.empty());
        CHECK(lazy.blocks[bi].text_runs[0].layout != nullptr);  // no eviction: layout kept
    }
}

TEST_CASE("InlineFixed: evicted table cells re-materialize with alignment + geometry intact") {
    // Stage-2b: eager table cells get an InlineFixed recipe so they evict. The
    // re-materialized layout must reproduce the eager one byte-for-byte — most
    // critically the column ALIGNMENT (a bug the plain Inline recipe would drop).
    auto factory = dwf2();
    REQUIRE(factory);
    MarkdownParser p;
    std::string md;
    for (int i = 0; i < 40; ++i) md += "Filler paragraph number " + std::to_string(i) + ".\n\n";
    md += "| Left | Center | Right |\n|:-----|:------:|------:|\n| a | bb | ccc |\n| dd | e | f |\n\n";
    for (int i = 0; i < 40; ++i) md += "Trailing paragraph number " + std::to_string(i) + ".\n\n";
    auto doc = std::make_shared<Document>(p.parse(md.c_str(), md.size()));
    ThemeService theme;
    LayoutEngine eng(factory.Get(), theme, false);
    auto lazy = eng.layout(*doc, 800.0f, /*wrap_code=*/false, /*gutter=*/0.0f, /*lazy=*/true);
    auto ctx = eng.take_md_ctx();
    REQUIRE(static_cast<bool>(ctx));
    ctx->document = doc;

    // Materialize the whole document once so every block is at its final
    // (measured, reflowed) position — removes the estimate->measured confound.
    materialize_viewport(lazy, 0.0f, 1.0e6f, &ctx->recipes);

    struct CellSnap {
        size_t idx;
        DWRITE_TEXT_ALIGNMENT align;
        D2D1_RECT_F run_rect;
        float metric_w, metric_h;
        size_t spans;
    };
    std::vector<CellSnap> snaps;
    for (size_t i = 0; i < lazy.blocks.size(); ++i) {
        auto& b = lazy.blocks[i];
        if (b.type != BlockType::TableCell || b.text_runs.empty() || !b.text_runs[0].layout)
            continue;
        REQUIRE(i < ctx->recipes.size());
        CHECK(ctx->recipes[i].kind == BlockRecipe::Kind::InlineFixed);  // cells are evictable now
        DWRITE_TEXT_METRICS m{};
        b.text_runs[0].layout->GetMetrics(&m);
        snaps.push_back({i, b.text_runs[0].layout->GetTextAlignment(),
                         b.text_runs[0].rect, m.width, m.height, b.spans.size()});
    }
    REQUIRE(snaps.size() >= 6);  // 2 rows x 3 cols
    // The center and right columns must actually be non-default, or the test
    // would not exercise the alignment-preservation path.
    bool saw_center = false, saw_trailing = false;
    for (auto& s : snaps) {
        saw_center   |= (s.align == DWRITE_TEXT_ALIGNMENT_CENTER);
        saw_trailing |= (s.align == DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    CHECK(saw_center);
    CHECK(saw_trailing);

    // Evict: scroll to the very top with a tiny viewport so the mid-document
    // table falls far outside the keep zone.
    const float vp_h = 60.0f;
    materialize_viewport(lazy, 0.0f, vp_h, &ctx->recipes);
    for (auto& s : snaps)
        CHECK(lazy.blocks[s.idx].text_runs[0].layout == nullptr);  // evicted

    // Scroll back onto the table and re-materialize.
    float table_top = lazy.blocks[snaps.front().idx].rect.top;
    materialize_viewport(lazy, table_top - 20.0f, 400.0f, &ctx->recipes);

    for (auto& s : snaps) {
        auto& run = lazy.blocks[s.idx].text_runs[0];
        REQUIRE(run.layout != nullptr);  // re-materialized
        CHECK(run.layout->GetTextAlignment() == s.align);  // alignment preserved
        DWRITE_TEXT_METRICS m{};
        run.layout->GetMetrics(&m);
        CHECK(m.width  == doctest::Approx(s.metric_w));
        CHECK(m.height == doctest::Approx(s.metric_h));
        CHECK(run.rect.left   == doctest::Approx(s.run_rect.left));
        CHECK(run.rect.top    == doctest::Approx(s.run_rect.top));
        CHECK(run.rect.right  == doctest::Approx(s.run_rect.right));
        CHECK(run.rect.bottom == doctest::Approx(s.run_rect.bottom));
        CHECK(lazy.blocks[s.idx].spans.size() == s.spans);
    }
}
