#include <doctest/doctest.h>
#include "grammar_registry.h"

TEST_CASE("GrammarRegistry with nonexistent dir has no languages") {
    GrammarRegistry reg(L"nonexistent_grammar_dir");
    CHECK(reg.available_languages().empty());
    CHECK_FALSE(reg.supports("c"));
}

TEST_CASE("GrammarRegistry get_grammar for unsupported language returns nullptr") {
    GrammarRegistry reg(L"nonexistent_grammar_dir");
    CHECK(reg.get_grammar("c") == nullptr);
}
