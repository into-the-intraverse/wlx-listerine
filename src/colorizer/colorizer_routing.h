#pragma once

#include <string>
#include "colorizer.h"
#include "colorizer_layout.h"

// Pure routing primitive: when the user opts into the Unreal C++ grammar via
// [colorizer].cpp_grammar = "unreal" AND the unreal-cpp grammar is actually
// loadable, swap "cpp" for "unreal-cpp". Falls back to "cpp" if the grammar
// is missing. Non-cpp languages are passed through unchanged.
inline std::string apply_cpp_variant(const std::string& lang,
                                     CppGrammar variant,
                                     const Colorizer* colorizer) {
    if (lang == "cpp"
        && variant == CppGrammar::Unreal
        && colorizer != nullptr
        && colorizer->supports("unreal-cpp")) {
        return "unreal-cpp";
    }
    return lang;
}
