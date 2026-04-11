#include <doctest/doctest.h>
#include "scope.h"

// ===========================================================================
// capture_to_scope — exact matches
// ===========================================================================

TEST_CASE("capture_to_scope: keyword -> Keyword") {
    CHECK(capture_to_scope("keyword") == Scope::Keyword);
}

TEST_CASE("capture_to_scope: function -> Function") {
    CHECK(capture_to_scope("function") == Scope::Function);
}

TEST_CASE("capture_to_scope: function.builtin -> FunctionBuiltin") {
    CHECK(capture_to_scope("function.builtin") == Scope::FunctionBuiltin);
}

TEST_CASE("capture_to_scope: function.call -> FunctionCall") {
    CHECK(capture_to_scope("function.call") == Scope::FunctionCall);
}

TEST_CASE("capture_to_scope: function.method.call -> FunctionCall") {
    CHECK(capture_to_scope("function.method.call") == Scope::FunctionCall);
}

TEST_CASE("capture_to_scope: string -> String") {
    CHECK(capture_to_scope("string") == Scope::String);
}

TEST_CASE("capture_to_scope: string.escape -> StringEscape") {
    CHECK(capture_to_scope("string.escape") == Scope::StringEscape);
}

TEST_CASE("capture_to_scope: string.special -> StringSpecial") {
    CHECK(capture_to_scope("string.special") == Scope::StringSpecial);
}

TEST_CASE("capture_to_scope: string.regexp -> StringSpecial") {
    CHECK(capture_to_scope("string.regexp") == Scope::StringSpecial);
}

TEST_CASE("capture_to_scope: character -> String") {
    CHECK(capture_to_scope("character") == Scope::String);
}

TEST_CASE("capture_to_scope: character.special -> StringEscape") {
    CHECK(capture_to_scope("character.special") == Scope::StringEscape);
}

TEST_CASE("capture_to_scope: number -> Number") {
    CHECK(capture_to_scope("number") == Scope::Number);
}

TEST_CASE("capture_to_scope: number.float -> Number") {
    CHECK(capture_to_scope("number.float") == Scope::Number);
}

TEST_CASE("capture_to_scope: boolean -> Boolean") {
    CHECK(capture_to_scope("boolean") == Scope::Boolean);
}

TEST_CASE("capture_to_scope: comment -> Comment") {
    CHECK(capture_to_scope("comment") == Scope::Comment);
}

TEST_CASE("capture_to_scope: operator -> Operator") {
    CHECK(capture_to_scope("operator") == Scope::Operator);
}

TEST_CASE("capture_to_scope: type -> Type") {
    CHECK(capture_to_scope("type") == Scope::Type);
}

TEST_CASE("capture_to_scope: type.builtin -> Keyword2") {
    CHECK(capture_to_scope("type.builtin") == Scope::Keyword2);
}

TEST_CASE("capture_to_scope: type.definition -> Type") {
    CHECK(capture_to_scope("type.definition") == Scope::Type);
}

TEST_CASE("capture_to_scope: constant -> Variable") {
    CHECK(capture_to_scope("constant") == Scope::Variable);
}

TEST_CASE("capture_to_scope: constant.builtin -> ConstantBuiltin") {
    CHECK(capture_to_scope("constant.builtin") == Scope::ConstantBuiltin);
}

TEST_CASE("capture_to_scope: constant.macro -> Variable") {
    CHECK(capture_to_scope("constant.macro") == Scope::Variable);
}

TEST_CASE("capture_to_scope: constructor -> Constructor") {
    CHECK(capture_to_scope("constructor") == Scope::Constructor);
}

TEST_CASE("capture_to_scope: module -> Namespace") {
    CHECK(capture_to_scope("module") == Scope::Namespace);
}

TEST_CASE("capture_to_scope: module.builtin -> Namespace") {
    CHECK(capture_to_scope("module.builtin") == Scope::Namespace);
}

TEST_CASE("capture_to_scope: namespace -> Namespace") {
    CHECK(capture_to_scope("namespace") == Scope::Namespace);
}

TEST_CASE("capture_to_scope: variable -> Variable") {
    CHECK(capture_to_scope("variable") == Scope::Variable);
}

TEST_CASE("capture_to_scope: punctuation -> Punctuation") {
    CHECK(capture_to_scope("punctuation") == Scope::Punctuation);
}

TEST_CASE("capture_to_scope: tag -> Tag") {
    CHECK(capture_to_scope("tag") == Scope::Tag);
}

TEST_CASE("capture_to_scope: tag.builtin -> Tag") {
    CHECK(capture_to_scope("tag.builtin") == Scope::Tag);
}

TEST_CASE("capture_to_scope: tag.delimiter -> TagDelimiter") {
    CHECK(capture_to_scope("tag.delimiter") == Scope::TagDelimiter);
}

TEST_CASE("capture_to_scope: tag.attribute -> Attribute") {
    CHECK(capture_to_scope("tag.attribute") == Scope::Attribute);
}

TEST_CASE("capture_to_scope: attribute -> Attribute") {
    CHECK(capture_to_scope("attribute") == Scope::Attribute);
}

TEST_CASE("capture_to_scope: attribute.builtin -> Attribute") {
    CHECK(capture_to_scope("attribute.builtin") == Scope::Attribute);
}

TEST_CASE("capture_to_scope: property -> Property") {
    CHECK(capture_to_scope("property") == Scope::Property);
}

TEST_CASE("capture_to_scope: label -> Label") {
    CHECK(capture_to_scope("label") == Scope::Label);
}

TEST_CASE("capture_to_scope: preproc -> Preprocessor") {
    CHECK(capture_to_scope("preproc") == Scope::Preprocessor);
}

TEST_CASE("capture_to_scope: keyword.directive -> Preprocessor") {
    CHECK(capture_to_scope("keyword.directive") == Scope::Preprocessor);
}

TEST_CASE("capture_to_scope: keyword.directive.define -> Preprocessor") {
    CHECK(capture_to_scope("keyword.directive.define") == Scope::Preprocessor);
}

// ===========================================================================
// capture_to_scope — prefix fallback
// ===========================================================================

TEST_CASE("capture_to_scope: keyword.return falls back to Keyword via prefix") {
    CHECK(capture_to_scope("keyword.return") == Scope::Keyword);
}

TEST_CASE("capture_to_scope: keyword.control falls back to Keyword via prefix") {
    CHECK(capture_to_scope("keyword.control") == Scope::Keyword);
}

TEST_CASE("capture_to_scope: string.documentation falls back to String via prefix") {
    CHECK(capture_to_scope("string.documentation") == Scope::String);
}

TEST_CASE("capture_to_scope: comment.block falls back to Comment via prefix") {
    CHECK(capture_to_scope("comment.block") == Scope::Comment);
}

TEST_CASE("capture_to_scope: comment.line falls back to Comment via prefix") {
    CHECK(capture_to_scope("comment.line") == Scope::Comment);
}

TEST_CASE("capture_to_scope: variable.parameter falls back to Variable via prefix") {
    CHECK(capture_to_scope("variable.parameter") == Scope::Variable);
}

TEST_CASE("capture_to_scope: variable.builtin falls back to Variable via prefix") {
    CHECK(capture_to_scope("variable.builtin") == Scope::Variable);
}

TEST_CASE("capture_to_scope: punctuation.bracket falls back to Punctuation via prefix") {
    CHECK(capture_to_scope("punctuation.bracket") == Scope::Punctuation);
}

TEST_CASE("capture_to_scope: punctuation.delimiter falls back to Punctuation via prefix") {
    CHECK(capture_to_scope("punctuation.delimiter") == Scope::Punctuation);
}

TEST_CASE("capture_to_scope: type.qualifier falls back to Type via prefix") {
    CHECK(capture_to_scope("type.qualifier") == Scope::Type);
}

TEST_CASE("capture_to_scope: function.macro falls back to Function via prefix") {
    CHECK(capture_to_scope("function.macro") == Scope::Function);
}

// ===========================================================================
// capture_to_scope — unknown / edge cases
// ===========================================================================

TEST_CASE("capture_to_scope: unknown capture returns Plain") {
    CHECK(capture_to_scope("totally_unknown") == Scope::Plain);
}

TEST_CASE("capture_to_scope: empty string returns Plain") {
    CHECK(capture_to_scope("") == Scope::Plain);
}

TEST_CASE("capture_to_scope: dotted unknown returns Plain") {
    CHECK(capture_to_scope("foo.bar.baz") == Scope::Plain);
}

// ===========================================================================
// scope_to_color — all 25 scopes with light palette
// ===========================================================================

TEST_CASE("scope_to_color: all 25 scopes map to correct light palette colors") {
    SyntaxPalette pal = SyntaxPalette::defaults(false);

    CHECK(scope_to_color(Scope::Keyword, pal)         == pal.keyword);
    CHECK(scope_to_color(Scope::Keyword2, pal)        == pal.keyword2);
    CHECK(scope_to_color(Scope::Function, pal)        == pal.function);
    CHECK(scope_to_color(Scope::String, pal)          == pal.string);
    CHECK(scope_to_color(Scope::Number, pal)          == pal.number);
    CHECK(scope_to_color(Scope::Comment, pal)         == pal.comment);
    CHECK(scope_to_color(Scope::Operator, pal)        == pal.op);
    CHECK(scope_to_color(Scope::Type, pal)            == pal.type);
    CHECK(scope_to_color(Scope::Preprocessor, pal)    == pal.preprocessor);
    CHECK(scope_to_color(Scope::Namespace, pal)       == pal.ns);
    CHECK(scope_to_color(Scope::Variable, pal)        == pal.variable);
    CHECK(scope_to_color(Scope::Punctuation, pal)     == pal.punctuation);
    CHECK(scope_to_color(Scope::Plain, pal)           == pal.plain);
    CHECK(scope_to_color(Scope::ConstantBuiltin, pal) == pal.constant_builtin);
    CHECK(scope_to_color(Scope::FunctionBuiltin, pal) == pal.function_builtin);
    CHECK(scope_to_color(Scope::FunctionCall, pal)    == pal.function_call);
    CHECK(scope_to_color(Scope::StringEscape, pal)    == pal.string_escape);
    CHECK(scope_to_color(Scope::StringSpecial, pal)   == pal.string_special);
    CHECK(scope_to_color(Scope::Boolean, pal)         == pal.boolean_lit);
    CHECK(scope_to_color(Scope::Tag, pal)             == pal.tag);
    CHECK(scope_to_color(Scope::TagDelimiter, pal)    == pal.tag_delimiter);
    CHECK(scope_to_color(Scope::Attribute, pal)       == pal.attribute);
    CHECK(scope_to_color(Scope::Constructor, pal)     == pal.constructor);
    CHECK(scope_to_color(Scope::Property, pal)        == pal.property);
    CHECK(scope_to_color(Scope::Label, pal)           == pal.label);
}

// ===========================================================================
// scope_to_color — all 25 scopes with dark palette
// ===========================================================================

TEST_CASE("scope_to_color: all 25 scopes map to correct dark palette colors") {
    SyntaxPalette pal = SyntaxPalette::defaults(true);

    CHECK(scope_to_color(Scope::Keyword, pal)         == pal.keyword);
    CHECK(scope_to_color(Scope::Keyword2, pal)        == pal.keyword2);
    CHECK(scope_to_color(Scope::Function, pal)        == pal.function);
    CHECK(scope_to_color(Scope::String, pal)          == pal.string);
    CHECK(scope_to_color(Scope::Number, pal)          == pal.number);
    CHECK(scope_to_color(Scope::Comment, pal)         == pal.comment);
    CHECK(scope_to_color(Scope::Operator, pal)        == pal.op);
    CHECK(scope_to_color(Scope::Type, pal)            == pal.type);
    CHECK(scope_to_color(Scope::Preprocessor, pal)    == pal.preprocessor);
    CHECK(scope_to_color(Scope::Namespace, pal)       == pal.ns);
    CHECK(scope_to_color(Scope::Variable, pal)        == pal.variable);
    CHECK(scope_to_color(Scope::Punctuation, pal)     == pal.punctuation);
    CHECK(scope_to_color(Scope::Plain, pal)           == pal.plain);
    CHECK(scope_to_color(Scope::ConstantBuiltin, pal) == pal.constant_builtin);
    CHECK(scope_to_color(Scope::FunctionBuiltin, pal) == pal.function_builtin);
    CHECK(scope_to_color(Scope::FunctionCall, pal)    == pal.function_call);
    CHECK(scope_to_color(Scope::StringEscape, pal)    == pal.string_escape);
    CHECK(scope_to_color(Scope::StringSpecial, pal)   == pal.string_special);
    CHECK(scope_to_color(Scope::Boolean, pal)         == pal.boolean_lit);
    CHECK(scope_to_color(Scope::Tag, pal)             == pal.tag);
    CHECK(scope_to_color(Scope::TagDelimiter, pal)    == pal.tag_delimiter);
    CHECK(scope_to_color(Scope::Attribute, pal)       == pal.attribute);
    CHECK(scope_to_color(Scope::Constructor, pal)     == pal.constructor);
    CHECK(scope_to_color(Scope::Property, pal)        == pal.property);
    CHECK(scope_to_color(Scope::Label, pal)           == pal.label);
}

// ===========================================================================
// SyntaxPalette::defaults — verify new fields have non-zero values
// ===========================================================================

TEST_CASE("SyntaxPalette::defaults light: new fields are non-zero") {
    SyntaxPalette pal = SyntaxPalette::defaults(false);
    CHECK(pal.constant_builtin != 0);
    CHECK(pal.function_builtin != 0);
    CHECK(pal.function_call    != 0);
    CHECK(pal.string_escape    != 0);
    CHECK(pal.string_special   != 0);
    CHECK(pal.boolean_lit      != 0);
    CHECK(pal.tag              != 0);
    CHECK(pal.tag_delimiter    != 0);
    CHECK(pal.attribute        != 0);
    CHECK(pal.constructor      != 0);
    CHECK(pal.property         != 0);
    CHECK(pal.label            != 0);
}

TEST_CASE("SyntaxPalette::defaults dark: new fields are non-zero") {
    SyntaxPalette pal = SyntaxPalette::defaults(true);
    CHECK(pal.constant_builtin != 0);
    CHECK(pal.function_builtin != 0);
    CHECK(pal.function_call    != 0);
    CHECK(pal.string_escape    != 0);
    CHECK(pal.string_special   != 0);
    CHECK(pal.boolean_lit      != 0);
    CHECK(pal.tag              != 0);
    CHECK(pal.tag_delimiter    != 0);
    CHECK(pal.attribute        != 0);
    CHECK(pal.constructor      != 0);
    CHECK(pal.property         != 0);
    CHECK(pal.label            != 0);
}

// ===========================================================================
// SyntaxPalette::defaults — spot-check specific color values
// ===========================================================================

TEST_CASE("SyntaxPalette::defaults light: specific new color values") {
    SyntaxPalette pal = SyntaxPalette::defaults(false);
    CHECK(pal.constant_builtin == 0x0000FF);
    CHECK(pal.function_builtin == 0x795E26);
    CHECK(pal.function_call    == 0x795E26);
    CHECK(pal.string_escape    == 0xEE0000);
    CHECK(pal.string_special   == 0x811F3F);
    CHECK(pal.boolean_lit      == 0x0000FF);
    CHECK(pal.tag              == 0x800000);
    CHECK(pal.tag_delimiter    == 0x800000);
    CHECK(pal.attribute        == 0xFF0000);
    CHECK(pal.constructor      == 0x267F99);
    CHECK(pal.property         == 0x001080);
    CHECK(pal.label            == 0x001080);
}

TEST_CASE("SyntaxPalette::defaults dark: specific new color values") {
    SyntaxPalette pal = SyntaxPalette::defaults(true);
    CHECK(pal.constant_builtin == 0x569CD6);
    CHECK(pal.function_builtin == 0xDCDCAA);
    CHECK(pal.function_call    == 0xDCDCAA);
    CHECK(pal.string_escape    == 0xD7BA7D);
    CHECK(pal.string_special   == 0xD16969);
    CHECK(pal.boolean_lit      == 0x569CD6);
    CHECK(pal.tag              == 0x569CD6);
    CHECK(pal.tag_delimiter    == 0x808080);
    CHECK(pal.attribute        == 0x9CDCFE);
    CHECK(pal.constructor      == 0x4EC9B0);
    CHECK(pal.property         == 0x9CDCFE);
    CHECK(pal.label            == 0x9CDCFE);
}
