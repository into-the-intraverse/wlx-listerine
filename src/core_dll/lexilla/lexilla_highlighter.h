#pragma once

#include "core_dll/colorizer/color_span.h"
#include "core_dll/theme/helix_theme.h"
#include "wlx_core/abi.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wlx::core::lexilla {

// Everything needed to drive one Lexilla lexer and translate its numeric style
// bytes into Helix theme scopes.
//
// Style -> scope resolution: `style_scopes` is an OVERRIDE map (sparse). For any
// style not overridden, the scope comes from the lexer's own semantic tag
// (ILexer5::TagsOfStyle, from its lexicalClasses table) via a generic
// tag->scope mapping — so most languages need only keyword lists here. Overrides
// cover lexers without lexicalClasses (CSS/JSON/YAML/TOML/SQL/PowerShell) and
// semantic upgrades the generic tags can't express (e.g. the cpp lexer tags
// WORD2 as "identifier"; override it to "type"). An override value of "" means
// "explicitly leave uncolored".
struct LexerSpec {
    std::string lexer_name;                                        // CreateLexer name
    std::vector<std::string> word_lists;                           // WordListSet(0..n)
    std::vector<std::pair<std::string, std::string>> properties;   // PropertySet
    std::unordered_map<int, std::string> style_scopes;             // SCE_* overrides
};

// Theme-independent result of lexing once: one style byte per source byte, plus
// the per-style-byte Helix scope (index = style byte, "" = uncolored). Built by
// lex(); colors are applied later by spans_from_lex() so a re-theme (dark/light
// toggle, viewport rehighlight) never re-lexes.
struct LexResult {
    std::string styles;                       // one style byte per source byte
    std::vector<std::string> style_scope;     // [style byte] -> Helix scope
};

// Lex `source` with `spec`'s lexer once and capture its style bytes + the
// style->scope table (theme-independent). The lexer is created and released
// internally.
WLX_CORE_API LexResult lex(const LexerSpec& spec, std::string_view source);

// Map a cached LexResult to ColorSpans against `theme`. Output is sparse (only
// styled regions), ordered, and non-overlapping — the same shape the tree-sitter
// QueryHighlighter produced. With range_end > range_start only spans
// intersecting [range_start, range_end) are emitted.
WLX_CORE_API std::vector<colorizer::ColorSpan> spans_from_lex(
    const LexResult& lexed, const theme::HelixTheme& theme,
    uint32_t range_start = 0, uint32_t range_end = 0);

// Convenience one-shot: lex() then spans_from_lex().
WLX_CORE_API std::vector<colorizer::ColorSpan> highlight(
    const LexerSpec& spec, std::string_view source, const theme::HelixTheme& theme,
    uint32_t range_start = 0, uint32_t range_end = 0);

}  // namespace wlx::core::lexilla
