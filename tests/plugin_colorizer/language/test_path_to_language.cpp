#include <doctest/doctest.h>

#include "plugin_colorizer/language/path_to_language.h"

using wlx::plugin_colorizer::language::ext_to_language;
using wlx::plugin_colorizer::language::filename_to_language;

// ---------- ext_to_language ----------

TEST_CASE("ext_to_language: C / C++ family") {
    CHECK(ext_to_language(L"foo.c")    == "cpp");
    CHECK(ext_to_language(L"foo.h")    == "cpp");
    CHECK(ext_to_language(L"foo.cpp")  == "cpp");
    CHECK(ext_to_language(L"foo.cc")   == "cpp");
    CHECK(ext_to_language(L"foo.cxx")  == "cpp");
    CHECK(ext_to_language(L"foo.hpp")  == "cpp");
    CHECK(ext_to_language(L"foo.hxx")  == "cpp");
}

TEST_CASE("ext_to_language: Python / JS / TS") {
    CHECK(ext_to_language(L"app.py")   == "python");
    CHECK(ext_to_language(L"types.pyi") == "python");
    CHECK(ext_to_language(L"x.js")     == "javascript");
    CHECK(ext_to_language(L"x.mjs")    == "javascript");
    CHECK(ext_to_language(L"x.cjs")    == "javascript");
    CHECK(ext_to_language(L"x.jsx")    == "javascript");
    CHECK(ext_to_language(L"x.ts")     == "typescript");
    CHECK(ext_to_language(L"x.tsx")    == "typescript");
    CHECK(ext_to_language(L"x.mts")    == "typescript");
}

TEST_CASE("ext_to_language: shell rc dotfiles (the .bashrc / .zshrc family)") {
    CHECK(ext_to_language(L".bashrc")        == "bash");
    CHECK(ext_to_language(L".bash_profile")  == "bash");
    CHECK(ext_to_language(L".bash_aliases")  == "bash");
    CHECK(ext_to_language(L".bash_logout")   == "bash");
    CHECK(ext_to_language(L".zshrc")         == "bash");
    CHECK(ext_to_language(L".zshenv")        == "bash");
    CHECK(ext_to_language(L".zprofile")      == "bash");
    CHECK(ext_to_language(L".zlogin")        == "bash");
    CHECK(ext_to_language(L".zlogout")       == "bash");
    CHECK(ext_to_language(L".profile")       == "bash");
    CHECK(ext_to_language(L".envrc")         == "bash");
}

TEST_CASE("ext_to_language: PowerShell / Vim") {
    CHECK(ext_to_language(L"x.ps1")   == "powershell");
    CHECK(ext_to_language(L"x.psm1")  == "powershell");
    CHECK(ext_to_language(L"x.psd1")  == "powershell");
    CHECK(ext_to_language(L"init.vim") == "vim");
    CHECK(ext_to_language(L".vimrc")   == "vim");
    CHECK(ext_to_language(L".nvimrc")  == "vim");
}

TEST_CASE("ext_to_language: data / config") {
    CHECK(ext_to_language(L"x.json")  == "json");
    CHECK(ext_to_language(L"x.jsonc") == "json");
    CHECK(ext_to_language(L"x.toml")  == "toml");
    CHECK(ext_to_language(L"x.yaml")  == "yaml");
    CHECK(ext_to_language(L"x.yml")   == "yaml");
}

TEST_CASE("ext_to_language: markup") {
    CHECK(ext_to_language(L"x.html") == "html");
    CHECK(ext_to_language(L"x.htm")  == "html");
    CHECK(ext_to_language(L"x.xml")  == "xml");
    CHECK(ext_to_language(L"x.svg")  == "xml");
    CHECK(ext_to_language(L"x.css")  == "css");
}

TEST_CASE("ext_to_language: Visual Studio / MSBuild XML") {
    CHECK(ext_to_language(L"app.vcxproj") == "xml");
    CHECK(ext_to_language(L"app.csproj")  == "xml");
    CHECK(ext_to_language(L"app.fsproj")  == "xml");
    CHECK(ext_to_language(L"app.vbproj")  == "xml");
    CHECK(ext_to_language(L"shared.proj") == "xml");
    CHECK(ext_to_language(L"common.props")    == "xml");
    CHECK(ext_to_language(L"common.targets")  == "xml");
    CHECK(ext_to_language(L"app.vcxproj.filters") == "xml");  // .filters
    CHECK(ext_to_language(L"MyApp.slnx")    == "xml");  // VS 17.13+ XML solution
    CHECK(ext_to_language(L"main.xaml") == "xml");
    CHECK(ext_to_language(L"strings.resx") == "xml");
}

TEST_CASE("ext_to_language: build / git") {
    CHECK(ext_to_language(L"foo.cmake")        == "cmake");
    CHECK(ext_to_language(L"x.dockerfile")     == "dockerfile");
    CHECK(ext_to_language(L"x.sql")            == "sql");
    CHECK(ext_to_language(L".gitconfig")       == "git-config");
    CHECK(ext_to_language(L".gitmodules")      == "git-config");
    CHECK(ext_to_language(L".gitignore")       == "gitignore");
    CHECK(ext_to_language(L".gitattributes")   == "gitattributes");
    CHECK(ext_to_language(L".dockerignore")    == "gitignore");
    CHECK(ext_to_language(L".npmignore")       == "gitignore");
}

TEST_CASE("ext_to_language: case-insensitive") {
    CHECK(ext_to_language(L"FOO.CPP")     == "cpp");
    CHECK(ext_to_language(L"App.CsProj")  == "xml");
    CHECK(ext_to_language(L".BASHRC")     == "bash");
}

TEST_CASE("ext_to_language: unknown extension returns empty") {
    CHECK(ext_to_language(L"foo.zzz").empty());
    CHECK(ext_to_language(L"a.unknown").empty());
}

TEST_CASE("ext_to_language: no-dot files return empty (filename_to_language territory)") {
    CHECK(ext_to_language(L"Pipfile").empty());
    CHECK(ext_to_language(L"Makefile").empty());
    CHECK(ext_to_language(L"Dockerfile").empty());
    CHECK(ext_to_language(L"CMakeLists.txt") == "");  // .txt isn't in kExtTable
}

TEST_CASE("ext_to_language: dot in directory name doesn't confuse the lookup") {
    // The pre-refactor implementation used path.find_last_of('.') against
    // the full path, which broke for paths like `D:\my.project\Pipfile`.
    // The new helper restricts the dot search to the basename.
    CHECK(ext_to_language(L"D:\\my.project\\Pipfile").empty());
    CHECK(ext_to_language(L"C:/my.dotted.dir/Cargo.toml") == "toml");
    CHECK(ext_to_language(L"/path.with.dots/script.py")   == "python");
}

// ---------- filename_to_language ----------

TEST_CASE("filename_to_language: Dockerfile and friends") {
    CHECK(filename_to_language(L"Dockerfile")          == "dockerfile");
    CHECK(filename_to_language(L"dockerfile")          == "dockerfile");
    CHECK(filename_to_language(L"Containerfile")       == "dockerfile");
    CHECK(filename_to_language(L"Dockerfile.dev")      == "dockerfile");
    CHECK(filename_to_language(L"Dockerfile.prod")     == "dockerfile");
    CHECK(filename_to_language(L"my.dockerfile.local") == "dockerfile");
}

TEST_CASE("filename_to_language: CMake-related .txt files") {
    CHECK(filename_to_language(L"CMakeLists.txt") == "cmake");
    CHECK(filename_to_language(L"cmakelists.txt") == "cmake");
    CHECK(filename_to_language(L"CMakeCache.txt") == "cmake");
    CHECK(filename_to_language(L"CMAKECACHE.TXT") == "cmake");
}

TEST_CASE("filename_to_language: Python ecosystem") {
    CHECK(filename_to_language(L"Pipfile")       == "toml");
    CHECK(filename_to_language(L"pipfile")       == "toml");
    CHECK(filename_to_language(L"Pipfile.lock")  == "json");
    CHECK(filename_to_language(L"uv.lock")       == "toml");
    CHECK(filename_to_language(L"poetry.lock")   == "toml");
}

TEST_CASE("filename_to_language: JS ecosystem") {
    CHECK(filename_to_language(L"bun.lock")      == "json");
}

TEST_CASE("filename_to_language: git rebase todo") {
    CHECK(filename_to_language(L"git-rebase-todo") == "git_rebase");
    CHECK(filename_to_language(L".git/rebase-merge/git-rebase-todo") == "git_rebase");
}

TEST_CASE("filename_to_language: unrelated names return empty") {
    CHECK(filename_to_language(L"foo.txt").empty());
    CHECK(filename_to_language(L"random_name").empty());
    CHECK(filename_to_language(L"data.lock").empty());     // not a known lock
    CHECK(filename_to_language(L"setup.py").empty());      // ext lookup handles this
}

TEST_CASE("filename_to_language: handles full paths") {
    CHECK(filename_to_language(L"D:\\projects\\foo\\CMakeLists.txt") == "cmake");
    CHECK(filename_to_language(L"/home/user/project/uv.lock") == "toml");
    CHECK(filename_to_language(L"C:/x/Pipfile.lock") == "json");
}

// ---------- Composition: ext_to_language first, then filename_to_language ----------

TEST_CASE("composition: extension lookup wins over filename when both could match") {
    // The host adapter calls ext_to_language first; only on empty does it
    // fall through to filename_to_language. This test pins that contract.
    // foo.toml: ext lookup returns "toml" — filename lookup is never asked.
    CHECK(ext_to_language(L"D:\\proj\\Cargo.toml") == "toml");
    // Pipfile: ext lookup returns "" (no dot), so filename lookup runs.
    CHECK(ext_to_language(L"D:\\proj\\Pipfile").empty());
    CHECK(filename_to_language(L"D:\\proj\\Pipfile") == "toml");
}
