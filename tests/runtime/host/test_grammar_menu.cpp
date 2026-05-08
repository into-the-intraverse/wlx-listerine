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
    // Coverage for the remaining 7 entries in kDisplayTable that the spec
    // didn't enumerate. Without these, a typo in one of these display
    // strings (e.g., "Ph P" or "powershell") would silently ship.
    CHECK(grammar_display_name("php")           == L"PHP");
    CHECK(grammar_display_name("powershell")    == L"PowerShell");
    CHECK(grammar_display_name("sql")           == L"SQL");
    CHECK(grammar_display_name("vim")           == L"Vim");
    CHECK(grammar_display_name("xml")           == L"XML");
    CHECK(grammar_display_name("gitattributes") == L"Git Attributes");
    CHECK(grammar_display_name("gitignore")     == L"Git Ignore");
}

TEST_CASE("grammar_display_name capitalizes unknown ids as fallback") {
    CHECK(grammar_display_name("zigzag") == L"Zigzag");
    CHECK(grammar_display_name("FOO")    == L"Foo");
    CHECK(grammar_display_name("a")      == L"A");
}

TEST_CASE("grammar_display_name returns empty for empty id") {
    CHECK(grammar_display_name("").empty());
}
