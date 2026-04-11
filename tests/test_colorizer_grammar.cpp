#include <doctest/doctest.h>
#include <filesystem>
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

TEST_CASE("GrammarRegistry discovers grammar DLLs"
    * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    CHECK(reg.supports("c"));
    CHECK(reg.supports("json"));
}

TEST_CASE("GrammarRegistry loads C grammar"
    * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto* lang = reg.get_grammar("c");
    REQUIRE(lang != nullptr);
}

TEST_CASE("GrammarRegistry loads JSON grammar"
    * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-json.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto* lang = reg.get_grammar("json");
    REQUIRE(lang != nullptr);
}

TEST_CASE("GrammarRegistry loads Python grammar"
    * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-python.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto* lang = reg.get_grammar("python");
    REQUIRE(lang != nullptr);
}

TEST_CASE("GrammarRegistry available_languages lists all discovered grammars"
    * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto langs = reg.available_languages();
    CHECK_FALSE(langs.empty());
    // Result is sorted
    for (size_t i = 1; i < langs.size(); i++) {
        CHECK(langs[i] > langs[i - 1]);
    }
}
