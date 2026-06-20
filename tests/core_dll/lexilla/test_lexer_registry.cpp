#include <doctest/doctest.h>

#include "core_dll/lexilla/lexilla_highlighter.h"
#include "core_dll/lexilla/lexer_registry.h"
#include "core_dll/theme/helix_theme.h"

#include <string>
#include <vector>

using namespace wlx::core;

namespace {
const colorizer::ColorSpan* span_covering(
    const std::vector<colorizer::ColorSpan>& spans, uint32_t off) {
    for (const auto& s : spans)
        if (off >= s.start && off < s.start + s.length) return &s;
    return nullptr;
}
}  // namespace

TEST_CASE("lexer registry: covered languages produce spans") {
    const auto thm = theme::HelixTheme::make_default(true);
    struct Case { const char* lang; std::string src; };
    const Case cases[] = {
        {"cpp",        "int x = 1; // c\n"},
        {"javascript", "function f(){ return 1; } // c\n"},
        {"typescript", "const x: number = 1; // c\n"},
        {"java",       "class A { int x = 1; } // c\n"},
        {"c-sharp",    "class A { int x = 1; } // c\n"},
        {"python",     "def f():\n    return 1  # c\n"},
        {"bash",       "if true; then echo hi; fi # c\n"},
        {"json",       "{\"a\": 42, \"b\": \"hi\"}\n"},
        {"rust",       "fn main() { let x = 1; } // c\n"},
        {"lua",        "local x = 1 -- c\n"},
        {"css",        "a { color: red; } /* c */\n"},
        {"yaml",       "key: 1 # c\n"},
        {"toml",       "[t]\nkey = 1 # c\n"},
        {"sql",        "SELECT * FROM t -- c\n"},
        {"powershell", "if ($x) { 1 } # c\n"},
        {"html",       "<div class=\"x\">hi</div><!-- c -->\n"},
        {"xml",        "<a x=\"1\">t</a>\n"},
        {"php",        "<?php $x = 1; // c\n?>\n"},
        {"cmake",      "# c\nadd_library(foo STATIC a.c)\nif(X)\nendif()\n"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.lang);
        const auto* spec = lexilla::lexer_spec_for(c.lang);
        REQUIRE(spec != nullptr);
        const auto spans = lexilla::highlight(*spec, c.src, thm);
        CHECK(!spans.empty());
    }
}

TEST_CASE("lexer registry: keyword/comment/number coloring lands") {
    const auto thm = theme::HelixTheme::make_default(true);

    // python: 'def' keyword at offset 0.
    {
        const std::string src = "def f():\n    return 1  # c\n";
        const auto spans = lexilla::highlight(*lexilla::lexer_spec_for("python"), src, thm);
        CHECK(span_covering(spans, 0) != nullptr);
    }
    // json: property "a" (offset 1) and number 42 (offset 6) both colored.
    {
        const std::string src = "{\"a\": 42}";
        const auto spans = lexilla::highlight(*lexilla::lexer_spec_for("json"), src, thm);
        CHECK(span_covering(spans, 1) != nullptr);
        CHECK(span_covering(spans, 6) != nullptr);
    }
}

TEST_CASE("lexer registry: gap languages have no spec (plain text)") {
    for (const char* lang : {"go", "vim", "dockerfile", "gitignore",
                             "git-config", "git_rebase", "gitattributes"}) {
        CAPTURE(lang);
        CHECK(lexilla::lexer_spec_for(lang) == nullptr);
    }
}
