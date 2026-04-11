#include <doctest/doctest.h>
#include <filesystem>
#include "tokenizer.h"
#include "grammar_registry.h"

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

TEST_CASE("Tokenizer produces spans for C code"
    * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto* grammar = reg.get_grammar("c");
    REQUIRE(grammar != nullptr);

    auto spans = Tokenizer::tokenize(grammar, "// hello\nint x = 42;");
    CHECK_FALSE(spans.empty());

    bool found_comment = false;
    for (auto& s : spans) {
        if (s.node_type == "comment") {
            found_comment = true;
            CHECK(s.start == 0);
            CHECK(s.length == 8);
        }
    }
    CHECK(found_comment);
}

TEST_CASE("Tokenizer spans are sorted by start offset"
    * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto* grammar = reg.get_grammar("c");
    REQUIRE(grammar != nullptr);

    auto spans = Tokenizer::tokenize(grammar, "int x = 1; // comment\nfloat y = 2.0;");
    CHECK_FALSE(spans.empty());

    for (size_t i = 1; i < spans.size(); i++) {
        CHECK(spans[i].start >= spans[i - 1].start);
    }
}

TEST_CASE("Tokenizer produces spans for JSON"
    * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-json.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto* grammar = reg.get_grammar("json");
    REQUIRE(grammar != nullptr);

    auto spans = Tokenizer::tokenize(grammar, R"({"key": 42})");
    CHECK_FALSE(spans.empty());
}

TEST_CASE("Tokenizer produces spans for Python"
    * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-python.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto* grammar = reg.get_grammar("python");
    REQUIRE(grammar != nullptr);

    auto spans = Tokenizer::tokenize(grammar, "# comment\ndef foo():\n    return 42\n");
    CHECK_FALSE(spans.empty());
}
