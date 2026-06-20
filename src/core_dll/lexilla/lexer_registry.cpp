#include "core_dll/lexilla/lexer_registry.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

// SCE_* style constants for the few lexers that need explicit overrides.
#if defined(_MSC_VER)
#  pragma warning(push, 3)
#endif
#include "SciLexer.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

namespace wlx::core::lexilla {

namespace {

// ---- Keyword lists -------------------------------------------------------
// Word list 0 = keywords (-> "keyword"); word list 1 = type-ish names that the
// cpp-family override maps SCE_C_WORD2 -> "type". Lists are solid but not
// exhaustive; missing keywords simply render as plain identifiers and can be
// extended without code changes elsewhere.

// .c and .h also route here, so this includes C keywords.
constexpr const char* kCppKeywords =
    "alignas alignof and and_eq asm auto bool break case catch char char8_t "
    "char16_t char32_t class compl concept const consteval constexpr constinit "
    "const_cast continue co_await co_return co_yield decltype default delete do "
    "double dynamic_cast else enum explicit export extern false float for friend "
    "goto if inline int long mutable namespace new noexcept not not_eq nullptr "
    "operator or or_eq private protected public register reinterpret_cast "
    "requires return short signed sizeof static static_assert static_cast struct "
    "switch template this thread_local throw true try typedef typeid typename "
    "union unsigned using virtual void volatile wchar_t while xor xor_eq "
    "restrict _Bool _Complex _Atomic _Generic _Noreturn _Static_assert";
constexpr const char* kCppTypes =
    "size_t ptrdiff_t intptr_t uintptr_t int8_t int16_t int32_t int64_t uint8_t "
    "uint16_t uint32_t uint64_t string wstring string_view vector map "
    "unordered_map set unordered_set array pair tuple optional FILE";

constexpr const char* kJsKeywords =
    "async await break case catch class const continue debugger default delete "
    "do else export extends false finally for function get if import in "
    "instanceof let new null of return set static super switch this throw true "
    "try typeof undefined var void while with yield";
constexpr const char* kJsTypes =
    "Array Boolean Date Error JSON Map Math Number Object Promise RegExp Set "
    "String Symbol WeakMap WeakSet";

constexpr const char* kTsKeywords =
    "abstract any as asserts async await boolean break case catch class const "
    "continue debugger declare default delete do else enum export extends false "
    "finally for from function get if implements import in infer instanceof "
    "interface is keyof let module namespace never new null number object of "
    "private protected public readonly return set static string super switch "
    "symbol this throw true try type typeof undefined unique unknown var void "
    "while with yield";
constexpr const char* kTsTypes =
    "Array Boolean Date Error JSON Map Math Number Object Promise ReadonlyArray "
    "Record Partial Required Readonly Pick Omit RegExp Set String Symbol";

constexpr const char* kJavaKeywords =
    "abstract assert boolean break byte case catch char class const continue "
    "default do double else enum extends final finally float for goto if "
    "implements import instanceof int interface long native new package private "
    "protected public return short static strictfp super switch synchronized "
    "this throw throws transient try void volatile while true false null var "
    "record sealed permits yield";
constexpr const char* kJavaTypes =
    "String Object Integer Long Double Float Boolean Character Byte Short List "
    "Map Set Collection Optional Stream";

constexpr const char* kCsKeywords =
    "abstract as base bool break byte case catch char checked class const "
    "continue decimal default delegate do double else enum event explicit "
    "extern false finally fixed float for foreach goto if implicit in int "
    "interface internal is lock long namespace new null object operator out "
    "override params private protected public readonly ref return sbyte sealed "
    "short sizeof stackalloc static string struct switch this throw true try "
    "typeof uint ulong unchecked unsafe ushort using var virtual void volatile "
    "while async await dynamic nameof when yield record init";
constexpr const char* kCsTypes =
    "String Object Int32 Int64 Boolean List Dictionary IEnumerable Task Nullable "
    "Console Convert Math";

constexpr const char* kPythonKeywords =
    "False None True and as assert async await break class continue def del elif "
    "else except finally for from global if import in is lambda nonlocal not or "
    "pass raise return try while with yield match case";

constexpr const char* kBashKeywords =
    "if then else elif fi case esac for select while until do done in function "
    "time coproc break continue return exit export local readonly declare typeset "
    "unset shift eval exec source alias set test";

constexpr const char* kRustKeywords =
    "as async await break const continue crate dyn else enum extern false fn for "
    "if impl in let loop match mod move mut pub ref return self Self static struct "
    "super trait true type unsafe use where while macro_rules union";
constexpr const char* kRustTypes =
    "i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str "
    "String Vec Option Result Box Rc Arc HashMap BTreeMap";

constexpr const char* kLuaKeywords =
    "and break do else elseif end false for function goto if in local nil not or "
    "repeat return then true until while";

constexpr const char* kSqlKeywords =
    "select from where insert into update delete create drop alter table index "
    "view join inner outer left right full on group by order having union all "
    "distinct as and or not null is in between like exists case when then else "
    "end limit offset primary key foreign references default constraint unique "
    "check cascade begin commit rollback transaction grant revoke with values "
    "set add column";
constexpr const char* kSqlTypes =
    "int integer smallint bigint decimal numeric float real double char varchar "
    "nvarchar text date time timestamp datetime boolean bool blob serial uuid";

constexpr const char* kPowerShellKeywords =
    "begin break catch class continue data define do dynamicparam else elseif end "
    "enum exit filter finally for foreach from function hidden if in param process "
    "return static switch throw trap try until using while";

constexpr const char* kPhpKeywords =
    "abstract and array as break callable case catch class clone const continue "
    "declare default do echo else elseif empty enddeclare endfor endforeach endif "
    "endswitch endwhile enum extends final finally fn for foreach function global "
    "goto if implements include include_once instanceof insteadof interface isset "
    "list match namespace new or print private protected public readonly require "
    "require_once return static switch throw trait try unset use var while xor "
    "yield true false null";

constexpr const char* kCmakeCommands =
    "add_executable add_library add_subdirectory add_custom_command "
    "add_custom_target add_dependencies add_definitions include find_package "
    "find_library find_path set unset list string file foreach endforeach while "
    "endwhile function endfunction macro endmacro if elseif else endif project "
    "option message install target_link_libraries target_include_directories "
    "target_compile_definitions target_compile_options target_compile_features "
    "target_sources cmake_minimum_required cmake_policy get_property set_property "
    "set_target_properties get_target_property configure_file enable_testing "
    "add_test source_group execute_process";
constexpr const char* kCmakeParameters =
    "PRIVATE PUBLIC INTERFACE STATIC SHARED MODULE REQUIRED QUIET EXACT AND OR "
    "NOT COMMAND TARGET EXISTS DEFINED STREQUAL MATCHES VERSION_LESS "
    "VERSION_GREATER WIN32 APPLE UNIX ON OFF TRUE FALSE";

// Properties shared by the cpp lexer (used for C/C++/JS/TS/Java/C#).
const std::vector<std::pair<std::string, std::string>> kCppProps = {
    {"styling.within.preprocessor", "1"},
    {"lexer.cpp.track.preprocessor", "1"},
    {"lexer.cpp.escape.sequence", "1"},
};

LexerSpec cpp_like(const char* keywords, const char* types) {
    LexerSpec s;
    s.lexer_name = "cpp";
    s.word_lists = {keywords, types};
    s.properties = kCppProps;
    // The cpp lexer tags WORD2/GLOBALCLASS as plain "identifier"; upgrade them.
    s.style_scopes = {{SCE_C_WORD2, "type"}, {SCE_C_GLOBALCLASS, "type"}};
    return s;
}

LexerSpec python_spec() {
    LexerSpec s;
    s.lexer_name = "python";
    s.word_lists = {kPythonKeywords};
    return s;  // tag-driven: comment/string/number/keyword/operator all covered
}

LexerSpec bash_spec() {
    LexerSpec s;
    s.lexer_name = "bash";
    s.word_lists = {kBashKeywords};
    return s;
}

LexerSpec json_spec() {
    LexerSpec s;
    s.lexer_name = "json";
    s.word_lists = {"true false null"};
    // LexJSON has no lexicalClasses (empty tags), so map styles explicitly.
    s.style_scopes = {
        {SCE_JSON_NUMBER, "constant.numeric"},
        {SCE_JSON_STRING, "string"},
        {SCE_JSON_STRINGEOL, "string"},
        {SCE_JSON_PROPERTYNAME, "string"},
        {SCE_JSON_ESCAPESEQUENCE, "constant.character.escape"},
        {SCE_JSON_LINECOMMENT, "comment"},
        {SCE_JSON_BLOCKCOMMENT, "comment"},
        {SCE_JSON_OPERATOR, "operator"},
        {SCE_JSON_KEYWORD, "constant.builtin"},
        {SCE_JSON_LDKEYWORD, "keyword"},
        {SCE_JSON_URI, "string"},
        {SCE_JSON_COMPACTIRI, "string"},
        {SCE_JSON_ERROR, ""},
    };
    return s;
}

LexerSpec rust_spec() {
    LexerSpec s;
    s.lexer_name = "rust";
    s.word_lists = {kRustKeywords, kRustTypes};
    return s;  // tag-driven (LexRust has lexicalClasses)
}

LexerSpec lua_spec() {
    LexerSpec s;
    s.lexer_name = "lua";
    s.word_lists = {kLuaKeywords};
    return s;  // tag-driven (LexLua has lexicalClasses)
}

LexerSpec css_spec() {
    LexerSpec s;
    s.lexer_name = "css";
    // LexCSS has no lexicalClasses — map styles explicitly.
    s.style_scopes = {
        {SCE_CSS_COMMENT, "comment"},
        {SCE_CSS_DOUBLESTRING, "string"},
        {SCE_CSS_SINGLESTRING, "string"},
        {SCE_CSS_OPERATOR, "operator"},
        {SCE_CSS_VALUE, "constant"},
        {SCE_CSS_TAG, "type"},
        {SCE_CSS_CLASS, "keyword"},
        {SCE_CSS_ID, "keyword"},
        {SCE_CSS_PSEUDOCLASS, "keyword"},
        {SCE_CSS_UNKNOWN_PSEUDOCLASS, "keyword"},
        {SCE_CSS_PSEUDOELEMENT, "keyword"},
        {SCE_CSS_IDENTIFIER, "keyword"},
        {SCE_CSS_IDENTIFIER2, "keyword"},
        {SCE_CSS_IDENTIFIER3, "keyword"},
        {SCE_CSS_UNKNOWN_IDENTIFIER, "keyword"},
        {SCE_CSS_ATTRIBUTE, "keyword"},
        {SCE_CSS_IMPORTANT, "keyword.directive"},
        {SCE_CSS_DIRECTIVE, "keyword.directive"},
        {SCE_CSS_VARIABLE, "constant"},
    };
    return s;
}

LexerSpec yaml_spec() {
    LexerSpec s;
    s.lexer_name = "yaml";
    s.word_lists = {"true false null True False Null TRUE FALSE NULL yes no on off"};
    // LexYAML has no lexicalClasses — map styles explicitly.
    s.style_scopes = {
        {SCE_YAML_COMMENT, "comment"},
        {SCE_YAML_IDENTIFIER, "keyword"},   // mapping keys
        {SCE_YAML_KEYWORD, "constant.builtin"},
        {SCE_YAML_NUMBER, "constant.numeric"},
        {SCE_YAML_REFERENCE, "constant"},
        {SCE_YAML_DOCUMENT, "operator"},
        {SCE_YAML_TEXT, "string"},
        {SCE_YAML_OPERATOR, "operator"},
        {SCE_YAML_ERROR, ""},
    };
    return s;
}

LexerSpec toml_spec() {
    LexerSpec s;
    s.lexer_name = "toml";
    s.word_lists = {"true false"};
    // LexTOML has no lexicalClasses — map styles explicitly.
    s.style_scopes = {
        {SCE_TOML_COMMENT, "comment"},
        {SCE_TOML_KEYWORD, "constant.builtin"},
        {SCE_TOML_NUMBER, "constant.numeric"},
        {SCE_TOML_TABLE, "type"},
        {SCE_TOML_KEY, "keyword"},
        {SCE_TOML_IDENTIFIER, "keyword"},
        {SCE_TOML_OPERATOR, "operator"},
        {SCE_TOML_STRING_SQ, "string"},
        {SCE_TOML_STRING_DQ, "string"},
        {SCE_TOML_TRIPLE_STRING_SQ, "string"},
        {SCE_TOML_TRIPLE_STRING_DQ, "string"},
        {SCE_TOML_ESCAPECHAR, "constant.character.escape"},
        {SCE_TOML_DATETIME, "constant"},
        {SCE_TOML_STRINGEOL, "string"},
        {SCE_TOML_ERROR, ""},
    };
    return s;
}

LexerSpec sql_spec() {
    LexerSpec s;
    s.lexer_name = "sql";
    s.word_lists = {kSqlKeywords, kSqlTypes};
    // LexSQL has no lexicalClasses — map styles explicitly.
    s.style_scopes = {
        {SCE_SQL_COMMENT, "comment"},
        {SCE_SQL_COMMENTLINE, "comment"},
        {SCE_SQL_COMMENTDOC, "comment"},
        {SCE_SQL_COMMENTLINEDOC, "comment"},
        {SCE_SQL_COMMENTDOCKEYWORD, "comment"},
        {SCE_SQL_NUMBER, "constant.numeric"},
        {SCE_SQL_WORD, "keyword"},
        {SCE_SQL_WORD2, "type"},
        {SCE_SQL_STRING, "string"},
        {SCE_SQL_CHARACTER, "string"},
        {SCE_SQL_QUOTEDIDENTIFIER, "string"},
        {SCE_SQL_OPERATOR, "operator"},
    };
    return s;
}

LexerSpec powershell_spec() {
    LexerSpec s;
    s.lexer_name = "powershell";
    s.word_lists = {kPowerShellKeywords};
    // LexPowerShell has no lexicalClasses — map styles explicitly. (Cmdlet/alias
    // word lists not supplied yet, so those tokens stay default for now.)
    s.style_scopes = {
        {SCE_POWERSHELL_COMMENT, "comment"},
        {SCE_POWERSHELL_COMMENTSTREAM, "comment"},
        {SCE_POWERSHELL_COMMENTDOCKEYWORD, "comment"},
        {SCE_POWERSHELL_STRING, "string"},
        {SCE_POWERSHELL_CHARACTER, "string"},
        {SCE_POWERSHELL_HERE_STRING, "string"},
        {SCE_POWERSHELL_HERE_CHARACTER, "string"},
        {SCE_POWERSHELL_NUMBER, "constant.numeric"},
        {SCE_POWERSHELL_VARIABLE, "variable"},
        {SCE_POWERSHELL_OPERATOR, "operator"},
        {SCE_POWERSHELL_KEYWORD, "keyword"},
        {SCE_POWERSHELL_CMDLET, "function"},
        {SCE_POWERSHELL_ALIAS, "function"},
        {SCE_POWERSHELL_FUNCTION, "function"},
    };
    return s;
}

LexerSpec html_spec() {
    LexerSpec s;
    s.lexer_name = "hypertext";
    return s;  // tag-driven (LexHTML has lexicalClasses); tag recognition is structural
}

LexerSpec xml_spec() {
    LexerSpec s;
    s.lexer_name = "xml";
    return s;  // tag-driven (LexHTML/XML lexicalClasses)
}

LexerSpec php_spec() {
    LexerSpec s;
    s.lexer_name = "phpscript";
    // LexHTML reads PHP keywords from word-list index 4 (phpscriptWordListDesc).
    s.word_lists = {"", "", "", "", kPhpKeywords};
    return s;  // tag-driven; comments/strings/numbers color via tags regardless
}

LexerSpec cmake_spec() {
    LexerSpec s;
    s.lexer_name = "cmake";
    s.word_lists = {kCmakeCommands, kCmakeParameters};
    // LexCmake has no lexicalClasses — map styles explicitly.
    s.style_scopes = {
        {SCE_CMAKE_COMMENT, "comment"},
        {SCE_CMAKE_STRINGDQ, "string"},
        {SCE_CMAKE_STRINGLQ, "string"},
        {SCE_CMAKE_STRINGRQ, "string"},
        {SCE_CMAKE_STRINGVAR, "string"},
        {SCE_CMAKE_COMMANDS, "keyword"},
        {SCE_CMAKE_PARAMETERS, "keyword"},
        {SCE_CMAKE_VARIABLE, "variable"},
        {SCE_CMAKE_NUMBER, "constant.numeric"},
        {SCE_CMAKE_WHILEDEF, "keyword"},
        {SCE_CMAKE_FOREACHDEF, "keyword"},
        {SCE_CMAKE_IFDEFINEDEF, "keyword"},
        {SCE_CMAKE_MACRODEF, "keyword"},
        {SCE_CMAKE_USERDEFINED, "function"},
    };
    return s;
}

const std::unordered_map<std::string, LexerSpec>& registry() {
    static const std::unordered_map<std::string, LexerSpec> m = [] {
        std::unordered_map<std::string, LexerSpec> r;
        r.emplace("cpp", cpp_like(kCppKeywords, kCppTypes));
        r.emplace("javascript", cpp_like(kJsKeywords, kJsTypes));
        r.emplace("typescript", cpp_like(kTsKeywords, kTsTypes));
        r.emplace("java", cpp_like(kJavaKeywords, kJavaTypes));
        r.emplace("c-sharp", cpp_like(kCsKeywords, kCsTypes));
        r.emplace("python", python_spec());
        r.emplace("bash", bash_spec());
        r.emplace("json", json_spec());
        r.emplace("rust", rust_spec());
        r.emplace("lua", lua_spec());
        r.emplace("css", css_spec());
        r.emplace("yaml", yaml_spec());
        r.emplace("toml", toml_spec());
        r.emplace("sql", sql_spec());
        r.emplace("powershell", powershell_spec());
        r.emplace("html", html_spec());
        r.emplace("xml", xml_spec());
        r.emplace("php", php_spec());
        r.emplace("cmake", cmake_spec());
        return r;
    }();
    return m;
}

}  // namespace

const LexerSpec* lexer_spec_for(std::string_view language) {
    const auto& m = registry();
    const auto it = m.find(std::string(language));
    return it == m.end() ? nullptr : &it->second;
}

std::vector<std::string> registered_languages() {
    const auto& m = registry();
    std::vector<std::string> out;
    out.reserve(m.size());
    for (const auto& [lang, spec] : m) out.push_back(lang);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace wlx::core::lexilla
