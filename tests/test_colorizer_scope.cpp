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

// ---------------------------------------------------------------------------
// Rust
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps Rust fn to keyword") {
    CHECK(ScopeMapper::map("rust", "fn") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Rust async to keyword") {
    CHECK(ScopeMapper::map("rust", "async") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Rust i32 to keyword2") {
    CHECK(ScopeMapper::map("rust", "i32") == Scope::Keyword2);
}

TEST_CASE("ScopeMapper maps Rust Option to keyword2") {
    CHECK(ScopeMapper::map("rust", "Option") == Scope::Keyword2);
}

// ---------------------------------------------------------------------------
// Go
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps Go func to keyword") {
    CHECK(ScopeMapper::map("go", "func") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Go defer to keyword") {
    CHECK(ScopeMapper::map("go", "defer") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Go nil to keyword2") {
    CHECK(ScopeMapper::map("go", "nil") == Scope::Keyword2);
}

TEST_CASE("ScopeMapper maps Go string type to keyword2") {
    CHECK(ScopeMapper::map("go", "string") == Scope::Keyword2);
}

// ---------------------------------------------------------------------------
// Java
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps Java class to keyword") {
    CHECK(ScopeMapper::map("java", "class") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Java instanceof to keyword") {
    CHECK(ScopeMapper::map("java", "instanceof") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Java null to keyword2") {
    CHECK(ScopeMapper::map("java", "null") == Scope::Keyword2);
}

// ---------------------------------------------------------------------------
// C#
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps C# foreach to keyword") {
    CHECK(ScopeMapper::map("c-sharp", "foreach") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps C# await to keyword") {
    CHECK(ScopeMapper::map("c-sharp", "await") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps C# bool to keyword2") {
    CHECK(ScopeMapper::map("c-sharp", "bool") == Scope::Keyword2);
}

// ---------------------------------------------------------------------------
// Ruby
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps Ruby def to keyword") {
    CHECK(ScopeMapper::map("ruby", "def") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Ruby rescue to keyword") {
    CHECK(ScopeMapper::map("ruby", "rescue") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Ruby nil to keyword2") {
    CHECK(ScopeMapper::map("ruby", "nil") == Scope::Keyword2);
}

// ---------------------------------------------------------------------------
// PHP
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps PHP function to keyword") {
    CHECK(ScopeMapper::map("php", "function") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps PHP foreach to keyword") {
    CHECK(ScopeMapper::map("php", "foreach") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps PHP null to keyword2") {
    CHECK(ScopeMapper::map("php", "null") == Scope::Keyword2);
}

// ---------------------------------------------------------------------------
// Lua
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps Lua function to keyword") {
    CHECK(ScopeMapper::map("lua", "function") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Lua repeat to keyword") {
    CHECK(ScopeMapper::map("lua", "repeat") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Lua nil to keyword2") {
    CHECK(ScopeMapper::map("lua", "nil") == Scope::Keyword2);
}

// ---------------------------------------------------------------------------
// Bash
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps Bash fi to keyword") {
    CHECK(ScopeMapper::map("bash", "fi") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Bash esac to keyword") {
    CHECK(ScopeMapper::map("bash", "esac") == Scope::Keyword);
}

// ---------------------------------------------------------------------------
// PowerShell
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps PowerShell foreach to keyword") {
    CHECK(ScopeMapper::map("powershell", "foreach") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps PowerShell param to keyword") {
    CHECK(ScopeMapper::map("powershell", "param") == Scope::Keyword);
}

// ---------------------------------------------------------------------------
// Vim script
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps Vim endfunction to keyword") {
    CHECK(ScopeMapper::map("vim", "endfunction") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Vim autocmd to keyword") {
    CHECK(ScopeMapper::map("vim", "autocmd") == Scope::Keyword);
}

// ---------------------------------------------------------------------------
// SQL
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps SQL select to keyword") {
    CHECK(ScopeMapper::map("sql", "select") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps SQL join to keyword") {
    CHECK(ScopeMapper::map("sql", "join") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps SQL null to keyword") {
    // null is in the SQL keyword set (appears as a reserved word in the grammar)
    CHECK(ScopeMapper::map("sql", "null") == Scope::Keyword);
}

// ---------------------------------------------------------------------------
// Dockerfile
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps Dockerfile FROM to keyword") {
    CHECK(ScopeMapper::map("dockerfile", "FROM") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps Dockerfile RUN to keyword") {
    CHECK(ScopeMapper::map("dockerfile", "RUN") == Scope::Keyword);
}

// ---------------------------------------------------------------------------
// CMake
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps CMake foreach to keyword") {
    CHECK(ScopeMapper::map("cmake", "foreach") == Scope::Keyword);
}

TEST_CASE("ScopeMapper maps CMake endfunction to keyword") {
    CHECK(ScopeMapper::map("cmake", "endfunction") == Scope::Keyword);
}

// ---------------------------------------------------------------------------
// Generic map: HTML node types
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps HTML tag_name to type") {
    CHECK(ScopeMapper::map("html", "tag_name") == Scope::Type);
}

TEST_CASE("ScopeMapper maps HTML attribute_name to variable") {
    CHECK(ScopeMapper::map("html", "attribute_name") == Scope::Variable);
}

TEST_CASE("ScopeMapper maps HTML attribute_value to string") {
    CHECK(ScopeMapper::map("html", "attribute_value") == Scope::String);
}

TEST_CASE("ScopeMapper maps XML tag_name to type") {
    CHECK(ScopeMapper::map("xml", "tag_name") == Scope::Type);
}

// ---------------------------------------------------------------------------
// Generic map: CSS node types
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps CSS property_name to variable") {
    CHECK(ScopeMapper::map("css", "property_name") == Scope::Variable);
}

TEST_CASE("ScopeMapper maps CSS class_name to type") {
    CHECK(ScopeMapper::map("css", "class_name") == Scope::Type);
}

TEST_CASE("ScopeMapper maps CSS id_name to type") {
    CHECK(ScopeMapper::map("css", "id_name") == Scope::Type);
}

TEST_CASE("ScopeMapper maps CSS color_value to number") {
    CHECK(ScopeMapper::map("css", "color_value") == Scope::Number);
}

TEST_CASE("ScopeMapper maps CSS plain_value to string") {
    CHECK(ScopeMapper::map("css", "plain_value") == Scope::String);
}

// ---------------------------------------------------------------------------
// Generic map: YAML node types
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps YAML anchor to preprocessor") {
    CHECK(ScopeMapper::map("yaml", "anchor") == Scope::Preprocessor);
}

TEST_CASE("ScopeMapper maps YAML tag to type") {
    CHECK(ScopeMapper::map("yaml", "tag") == Scope::Type);
}

// ---------------------------------------------------------------------------
// Config-like languages — verify they fall through to generic map or plain
// ---------------------------------------------------------------------------
TEST_CASE("ScopeMapper maps INI section to type via generic map") {
    CHECK(ScopeMapper::map("ini", "section") == Scope::Type);
}

TEST_CASE("ScopeMapper maps gitconfig bare_key to variable via generic map") {
    CHECK(ScopeMapper::map("gitconfig", "bare_key") == Scope::Variable);
}

TEST_CASE("ScopeMapper maps gitignore comment to comment via generic map") {
    CHECK(ScopeMapper::map("gitignore", "comment") == Scope::Comment);
}

TEST_CASE("ScopeMapper maps gitattributes string to string via generic map") {
    CHECK(ScopeMapper::map("gitattributes", "string") == Scope::String);
}
