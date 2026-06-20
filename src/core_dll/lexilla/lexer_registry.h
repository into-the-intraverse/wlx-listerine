#pragma once

#include "core_dll/lexilla/lexilla_highlighter.h"
#include "wlx_core/abi.h"

#include <string>
#include <string_view>
#include <vector>

namespace wlx::core::lexilla {

// Look up the LexerSpec for one of our language ids (the ids produced by
// path_to_language: "cpp", "python", "javascript", "json", ...). Returns
// nullptr when no Lexilla lexer is mapped for the language — the caller then
// renders it as plain text (uncolored but fully interactive). The returned
// pointer is to a process-lifetime static and is safe to hold.
WLX_CORE_API const LexerSpec* lexer_spec_for(std::string_view language);

// Sorted list of all language ids that have a LexerSpec (drives the grammar /
// language context menu via wlx_core_list_languages).
WLX_CORE_API std::vector<std::string> registered_languages();

}  // namespace wlx::core::lexilla
