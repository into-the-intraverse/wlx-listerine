#include <doctest/doctest.h>
#include "scope_mapper.h"

TEST_CASE("ScopeMapper maps C comment to comment scope") {
    CHECK(ScopeMapper::map("c", "comment") == Scope::Comment);
}

TEST_CASE("ScopeMapper maps C string_literal to string scope") {
    CHECK(ScopeMapper::map("c", "string_literal") == Scope::String);
}

TEST_CASE("ScopeMapper maps C preproc_include to preprocessor scope") {
    CHECK(ScopeMapper::map("c", "preproc_include") == Scope::Preprocessor);
}

TEST_CASE("ScopeMapper maps C identifier to variable") {
    CHECK(ScopeMapper::map("c", "identifier") == Scope::Variable);
}

TEST_CASE("ScopeMapper unknown node returns plain") {
    CHECK(ScopeMapper::map("c", "totally_unknown_node_xyz") == Scope::Plain);
}

TEST_CASE("ScopeMapper unknown language returns plain for keywords") {
    CHECK(ScopeMapper::map("unknown_lang", "if") == Scope::Plain);
}

TEST_CASE("ScopeMapper maps JSON string to string scope") {
    CHECK(ScopeMapper::map("json", "string") == Scope::String);
}

TEST_CASE("ScopeMapper maps Python def to keyword") {
    CHECK(ScopeMapper::map("python", "def") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps C int to keyword2") {
    CHECK(ScopeMapper::map("c", "int") == Scope::Keyword2);
}

TEST_CASE("scope_to_color returns correct color for keyword in light mode") {
    SyntaxPalette pal = SyntaxPalette::defaults(false);
    CHECK(scope_to_color(Scope::Keyword, pal) == pal.keyword);
}

TEST_CASE("scope_to_color returns correct color for comment in dark mode") {
    SyntaxPalette pal = SyntaxPalette::defaults(true);
    CHECK(scope_to_color(Scope::Comment, pal) == pal.comment);
}
