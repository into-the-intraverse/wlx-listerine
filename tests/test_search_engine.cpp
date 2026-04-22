#include <doctest/doctest.h>
#include "search_engine.h"
#include "layout_engine.h"

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
