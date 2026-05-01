#include <doctest/doctest.h>
#include <filesystem>
#include "colorizer.h"
#include "colorizer_layout.h"
#include "colorizer_routing.h"

namespace fs = std::filesystem;

static const bool grammars_present =
    fs::exists("grammars/cpp/tree-sitter-cpp.dll") &&
    fs::exists("grammars/unreal-cpp/tree-sitter-unreal-cpp.dll");

TEST_CASE("apply_cpp_variant: standard -> cpp"
    * doctest::skip(!grammars_present)) {
    Colorizer c(L"grammars", L"config/themes");
    CHECK(apply_cpp_variant("cpp", CppGrammar::Standard, &c) == "cpp");
}

TEST_CASE("apply_cpp_variant: unreal -> unreal-cpp"
    * doctest::skip(!grammars_present)) {
    Colorizer c(L"grammars", L"config/themes");
    CHECK(apply_cpp_variant("cpp", CppGrammar::Unreal, &c) == "unreal-cpp");
}

TEST_CASE("apply_cpp_variant: unreal but grammar missing -> falls back to cpp") {
    // Point at a non-existent grammar dir so the colorizer's registry has no grammars.
    Colorizer c(L"nonexistent_grammars_dir", L"config/themes");
    CHECK(apply_cpp_variant("cpp", CppGrammar::Unreal, &c) == "cpp");
}

TEST_CASE("apply_cpp_variant: non-cpp lang is untouched"
    * doctest::skip(!grammars_present)) {
    Colorizer c(L"grammars", L"config/themes");
    CHECK(apply_cpp_variant("python", CppGrammar::Unreal, &c) == "python");
    CHECK(apply_cpp_variant("rust", CppGrammar::Standard, &c) == "rust");
}

TEST_CASE("apply_cpp_variant: null colorizer is defensive") {
    CHECK(apply_cpp_variant("cpp", CppGrammar::Unreal,
                            static_cast<const Colorizer*>(nullptr)) == "cpp");
    CHECK(apply_cpp_variant("cpp", CppGrammar::Standard,
                            static_cast<const Colorizer*>(nullptr)) == "cpp");
    // Same defensive behavior for the WlxCore* overload used by host plugins.
    CHECK(apply_cpp_variant("cpp", CppGrammar::Unreal,
                            static_cast<WlxCore*>(nullptr)) == "cpp");
    CHECK(apply_cpp_variant("cpp", CppGrammar::Standard,
                            static_cast<WlxCore*>(nullptr)) == "cpp");
}
