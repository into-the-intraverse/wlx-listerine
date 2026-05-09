#include <doctest/doctest.h>
#include "runtime/host/context_menu.h"
#include "runtime/interaction/interaction_engine.h"
#include "runtime/layout/layout_block.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/text_position.h"

#include <algorithm>
#include <memory>

using namespace wlx::runtime::host;
using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;

namespace {

// Minimal fake md view for build_md_menu_context.
struct FakeMdView {
    std::shared_ptr<LayoutDocument> layout;
    std::unique_ptr<wlx::runtime::interaction::InteractionEngine> interaction;
    TextPosition sel_anchor;
    TextPosition sel_active;
};

// Minimal fake colorizer view for build_colorizer_menu_context.
struct FakeColorizerView {
    std::shared_ptr<LayoutDocument> layout;
    std::unique_ptr<wlx::runtime::interaction::InteractionEngine> interaction;
    TextPosition sel_anchor;
    TextPosition sel_active;
    std::string force_grammar_id;
};

}  // namespace

// ---- MenuResult::Kind ID stability ----
TEST_CASE("MenuResult::Kind enum values are stable") {
    CHECK(static_cast<int>(MenuResult::None)             == 0);
    CHECK(static_cast<int>(MenuResult::Copy)             == 1);
    CHECK(static_cast<int>(MenuResult::SelectAll)        == 2);
    CHECK(static_cast<int>(MenuResult::SearchGoogle)     == 3);
    CHECK(static_cast<int>(MenuResult::OpenLink)         == 4);
    CHECK(static_cast<int>(MenuResult::CopyLinkAddress)  == 5);
    CHECK(static_cast<int>(MenuResult::CopyCodeBlock)    == 6);
    CHECK(static_cast<int>(MenuResult::EditConfig)       == 7);
    CHECK(static_cast<int>(MenuResult::SetLanguage)      == 8);
}

// ---- build_menu_items: show/hide/enable rules ----
TEST_CASE("build_menu_items: empty context shows the always-on items") {
    MenuContext ctx;
    auto items = build_menu_items(ctx);
    // Expect: Copy(disabled), SelectAll, separator, SearchGoogle(disabled).
    REQUIRE(items.size() == 4);
    CHECK(items[0].kind == MenuItemKind::Copy);
    CHECK(items[0].enabled == false);
    CHECK(items[1].kind == MenuItemKind::SelectAll);
    CHECK(items[1].enabled == true);
    CHECK(items[2].kind == MenuItemKind::Separator);
    CHECK(items[3].kind == MenuItemKind::SearchGoogle);
    CHECK(items[3].enabled == false);
}

TEST_CASE("build_menu_items: with selection enables Copy and SearchGoogle") {
    MenuContext ctx;
    ctx.has_selection = true;
    auto items = build_menu_items(ctx);
    REQUIRE(items.size() == 4);
    CHECK(items[0].kind == MenuItemKind::Copy);
    CHECK(items[0].enabled == true);
    CHECK(items[3].kind == MenuItemKind::SearchGoogle);
    CHECK(items[3].enabled == true);
}

TEST_CASE("build_menu_items: link adds OpenLink + CopyLinkAddress under separator") {
    MenuContext ctx;
    ctx.link.present = true;
    ctx.link.url     = L"https://example.com";
    auto items = build_menu_items(ctx);
    // Copy, SelectAll, Sep, SearchGoogle, Sep, OpenLink, CopyLinkAddress
    REQUIRE(items.size() == 7);
    CHECK(items[4].kind == MenuItemKind::Separator);
    CHECK(items[5].kind == MenuItemKind::OpenLink);
    CHECK(items[6].kind == MenuItemKind::CopyLinkAddress);
}

TEST_CASE("build_menu_items: code block adds CopyCodeBlock under separator") {
    MenuContext ctx;
    ctx.code_block.present     = true;
    ctx.code_block.block_index = 3;
    auto items = build_menu_items(ctx);
    // Copy, SelectAll, Sep, SearchGoogle, Sep, CopyCodeBlock
    REQUIRE(items.size() == 6);
    CHECK(items[4].kind == MenuItemKind::Separator);
    CHECK(items[5].kind == MenuItemKind::CopyCodeBlock);
}

TEST_CASE("build_menu_items: config_path adds EditConfig under separator") {
    MenuContext ctx;
    ctx.config_path = L"C:\\path\\to\\plugin.toml";
    auto items = build_menu_items(ctx);
    // Copy, SelectAll, Sep, SearchGoogle, Sep, EditConfig
    REQUIRE(items.size() == 6);
    CHECK(items[4].kind == MenuItemKind::Separator);
    CHECK(items[5].kind == MenuItemKind::EditConfig);
}

TEST_CASE("build_menu_items: empty config_path hides EditConfig") {
    MenuContext ctx;
    auto items = build_menu_items(ctx);
    for (const auto& i : items) CHECK(i.kind != MenuItemKind::EditConfig);
}

TEST_CASE("build_menu_items: languages add Force Language root with separator before") {
    MenuContext ctx;
    ctx.config_path = L"C:\\plugin.toml";
    ctx.languages = { {"cpp", L"C++"}, {"python", L"Python"} };
    ctx.active_grammar_id = "cpp";
    auto items = build_menu_items(ctx);
    auto root_it = std::find_if(items.begin(), items.end(),
        [](const MenuItem& i) { return i.kind == MenuItemKind::LanguageSubmenuRoot; });
    REQUIRE(root_it != items.end());
    REQUIRE(root_it != items.begin());
    CHECK((root_it - 1)->kind == MenuItemKind::Separator);
}

TEST_CASE("build_menu_items: never emits leading or trailing separators") {
    MenuContext ctx;
    auto items = build_menu_items(ctx);
    REQUIRE(!items.empty());
    CHECK(items.front().kind != MenuItemKind::Separator);
    CHECK(items.back().kind != MenuItemKind::Separator);
}

TEST_CASE("build_menu_items: never emits two consecutive separators") {
    MenuContext ctx;
    ctx.has_selection = true;
    ctx.link.present  = true;
    ctx.code_block.present = true;
    ctx.config_path = L"x";
    ctx.languages = { {"cpp", L"C++"} };
    auto items = build_menu_items(ctx);
    for (size_t i = 1; i < items.size(); ++i) {
        bool both = items[i - 1].kind == MenuItemKind::Separator
                 && items[i].kind     == MenuItemKind::Separator;
        CHECK(both == false);
    }
}

// ---- build_md_menu_context against a synthetic FakeMdView ----
TEST_CASE("build_md_menu_context: empty layout yields empty context") {
    FakeMdView vs;
    auto ctx = build_md_menu_context(vs, 0.0f, 0.0f);
    CHECK(ctx.has_selection == false);
    CHECK(ctx.link.present == false);
    CHECK(ctx.code_block.present == false);
}

TEST_CASE("build_md_menu_context: detects selection") {
    FakeMdView vs;
    vs.layout = std::make_shared<LayoutDocument>();
    {
        LayoutBlock b;
        TextRun run;
        run.text = L"hello";
        b.text_runs.push_back(run);
        vs.layout->blocks.push_back(std::move(b));
    }
    vs.sel_anchor = TextPosition{0, 0};
    vs.sel_active = TextPosition{0, 5};
    auto ctx = build_md_menu_context(vs, 0.0f, 0.0f);
    CHECK(ctx.has_selection == true);
}

TEST_CASE("build_md_menu_context: detects code block under cursor") {
    FakeMdView vs;
    vs.layout = std::make_shared<LayoutDocument>();
    {
        LayoutBlock b;
        b.type = BlockType::CodeFence;
        b.rect = D2D1::RectF(0, 0, 100, 50);
        TextRun run;
        run.text = L"int x;";
        b.text_runs.push_back(run);
        vs.layout->blocks.push_back(std::move(b));
    }
    auto ctx = build_md_menu_context(vs, 10.0f, 10.0f);
    CHECK(ctx.code_block.present == true);
    CHECK(ctx.code_block.block_index == 0);
}

TEST_CASE("build_md_menu_context: cursor outside any code block leaves it absent") {
    FakeMdView vs;
    vs.layout = std::make_shared<LayoutDocument>();
    {
        LayoutBlock b;
        b.type = BlockType::CodeFence;
        b.rect = D2D1::RectF(0, 0, 100, 50);
        vs.layout->blocks.push_back(std::move(b));
    }
    auto ctx = build_md_menu_context(vs, 200.0f, 200.0f);
    CHECK(ctx.code_block.present == false);
}

// ---- build_colorizer_menu_context ----
TEST_CASE("build_colorizer_menu_context: marks auto-detect when no force_grammar_id") {
    FakeColorizerView vs;
    auto ctx = build_colorizer_menu_context(vs,
        std::vector<LanguageOption>{ {"cpp", L"C++"} }, 0.0f, 0.0f);
    CHECK(ctx.auto_detect_active == true);
    CHECK(ctx.active_grammar_id.empty());
    CHECK(ctx.languages.size() == 1);
}

TEST_CASE("build_colorizer_menu_context: forwards force_grammar_id to active") {
    FakeColorizerView vs;
    vs.force_grammar_id = "python";
    auto ctx = build_colorizer_menu_context(vs,
        std::vector<LanguageOption>{ {"cpp", L"C++"}, {"python", L"Python"} }, 0.0f, 0.0f);
    CHECK(ctx.auto_detect_active == false);
    CHECK(ctx.active_grammar_id == "python");
}
