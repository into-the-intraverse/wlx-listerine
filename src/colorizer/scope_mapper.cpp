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

    // HTML / XML node types
    {"tag_name",            Scope::Type},
    {"attribute_name",      Scope::Variable},
    {"attribute_value",     Scope::String},
    {"doctype",             Scope::Preprocessor},
    {"cdata_section",       Scope::Comment},

    // CSS node types
    {"property_name",       Scope::Variable},
    {"class_name",          Scope::Type},
    {"id_name",             Scope::Type},
    {"color_value",         Scope::Number},
    {"plain_value",         Scope::String},
    {"selector",            Scope::Type},
    {"pseudo_class",        Scope::Keyword},
    {"pseudo_element",      Scope::Keyword},
    {"unit",                Scope::Keyword2},

    // YAML node types
    {"block_mapping_pair",  Scope::Variable},
    {"flow_pair",           Scope::Variable},
    {"anchor",              Scope::Preprocessor},
    {"alias",               Scope::Preprocessor},
    {"tag",                 Scope::Type},

    // Markdown node types
    {"atx_heading",         Scope::Keyword},
    {"setext_heading",      Scope::Keyword},
    {"code_span",           Scope::String},
    {"fenced_code_block",   Scope::String},
    {"link_text",           Scope::Variable},
    {"link_destination",    Scope::String},
    {"emphasis",            Scope::Plain},
    {"strong_emphasis",     Scope::Plain},

    // SQL node types
    {"keyword",             Scope::Keyword},
    {"relation",            Scope::Type},
    {"column",              Scope::Variable},

    // INI / config node types
    {"section",             Scope::Type},
    {"setting",             Scope::Variable},
    {"bare_key",            Scope::Variable},
    {"quoted_key",          Scope::Variable},
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

    {"rust", {
        "fn", "let", "mut", "const", "static", "struct", "enum", "impl",
        "trait", "type", "pub", "mod", "use", "crate", "self", "super",
        "where", "async", "await", "match", "if", "else", "for", "while",
        "loop", "break", "continue", "return", "move", "ref", "unsafe",
        "extern", "dyn", "in",
    }},
    {"go", {
        "func", "var", "const", "type", "struct", "interface", "map", "chan",
        "go", "select", "case", "default", "if", "else", "for", "range",
        "switch", "break", "continue", "return", "defer", "package", "import",
        "fallthrough",
    }},
    {"java", {
        "class", "interface", "enum", "extends", "implements", "abstract",
        "final", "static", "public", "private", "protected", "void", "new",
        "this", "super", "if", "else", "for", "while", "do", "switch", "case",
        "break", "continue", "return", "try", "catch", "finally", "throw",
        "throws", "import", "package", "synchronized", "volatile", "transient",
        "native", "instanceof", "default",
    }},
    {"c-sharp", {
        "class", "struct", "interface", "enum", "delegate", "event",
        "abstract", "sealed", "static", "virtual", "override", "new", "this",
        "base", "if", "else", "for", "foreach", "while", "do", "switch",
        "case", "break", "continue", "return", "try", "catch", "finally",
        "throw", "using", "namespace", "public", "private", "protected",
        "internal", "async", "await", "yield", "var", "dynamic", "readonly",
        "ref", "out", "in", "is", "as", "typeof", "sizeof", "lock",
        "checked", "unchecked", "default",
    }},
    {"ruby", {
        "def", "class", "module", "end", "if", "elsif", "else", "unless",
        "while", "until", "for", "do", "begin", "rescue", "ensure", "raise",
        "return", "yield", "require", "include", "extend", "self", "super",
        "lambda", "proc", "then", "in", "and", "or", "not", "when",
    }},
    {"php", {
        "function", "class", "interface", "trait", "extends", "implements",
        "abstract", "final", "static", "public", "private", "protected",
        "new", "echo", "print", "if", "else", "elseif", "for", "foreach",
        "while", "do", "switch", "case", "break", "continue", "return",
        "try", "catch", "finally", "throw", "use", "namespace", "require",
        "include", "require_once", "include_once", "isset", "unset", "empty",
        "array", "list", "global", "const", "var",
    }},
    {"lua", {
        "function", "local", "if", "then", "else", "elseif", "end", "for",
        "while", "do", "repeat", "until", "return", "break", "in", "and",
        "or", "not", "goto", "require",
    }},
    {"bash", {
        "if", "then", "else", "elif", "fi", "for", "while", "do", "done",
        "case", "esac", "in", "function", "return", "local", "export",
        "source", "alias", "unset", "readonly", "declare", "typeset", "set",
        "shift", "exit", "break", "continue", "trap", "eval", "exec",
    }},
    {"powershell", {
        "function", "if", "else", "elseif", "switch", "for", "foreach",
        "while", "do", "until", "break", "continue", "return", "throw",
        "try", "catch", "finally", "class", "enum", "using", "param",
        "begin", "process", "end", "filter", "workflow", "parallel",
        "sequence", "exit", "trap", "in",
    }},
    {"vim", {
        "function", "endfunction", "if", "else", "elseif", "endif", "for",
        "endfor", "while", "endwhile", "let", "set", "call", "return",
        "echo", "echom", "command", "autocmd", "augroup", "execute",
        "normal", "map", "nmap", "vmap", "imap", "nnoremap", "vnoremap",
        "inoremap", "try", "catch", "finally", "endtry", "throw",
        "source", "finish", "break", "continue",
    }},
    {"sql", {
        "select", "from", "where", "insert", "into", "update", "set",
        "delete", "create", "drop", "alter", "table", "index", "view",
        "join", "left", "right", "inner", "outer", "on", "and", "or",
        "not", "in", "between", "like", "is", "null", "as", "order", "by",
        "group", "having", "limit", "offset", "union", "all", "distinct",
        "exists", "case", "when", "then", "else", "end", "begin", "commit",
        "rollback", "grant", "revoke", "primary", "key", "foreign",
        "references", "constraint", "default", "check", "unique", "values",
    }},
    // Dockerfile: instruction keywords appear as node types in the grammar
    {"dockerfile", {
        "FROM", "RUN", "CMD", "COPY", "ADD", "ENV", "EXPOSE", "WORKDIR",
        "ENTRYPOINT", "VOLUME", "USER", "LABEL", "ARG", "ONBUILD",
        "STOPSIGNAL", "HEALTHCHECK", "SHELL",
    }},
    // CMake: command names appear as identifiers; the keywords below are
    // tree-sitter-cmake anonymous nodes
    {"cmake", {
        "if", "elseif", "else", "endif", "while", "endwhile", "foreach",
        "endforeach", "function", "endfunction", "macro", "endmacro",
        "return", "break", "continue",
    }},
    // Config-like / markup languages — the generic node-type map handles them
    {"html",           {}},
    {"xml",            {}},
    {"css",            {}},
    {"yaml",           {}},
    {"markdown",       {}},
    {"ini",            {}},
    {"gitconfig",      {}},
    {"gitignore",      {}},
    {"gitattributes",  {}},
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
    {"rust", {
        "true", "false",
        // primitive types
        "i8", "i16", "i32", "i64", "i128", "isize",
        "u8", "u16", "u32", "u64", "u128", "usize",
        "f32", "f64", "bool", "char", "str",
        // common stdlib types
        "String", "Vec", "Option", "Result", "Box", "Rc", "Arc",
        "Some", "None", "Ok", "Err",
    }},
    {"go", {
        "true", "false", "nil", "iota",
        "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64", "uintptr",
        "float32", "float64", "complex64", "complex128",
        "byte", "rune", "string", "bool", "error",
        "make", "new", "len", "cap", "append", "copy", "delete",
        "close", "panic", "recover", "print", "println",
    }},
    {"java", {
        "true", "false", "null",
        "int", "long", "short", "byte", "char", "float", "double", "boolean",
        "String", "Object",
    }},
    {"c-sharp", {
        "true", "false", "null",
        "int", "long", "short", "byte", "char", "float", "double", "decimal",
        "bool", "string", "object", "uint", "ulong", "ushort", "sbyte",
        "void",
    }},
    {"ruby", {
        "true", "false", "nil", "self",
        "__FILE__", "__LINE__", "__dir__",
    }},
    {"php", {
        "true", "false", "null", "TRUE", "FALSE", "NULL",
        "int", "float", "string", "bool", "array", "object", "callable",
        "iterable", "void", "mixed", "never",
    }},
    {"lua", {
        "true", "false", "nil",
    }},
    {"bash", {
        "true", "false",
    }},
    {"powershell", {
        "$true", "$false", "$null",
        "string", "int", "double", "bool", "array", "hashtable", "object",
    }},
    {"vim", {
        "v:true", "v:false", "v:null", "v:none",
    }},
    {"sql", {
        "true", "false", "null",
    }},
    // Languages without meaningful type-keywords
    {"dockerfile",    {}},
    {"cmake",         {}},
    {"html",          {}},
    {"xml",           {}},
    {"css",           {}},
    {"yaml",          {}},
    {"markdown",      {}},
    {"ini",           {}},
    {"gitconfig",     {}},
    {"gitignore",     {}},
    {"gitattributes", {}},
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
