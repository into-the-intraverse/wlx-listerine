#include "scope_mapper.h"
#include <unordered_map>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Generic node-type -> Scope table (shared across all grammars)
// ---------------------------------------------------------------------------
static const std::unordered_map<std::string, Scope> g_generic_map = {
    // Comments
    {"comment",             Scope::Comment},
    {"line_comment",        Scope::Comment},
    {"block_comment",       Scope::Comment},

    // Strings
    {"string",                      Scope::String},
    {"string_literal",              Scope::String},
    {"string_content",              Scope::String},
    {"char_literal",                Scope::String},
    {"raw_string_literal",          Scope::String},
    {"interpreted_string_literal",  Scope::String},
    {"template_string",             Scope::String},
    {"heredoc_body",                Scope::String},
    {"string_fragment",             Scope::String},
    {"system_lib_string",           Scope::String},

    // Numbers
    {"number",              Scope::Number},
    {"number_literal",      Scope::Number},
    {"integer",             Scope::Number},
    {"integer_literal",     Scope::Number},
    {"float",               Scope::Number},
    {"float_literal",       Scope::Number},

    // Types
    {"type_identifier",         Scope::Type},
    {"primitive_type",          Scope::Type},
    {"sized_type_specifier",    Scope::Type},

    // Functions
    {"function_declarator", Scope::Function},
    {"call_expression",     Scope::Function},

    // Preprocessor
    {"preproc_include",     Scope::Preprocessor},
    {"preproc_def",         Scope::Preprocessor},
    {"preproc_ifdef",       Scope::Preprocessor},
    {"preproc_directive",   Scope::Preprocessor},

    // Variables / identifiers
    {"identifier",          Scope::Variable},
    {"field_identifier",    Scope::Variable},

    // Namespace
    {"namespace_identifier", Scope::Namespace},

    // Punctuation
    {"{",       Scope::Punctuation},
    {"}",       Scope::Punctuation},
    {"(",       Scope::Punctuation},
    {")",       Scope::Punctuation},
    {"[",       Scope::Punctuation},
    {"]",       Scope::Punctuation},
    {";",       Scope::Punctuation},
    {"comma",   Scope::Punctuation},
    {".",       Scope::Punctuation},
};

// ---------------------------------------------------------------------------
// Per-language keyword tables  (tree-sitter surfaces keywords as anonymous
// leaf nodes whose node_type is the literal keyword text)
// ---------------------------------------------------------------------------
static const std::unordered_map<std::string, std::unordered_set<std::string>> g_keywords = {
    {"c", {
        "if", "else", "for", "while", "do", "switch", "case", "default",
        "break", "continue", "return", "goto",
        "struct", "union", "enum", "typedef",
        "sizeof", "extern", "static", "auto", "register", "volatile",
        "const", "inline", "restrict",
    }},
    {"cpp", {
        "if", "else", "for", "while", "do", "switch", "case", "default",
        "break", "continue", "return", "goto",
        "struct", "union", "enum", "class", "typedef",
        "sizeof", "extern", "static", "auto", "register", "volatile",
        "const", "inline", "constexpr", "consteval", "constinit",
        "new", "delete", "this", "virtual", "override", "final",
        "public", "protected", "private", "friend",
        "namespace", "using", "template", "typename", "operator",
        "throw", "try", "catch", "noexcept",
        "explicit", "mutable", "static_assert",
        "co_await", "co_return", "co_yield",
    }},
    {"python", {
        "def", "class", "import", "from", "as", "if", "elif", "else",
        "for", "while", "break", "continue", "return", "yield",
        "try", "except", "finally", "raise", "with",
        "pass", "del", "assert", "lambda", "global", "nonlocal",
        "not", "and", "or", "in", "is",
        "async", "await",
    }},
    {"javascript", {
        "var", "let", "const", "function", "class", "extends",
        "if", "else", "for", "while", "do", "switch", "case", "default",
        "break", "continue", "return", "yield",
        "try", "catch", "finally", "throw",
        "new", "delete", "typeof", "instanceof", "in", "of",
        "import", "export", "from", "as", "default",
        "async", "await",
        "this", "super",
        "debugger",
    }},
    {"typescript", {
        "var", "let", "const", "function", "class", "extends", "implements",
        "interface", "type", "enum", "namespace", "module", "declare",
        "abstract", "readonly", "override",
        "if", "else", "for", "while", "do", "switch", "case", "default",
        "break", "continue", "return", "yield",
        "try", "catch", "finally", "throw",
        "new", "delete", "typeof", "instanceof", "in", "of",
        "import", "export", "from", "as", "default",
        "async", "await",
        "this", "super",
        "keyof", "infer", "satisfies",
    }},
    {"json",   {}},  // JSON has no keywords
    {"toml",   {}},  // TOML has no keywords
};

// ---------------------------------------------------------------------------
// Per-language type-keyword tables  (keyword2 / secondary keyword colour)
// ---------------------------------------------------------------------------
static const std::unordered_map<std::string, std::unordered_set<std::string>> g_type_keywords = {
    {"c", {
        "int", "char", "short", "long", "float", "double",
        "unsigned", "signed", "void",
        "_Bool", "_Complex", "_Imaginary",
    }},
    {"cpp", {
        "int", "char", "short", "long", "float", "double",
        "unsigned", "signed", "void",
        "bool", "wchar_t", "char8_t", "char16_t", "char32_t",
        "auto",  // in type-deduction context
        "nullptr",
    }},
    {"python", {
        "True", "False", "None",
    }},
    {"javascript", {
        "true", "false", "null", "undefined", "NaN", "Infinity",
    }},
    {"typescript", {
        "true", "false", "null", "undefined", "NaN", "Infinity",
        "string", "number", "boolean", "object", "symbol", "bigint",
        "any", "unknown", "never", "void",
    }},
    {"json", {
        "true", "false", "null",
    }},
    {"toml", {
        "true", "false",
    }},
};

// ---------------------------------------------------------------------------
// ScopeMapper::map
// ---------------------------------------------------------------------------
Scope ScopeMapper::map(const std::string& language, const std::string& node_type) {
    // 1. Language-specific keywords (Keyword)
    auto kw_it = g_keywords.find(language);
    if (kw_it != g_keywords.end()) {
        if (kw_it->second.count(node_type)) {
            return Scope::Keyword;
        }
    }

    // 2. Language-specific type keywords (Keyword2)
    auto tkw_it = g_type_keywords.find(language);
    if (tkw_it != g_type_keywords.end()) {
        if (tkw_it->second.count(node_type)) {
            return Scope::Keyword2;
        }
    }

    // 3. Generic node-type map
    auto gen_it = g_generic_map.find(node_type);
    if (gen_it != g_generic_map.end()) {
        return gen_it->second;
    }

    // 4. Fallback
    return Scope::Plain;
}

// ---------------------------------------------------------------------------
// scope_to_color
// ---------------------------------------------------------------------------
uint32_t scope_to_color(Scope scope, const SyntaxPalette& palette) {
    switch (scope) {
        case Scope::Keyword:      return palette.keyword;
        case Scope::Keyword2:     return palette.keyword2;
        case Scope::Function:     return palette.function;
        case Scope::String:       return palette.string;
        case Scope::Number:       return palette.number;
        case Scope::Comment:      return palette.comment;
        case Scope::Operator:     return palette.op;
        case Scope::Type:         return palette.type;
        case Scope::Preprocessor: return palette.preprocessor;
        case Scope::Namespace:    return palette.ns;
        case Scope::Variable:     return palette.variable;
        case Scope::Punctuation:  return palette.punctuation;
        case Scope::Plain:        return palette.plain;
    }
    return palette.plain;
}
