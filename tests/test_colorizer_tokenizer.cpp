#include <doctest/doctest.h>
#include "tokenizer.h"

TEST_CASE("TokenSpan default construction") {
    TokenSpan span;
    CHECK(span.start == 0);
    CHECK(span.length == 0);
    CHECK(span.node_type.empty());
}

TEST_CASE("Tokenizer returns empty for null grammar") {
    auto spans = Tokenizer::tokenize(nullptr, "int x = 1;");
    CHECK(spans.empty());
}

TEST_CASE("Tokenizer returns empty for empty source") {
    auto spans = Tokenizer::tokenize(nullptr, "");
    CHECK(spans.empty());
}
