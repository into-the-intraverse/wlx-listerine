#include <doctest/doctest.h>
#include "runtime/search/search_index.h"
#include "runtime/layout/layout_block.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/text_run.h"

using namespace wlx::runtime::layout;
using namespace wlx::runtime::search;

static LayoutDocument make_layout(std::initializer_list<std::wstring> block_texts) {
    LayoutDocument doc;
    for (auto& t : block_texts) {
        LayoutBlock b;
        TextRun r;
        r.text = t;
        b.text_runs.push_back(std::move(r));
        doc.blocks.push_back(std::move(b));
    }
    return doc;
}

TEST_CASE("SearchIndex finds a single match in one block") {
    auto doc = make_layout({L"hello world"});
    SearchIndex idx;
    idx.build(doc);

    SearchQuery q;
    q.needle = L"world";

    auto matches = idx.find_all(q);
    REQUIRE(matches.size() == 1);
    CHECK(matches[0].block_index == 0);
    CHECK(matches[0].char_start == 6);
    CHECK(matches[0].char_end == 11);
}

TEST_CASE("SearchIndex empty document returns no matches") {
    LayoutDocument doc;
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"x";
    CHECK(idx.find_all(q).empty());
    CHECK(idx.empty());
}

TEST_CASE("SearchIndex empty needle returns no matches") {
    auto doc = make_layout({L"hello"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    CHECK(idx.find_all(q).empty());
}

TEST_CASE("SearchIndex needle longer than haystack returns no matches") {
    auto doc = make_layout({L"hi"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"hello";
    CHECK(idx.find_all(q).empty());
}

TEST_CASE("SearchIndex case-insensitive by default") {
    auto doc = make_layout({L"Hello World"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"hello";
    auto m = idx.find_all(q);
    REQUIRE(m.size() == 1);
    CHECK(m[0].char_start == 0);
    CHECK(m[0].char_end == 5);
}

TEST_CASE("SearchIndex case-sensitive when match_case") {
    auto doc = make_layout({L"Hello World"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"hello";
    q.match_case = true;
    CHECK(idx.find_all(q).empty());

    q.needle = L"Hello";
    auto m = idx.find_all(q);
    REQUIRE(m.size() == 1);
    CHECK(m[0].char_start == 0);
}

TEST_CASE("SearchIndex whole_words matches only at word boundaries") {
    auto doc = make_layout({L"cat catalogue cat_1 cats cat."});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"cat";
    q.whole_words = true;
    auto m = idx.find_all(q);
    REQUIRE(m.size() == 2);
    CHECK(m[0].char_start == 0);
    CHECK(m[1].char_start == 25);
}

TEST_CASE("SearchIndex returns document-order matches across blocks") {
    auto doc = make_layout({L"foo bar", L"baz foo qux", L"foo"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"foo";
    auto m = idx.find_all(q);
    REQUIRE(m.size() == 3);
    CHECK(m[0].block_index == 0);
    CHECK(m[0].char_start == 0);
    CHECK(m[1].block_index == 1);
    CHECK(m[1].char_start == 4);
    CHECK(m[2].block_index == 2);
    CHECK(m[2].char_start == 0);
}

TEST_CASE("SearchIndex never matches across block boundaries") {
    auto doc = make_layout({L"foo", L"bar"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"oo\nba";
    CHECK(idx.find_all(q).empty());
}

TEST_CASE("SearchIndex non-overlapping matches") {
    auto doc = make_layout({L"aaaa"});
    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"aa";
    auto m = idx.find_all(q);
    REQUIRE(m.size() == 2);
    CHECK(m[0].char_start == 0);
    CHECK(m[1].char_start == 2);
}

TEST_CASE("SearchIndex empty text_runs block doesn't shift offsets") {
    LayoutDocument doc;
    doc.blocks.resize(3);
    TextRun r0; r0.text = L"first";
    doc.blocks[0].text_runs.push_back(r0);
    TextRun r2; r2.text = L"third";
    doc.blocks[2].text_runs.push_back(r2);

    SearchIndex idx;
    idx.build(doc);
    SearchQuery q;
    q.needle = L"third";
    auto m = idx.find_all(q);
    REQUIRE(m.size() == 1);
    CHECK(m[0].block_index == 2);
    CHECK(m[0].char_start == 0);
}
