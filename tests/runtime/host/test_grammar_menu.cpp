#include <doctest/doctest.h>
#include "runtime/host/grammar_menu.h"

using namespace wlx::runtime::host;

TEST_CASE("grammar_display_name maps known ids to display names") {
    CHECK(grammar_display_name("cpp")        == L"C++");
    CHECK(grammar_display_name("c")          == L"C");
    CHECK(grammar_display_name("c-sharp")    == L"C#");
    CHECK(grammar_display_name("javascript") == L"JavaScript");
    CHECK(grammar_display_name("typescript") == L"TypeScript");
    CHECK(grammar_display_name("html")       == L"HTML");
    CHECK(grammar_display_name("css")        == L"CSS");
    CHECK(grammar_display_name("json")       == L"JSON");
    CHECK(grammar_display_name("toml")       == L"TOML");
    CHECK(grammar_display_name("yaml")       == L"YAML");
    CHECK(grammar_display_name("cmake")      == L"CMake");
    CHECK(grammar_display_name("git-config") == L"Git Config");
    CHECK(grammar_display_name("git_rebase") == L"Git Rebase");
    CHECK(grammar_display_name("dockerfile") == L"Dockerfile");
    CHECK(grammar_display_name("bash")       == L"Bash");
    CHECK(grammar_display_name("python")     == L"Python");
    CHECK(grammar_display_name("rust")       == L"Rust");
    CHECK(grammar_display_name("go")         == L"Go");
    CHECK(grammar_display_name("java")       == L"Java");
    CHECK(grammar_display_name("lua")        == L"Lua");
}

TEST_CASE("grammar_display_name capitalizes unknown ids as fallback") {
    CHECK(grammar_display_name("zigzag") == L"Zigzag");
    CHECK(grammar_display_name("FOO")    == L"Foo");
    CHECK(grammar_display_name("a")      == L"A");
}

TEST_CASE("grammar_display_name returns empty for empty id") {
    CHECK(grammar_display_name("").empty());
}
