#include <doctest/doctest.h>
#include "runtime/search/search_ops.h"
#include "runtime/search/search_index.h"
#include "runtime/layout/layout_block.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/text_run.h"

using namespace wlx::runtime::layout;
using namespace wlx::runtime::search;

namespace {

struct FakeV {
    LayoutDocument layout_store;
    LayoutDocument* layout = &layout_store;
    SearchIndex search_index;
    std::vector<SearchMatch> matches;
    int current_match = -1;
    SearchQuery last_query;
    bool index_dirty = true;
};

static_assert(SearchState<FakeV>);

static FakeV make_fake(std::initializer_list<std::wstring> blocks) {
    FakeV v;
    for (auto& t : blocks) {
        LayoutBlock b;
        TextRun r;
        r.text = t;
        b.text_runs.push_back(std::move(r));
        v.layout_store.blocks.push_back(std::move(b));
    }
    return v;
}

} // namespace

TEST_CASE("search_step findfirst on three-match doc lands on match 0") {
    auto v = make_fake({L"foo bar foo", L"foo baz"});
    SearchQuery q; q.needle = L"foo";
    auto r = search_step(v, q, /*findfirst=*/true);
    CHECK(r.has_match);
    CHECK(r.cursor == 0);
    CHECK(v.matches.size() == 3);
    CHECK(r.index_was_rebuilt);
}

TEST_CASE("search_step forward wraps after last match") {
    auto v = make_fake({L"foo foo foo"});
    SearchQuery q; q.needle = L"foo";
    search_step(v, q, /*findfirst=*/true);          // cursor 0
    search_step(v, q, false);                       // cursor 1
    search_step(v, q, false);                       // cursor 2
    auto r = search_step(v, q, false);              // wraps to 0
    CHECK(r.cursor == 0);
}

TEST_CASE("search_step backwards from cursor 0 wraps to n-1") {
    auto v = make_fake({L"foo foo foo"});
    SearchQuery q; q.needle = L"foo";
    search_step(v, q, /*findfirst=*/true);          // cursor 0
    q.backwards = true;
    auto r = search_step(v, q, false);
    CHECK(r.cursor == 2);
}

TEST_CASE("search_step no-match returns has_match=false and clears cursor") {
    auto v = make_fake({L"hello"});
    SearchQuery q; q.needle = L"xyz";
    auto r = search_step(v, q, /*findfirst=*/true);
    CHECK_FALSE(r.has_match);
    CHECK(r.cursor == -1);
    CHECK(v.matches.empty());
    CHECK(v.current_match == -1);
}

TEST_CASE("search_step clamps cursor after relayout shrinks match set") {
    auto v = make_fake({L"foo foo foo"});
    SearchQuery q; q.needle = L"foo";
    search_step(v, q, /*findfirst=*/true);
    search_step(v, q, false);                       // cursor 1
    search_step(v, q, false);                       // cursor 2
    // Simulate relayout: replace layout with fewer matches, mark dirty.
    v.layout_store.blocks.clear();
    LayoutBlock b; TextRun r; r.text = L"foo"; b.text_runs.push_back(r);
    v.layout_store.blocks.push_back(std::move(b));
    v.index_dirty = true;
    auto step = search_step(v, q, /*findfirst=*/false);
    CHECK(step.has_match);
    CHECK(v.matches.size() == 1);
    // Prior cursor=2 was clamped to 0 (matches.size()-1), then advance wraps to 0.
    CHECK(step.cursor == 0);
}

TEST_CASE("search_step query change without findfirst still requeries") {
    auto v = make_fake({L"foo bar"});
    SearchQuery q1; q1.needle = L"foo";
    search_step(v, q1, /*findfirst=*/true);
    SearchQuery q2; q2.needle = L"bar";
    auto r = search_step(v, q2, /*findfirst=*/false);
    CHECK(r.has_match);
    CHECK(v.matches.size() == 1);
    CHECK(r.cursor == 0);
}

TEST_CASE("search_step same query on non-dirty index does not rebuild") {
    auto v = make_fake({L"foo foo"});
    SearchQuery q; q.needle = L"foo";
    search_step(v, q, /*findfirst=*/true);
    auto r = search_step(v, q, /*findfirst=*/false);
    CHECK_FALSE(r.index_was_rebuilt);
}
