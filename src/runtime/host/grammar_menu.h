#pragma once

#include "wlx_core/abi.h"

#include <string>
#include <string_view>
#include <vector>

namespace wlx::runtime::host {

struct LanguageOption {
    std::string  grammar_id;
    std::wstring display_name;
};

// Pure mapping. Known grammar ids return their human-readable display
// (e.g., "cpp" → "C++"). Unknown ids fall back to capitalized id
// ("foobar" → "Foobar"). Empty input returns empty.
std::wstring grammar_display_name(std::string_view grammar_id);

// Enumerates grammars from the core ABI, attaches display names, sorts
// case-insensitively by display_name. Returns an empty vector if `core`
// is null or the ABI call fails.
std::vector<LanguageOption> available_grammars(WlxCore* core);

}  // namespace wlx::runtime::host
