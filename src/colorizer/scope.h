#pragma once

#include "theme_loader.h"
#include <cstdint>
#include <string_view>

// Expanded scope enum for query-based highlighting (25 variants).
// The original 13 scopes are preserved; 12 new ones follow.
enum class Scope {
    // Original 13
    Keyword,
    Keyword2,
    Function,
    String,
    Number,
    Comment,
    Operator,
    Type,
    Preprocessor,
    Namespace,
    Variable,
    Punctuation,
    Plain,

    // New 12
    ConstantBuiltin,
    FunctionBuiltin,
    FunctionCall,
    StringEscape,
    StringSpecial,
    Boolean,
    Tag,
    TagDelimiter,
    Attribute,
    Constructor,
    Property,
    Label
};

// Map a tree-sitter capture name (without leading @) to a Scope.
// Uses exact match first, then strips dot-suffixes for prefix fallback.
// Returns Scope::Plain for unrecognized captures.
Scope capture_to_scope(std::string_view capture);

// Map a Scope to its palette color.
uint32_t scope_to_color(Scope scope, const SyntaxPalette& palette);
