#pragma once

#include "theme_loader.h"
#include <string>

enum class Scope {
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
    Plain
};

class ScopeMapper {
public:
    static Scope map(const std::string& language, const std::string& node_type);
};

uint32_t scope_to_color(Scope scope, const SyntaxPalette& palette);
