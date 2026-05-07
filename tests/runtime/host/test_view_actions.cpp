#include <doctest/doctest.h>
#include "runtime/host/view_actions.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/layout_block.h"

#include <memory>
#include <windows.h>

using namespace wlx::runtime::host;
using namespace wlx::runtime::layout;

namespace {

// Minimal fake view satisfying the Selectable concept the new helpers expect.
struct FakeView {
    std::shared_ptr<LayoutDocument> layout;
    TextPosition sel_anchor;
    TextPosition sel_active;
    bool selecting = false;
};

static std::shared_ptr<LayoutDocument> make_layout_with_blocks(
    std::vector<std::wstring> block_texts) {
    auto layout = std::make_shared<LayoutDocument>();
    for (auto& t : block_texts) {
        LayoutBlock b;
        TextRun run;
        run.text = t;
        b.text_runs.push_back(run);
        layout->blocks.push_back(std::move(b));
    }
    return layout;
}

}  // namespace

TEST_CASE("view_actions::select_all on empty layout returns false") {
    FakeView v;
    v.layout = std::make_shared<LayoutDocument>();
    CHECK(select_all(v) == false);
    CHECK(v.sel_anchor.valid() == false);
}

TEST_CASE("view_actions::select_all on null layout returns false") {
    FakeView v;
    CHECK(select_all(v) == false);
}

TEST_CASE("view_actions::select_all spans all blocks") {
    FakeView v;
    v.layout = make_layout_with_blocks({L"hello", L"world!!"});
    REQUIRE(select_all(v));
    CHECK(v.sel_anchor.block_index == 0);
    CHECK(v.sel_anchor.char_offset == 0);
    CHECK(v.sel_active.block_index == 1);
    CHECK(v.sel_active.char_offset == 7);
    CHECK(v.selecting == false);
}

TEST_CASE("view_actions::copy_selection returns false when no selection") {
    FakeView v;
    v.layout = make_layout_with_blocks({L"hello"});
    // sel_anchor == sel_active (both default-constructed)
    CHECK(copy_selection(v, /*hwnd*/ nullptr) == false);
}

TEST_CASE("view_actions::copy_selection returns false when no layout") {
    FakeView v;
    v.sel_anchor = TextPosition{0, 0};
    v.sel_active = TextPosition{0, 5};
    CHECK(copy_selection(v, /*hwnd*/ nullptr) == false);
}
