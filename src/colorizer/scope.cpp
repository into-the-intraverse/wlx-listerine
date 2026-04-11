#include "scope.h"
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Capture name -> Scope exact-match table
// ---------------------------------------------------------------------------
static const std::unordered_map<std::string, Scope>& capture_map() {
    static const std::unordered_map<std::string, Scope> map = {
        // Keywords
        {"keyword",                    Scope::Keyword},
        {"keyword.directive",          Scope::Preprocessor},
        {"keyword.directive.define",   Scope::Preprocessor},

        // Functions
        {"function",                   Scope::Function},
        {"function.builtin",           Scope::FunctionBuiltin},
        {"function.call",              Scope::FunctionCall},
        {"function.method.call",       Scope::FunctionCall},

        // Strings
        {"string",                     Scope::String},
        {"string.escape",              Scope::StringEscape},
        {"string.special",             Scope::StringSpecial},
        {"string.regexp",              Scope::StringSpecial},

        // Characters (mapped to string family)
        {"character",                  Scope::String},
        {"character.special",          Scope::StringEscape},

        // Numbers
        {"number",                     Scope::Number},
        {"number.float",               Scope::Number},

        // Boolean
        {"boolean",                    Scope::Boolean},

        // Comments
        {"comment",                    Scope::Comment},

        // Operators
        {"operator",                   Scope::Operator},

        // Types
        {"type",                       Scope::Type},
        {"type.builtin",               Scope::Keyword2},
        {"type.definition",            Scope::Type},

        // Constants
        {"constant",                   Scope::Variable},
        {"constant.builtin",           Scope::ConstantBuiltin},
        {"constant.macro",             Scope::Variable},

        // Constructor
        {"constructor",                Scope::Constructor},

        // Modules / namespaces
        {"module",                     Scope::Namespace},
        {"module.builtin",             Scope::Namespace},
        {"namespace",                  Scope::Namespace},

        // Variables
        {"variable",                   Scope::Variable},

        // Properties
        {"property",                   Scope::Property},

        // Labels
        {"label",                      Scope::Label},

        // Punctuation
        {"punctuation",                Scope::Punctuation},

        // Tags (HTML/XML)
        {"tag",                        Scope::Tag},
        {"tag.builtin",                Scope::Tag},
        {"tag.delimiter",              Scope::TagDelimiter},
        {"tag.attribute",              Scope::Attribute},

        // Attributes
        {"attribute",                  Scope::Attribute},
        {"attribute.builtin",          Scope::Attribute},

        // Preprocessor
        {"preproc",                    Scope::Preprocessor},
    };
    return map;
}

// ---------------------------------------------------------------------------
// capture_to_scope
// ---------------------------------------------------------------------------
Scope capture_to_scope(std::string_view capture) {
    if (capture.empty()) return Scope::Plain;

    const auto& map = capture_map();

    // Try exact match first
    std::string key(capture);
    auto it = map.find(key);
    if (it != map.end()) return it->second;

    // Prefix fallback: strip last dot-suffix and retry
    while (true) {
        auto dot = key.rfind('.');
        if (dot == std::string::npos) break;
        key.erase(dot);
        it = map.find(key);
        if (it != map.end()) return it->second;
    }

    return Scope::Plain;
}

// ---------------------------------------------------------------------------
// scope_to_color
// ---------------------------------------------------------------------------
uint32_t scope_to_color(Scope scope, const SyntaxPalette& palette) {
    switch (scope) {
        case Scope::Keyword:         return palette.keyword;
        case Scope::Keyword2:        return palette.keyword2;
        case Scope::Function:        return palette.function;
        case Scope::String:          return palette.string;
        case Scope::Number:          return palette.number;
        case Scope::Comment:         return palette.comment;
        case Scope::Operator:        return palette.op;
        case Scope::Type:            return palette.type;
        case Scope::Preprocessor:    return palette.preprocessor;
        case Scope::Namespace:       return palette.ns;
        case Scope::Variable:        return palette.variable;
        case Scope::Punctuation:     return palette.punctuation;
        case Scope::Plain:           return palette.plain;
        case Scope::ConstantBuiltin: return palette.constant_builtin;
        case Scope::FunctionBuiltin: return palette.function_builtin;
        case Scope::FunctionCall:    return palette.function_call;
        case Scope::StringEscape:    return palette.string_escape;
        case Scope::StringSpecial:   return palette.string_special;
        case Scope::Boolean:         return palette.boolean_lit;
        case Scope::Tag:             return palette.tag;
        case Scope::TagDelimiter:    return palette.tag_delimiter;
        case Scope::Attribute:       return palette.attribute;
        case Scope::Constructor:     return palette.constructor;
        case Scope::Property:        return palette.property;
        case Scope::Label:           return palette.label;
    }
    return palette.plain;
}
