#pragma once

#include "theme_loader.h"
#include <string>
#include <unordered_set>

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

struct LanguageContext {
    const std::unordered_set<std::string>* keywords = nullptr;
    const std::unordered_set<std::string>* type_keywords = nullptr;
};

class ScopeMapper {
public:
    static Scope map(const std::string& language, const std::string& node_type);
    static LanguageContext for_language(const std::string& language);
    static Scope map(const LanguageContext& ctx, const std::string& node_type);
};

uint32_t scope_to_color(Scope scope, const SyntaxPalette& palette);
