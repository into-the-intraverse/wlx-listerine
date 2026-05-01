#pragma once

#include <string>
#include "colorizer.h"
#include "colorizer_layout.h"
#include "wlx_core/abi.h"

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

// WlxCore-handle overload: post-Task-3 the host plugins no longer have direct
// Colorizer pointers, so they pass the ABI handle instead. Same semantics as
// the Colorizer* overload — unit-tested via the Colorizer* version.
inline std::string apply_cpp_variant(const std::string& lang,
                                     CppGrammar variant,
                                     WlxCore* core) {
    if (lang == "cpp"
        && variant == CppGrammar::Unreal
        && core != nullptr
        && wlx_core_supports(core, "unreal-cpp") == 1) {
        return "unreal-cpp";
    }
    return lang;
}
