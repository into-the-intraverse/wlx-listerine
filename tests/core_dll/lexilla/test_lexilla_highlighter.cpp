#include <doctest/doctest.h>

#include "core_dll/lexilla/lexilla_highlighter.h"
#include "core_dll/lexilla/lexer_registry.h"
#include "core_dll/theme/helix_theme.h"
#include "wlx_core/text_modifier.h"

#include <string>
#include <vector>

using namespace wlx::core;

namespace {
// First emitted span covering byte `offset`, or nullptr.
const colorizer::ColorSpan* span_covering(
    const std::vector<colorizer::ColorSpan>& spans, uint32_t offset) {
    for (const auto& s : spans)
        if (offset >= s.start && offset < s.start + s.length) return &s;
    return nullptr;
}
}  // namespace

TEST_CASE("lexilla highlighter: cpp snippet yields sane spans") {
    const auto thm = theme::HelixTheme::make_default(/*dark_mode=*/true);
    //                       0     6  9    14
    const std::string src = "// hi\nint x = 42;\n";
    const auto spans = lexilla::highlight(*lexilla::lexer_spec_for("cpp"), src, thm);

    REQUIRE(spans.size() >= 3);

    const auto* comment = span_covering(spans, 0);    // "// hi"
    const auto* keyword = span_covering(spans, 6);     // "int"
    const auto* number = span_covering(spans, 14);     // "42"

    REQUIRE(comment != nullptr);
    REQUIRE(keyword != nullptr);
    REQUIRE(number != nullptr);

    // Distinct, non-zero theme colors prove style -> scope -> theme ran.
    CHECK(comment->color != keyword->color);
    CHECK(keyword->color != number->color);
    CHECK(comment->color != 0u);

    // make_default marks comments italic.
    CHECK((comment->modifiers & MOD_ITALIC) != 0);

    // Spans are ordered and non-overlapping.
    for (size_t i = 1; i < spans.size(); ++i)
        CHECK(spans[i].start >= spans[i - 1].start + spans[i - 1].length);
}

TEST_CASE("lexilla highlighter: range scoping emits spans overlapping the viewport") {
    const auto thm = theme::HelixTheme::make_default(true);
    //                       0       7
    const std::string src = "int a;\nint b;\n";
    const auto spans = lexilla::highlight(*lexilla::lexer_spec_for("cpp"), src, thm, 7, 13);

    // Spans overlap [7,13) with FULL extent (not clipped) — same contract as
    // tree-sitter's highlight_range, relied on by SpanTable::append_chunk.
    for (const auto& s : spans) {
        CHECK(s.start < 13u);
        CHECK(s.start + s.length > 7u);
    }
    // The second line's `int` keyword is present.
    CHECK(span_covering(spans, 7) != nullptr);
}

TEST_CASE("lexilla highlighter: unknown lexer name returns no spans") {
    const auto thm = theme::HelixTheme::make_default(true);
    lexilla::LexerSpec bogus;
    bogus.lexer_name = "no-such-lexer-xyz";
    bogus.style_scopes = {{0, "comment"}};
    CHECK(lexilla::highlight(bogus, "int x;", thm).empty());
}
