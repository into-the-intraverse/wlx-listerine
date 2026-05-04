#include <doctest/doctest.h>
#include <filesystem>
#include "core_dll/grammar/grammar_registry.h"

static bool has_c_grammar() {
    return std::filesystem::exists("grammars/c/tree-sitter-c.dll");
}

TEST_CASE("GrammarRegistry with nonexistent dir has no languages") {
    GrammarRegistry reg(L"nonexistent_grammar_dir");
    CHECK(reg.available_languages().empty());
    CHECK_FALSE(reg.supports("c"));
}

TEST_CASE("GrammarRegistry get_grammar for unsupported language returns nullptr") {
    GrammarRegistry reg(L"nonexistent_grammar_dir");
    CHECK(reg.get_grammar("c") == nullptr);
}

TEST_CASE("GrammarRegistry get_query for unsupported language returns nullptr") {
    GrammarRegistry reg(L"nonexistent_grammar_dir");
    CHECK(reg.get_query("c") == nullptr);
}

TEST_CASE("GrammarRegistry discovers subdirectory grammars"
    * doctest::skip(!has_c_grammar())) {
    GrammarRegistry reg(L"grammars");
    CHECK(reg.supports("c"));
    CHECK(reg.supports("json"));
    CHECK(reg.supports("python"));
}

TEST_CASE("GrammarRegistry loads C grammar from subdirectory"
    * doctest::skip(!has_c_grammar())) {
    GrammarRegistry reg(L"grammars");
    auto* lang = reg.get_grammar("c");
    REQUIRE(lang != nullptr);
}

TEST_CASE("GrammarRegistry compiles query from highlights.scm"
    * doctest::skip(!has_c_grammar())) {
    GrammarRegistry reg(L"grammars");
    auto* query = reg.get_query("c");
    REQUIRE(query != nullptr);
}

TEST_CASE("GrammarRegistry parse produces a tree"
    * doctest::skip(!has_c_grammar())) {
    GrammarRegistry reg(L"grammars");
    auto* tree = reg.parse("c", "int main() { return 0; }");
    REQUIRE(tree != nullptr);
    ts_tree_delete(tree);
}

TEST_CASE("GrammarRegistry parse returns nullptr for unsupported language") {
    GrammarRegistry reg(L"nonexistent_grammar_dir");
    auto* tree = reg.parse("c", "int main() {}");
    CHECK(tree == nullptr);
}

TEST_CASE("GrammarRegistry available_languages is sorted"
    * doctest::skip(!has_c_grammar())) {
    GrammarRegistry reg(L"grammars");
    auto langs = reg.available_languages();
    REQUIRE(langs.size() >= 3);
    for (size_t i = 1; i < langs.size(); i++) {
        CHECK(langs[i] > langs[i - 1]);
    }
}
