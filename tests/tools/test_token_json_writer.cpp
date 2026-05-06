#include <doctest/doctest.h>

#include "tools/screenshot/token_json_writer.h"
#include "core_dll/colorizer/colorize_result.h"
#include "wlx_core/text_modifier.h"

#include <string>

using wlx::core::colorizer::ColorSpan;
using wlx::core::colorizer::ColorizeResult;
using wlx::tools::screenshot::TokenJsonWriter;
using wlx::tools::screenshot::TokenJsonOptions;

namespace {

ColorSpan span(uint32_t start, uint32_t length, uint32_t color, uint8_t mods) {
    ColorSpan s;
    s.start = start;
    s.length = length;
    s.color = color;
    s.modifiers = mods;
    return s;
}

TokenJsonOptions test_opts() {
    TokenJsonOptions opt;
    opt.source_name   = "synthetic.cpp";
    opt.language      = "cpp";
    opt.theme_name    = "test_dark";
    opt.config_hash   = "deadbeef";
    return opt;
}

}  // namespace

TEST_CASE("TokenJsonWriter: empty result produces empty tokens array") {
    ColorizeResult cr;
    std::string out = TokenJsonWriter::write(cr, /*source=*/"", test_opts());
    CHECK(out.find("\"token_count\": 0") != std::string::npos);
    CHECK(out.find("\"tokens\": []") != std::string::npos);
}

TEST_CASE("TokenJsonWriter: adjacent identical-style tokens collapse") {
    ColorizeResult cr;
    cr.spans = {
        span(0, 3, 0xFF7B72, MOD_BOLD),
        span(3, 3, 0xFF7B72, MOD_BOLD),
    };
    std::string out = TokenJsonWriter::write(cr, "abcdef", test_opts());
    CHECK(out.find("\"token_count\": 1") != std::string::npos);
    CHECK(out.find("\"len\": 6") != std::string::npos);
}

TEST_CASE("TokenJsonWriter: tokens are sorted by (line, col, -len)") {
    ColorizeResult cr;
    cr.spans = {
        span(10, 2, 0xAAAAAA, 0),
        span( 5, 3, 0xBBBBBB, 0),
        span( 5, 8, 0xCCCCCC, 0),
    };
    std::string out = TokenJsonWriter::write(cr, "0123456789ABCDEFGHIJ", test_opts());
    auto p1 = out.find("\"len\": 8");
    auto p2 = out.find("\"len\": 3");
    auto p3 = out.find("\"len\": 2");
    REQUIRE(p1 != std::string::npos);
    REQUIRE(p2 != std::string::npos);
    REQUIRE(p3 != std::string::npos);
    CHECK(p1 < p2);
    CHECK(p2 < p3);
}

TEST_CASE("TokenJsonWriter: modifiers are alphabetized lowercase") {
    ColorizeResult cr;
    cr.spans = { span(0, 4, 0xFFFFFF, MOD_UNDERLINE | MOD_BOLD | MOD_ITALIC) };
    std::string out = TokenJsonWriter::write(cr, "test", test_opts());
    CHECK(out.find("\"mods\": [\"bold\", \"italic\", \"underline\"]") != std::string::npos);
}

TEST_CASE("TokenJsonWriter: byte offsets convert to 1-based char line/col over UTF-8") {
    ColorizeResult cr;
    // Source: "ab\nαβγ" (UTF-8: 'a' 'b' '\n' α(2B) β(2B) γ(2B))
    // Span at byte 3 (start of α) length 2 (just α) → line 2, col 1, len 1
    cr.spans = { span(3, 2, 0xFF0000, 0) };
    std::string out = TokenJsonWriter::write(cr, "ab\n\xCE\xB1\xCE\xB2\xCE\xB3", test_opts());
    CHECK(out.find("\"line\": 2") != std::string::npos);
    CHECK(out.find("\"col\": 1") != std::string::npos);
    CHECK(out.find("\"len\": 1") != std::string::npos);
}

TEST_CASE("TokenJsonWriter: identical input produces identical bytes across two calls") {
    ColorizeResult cr;
    cr.spans = {
        span(0, 5, 0x123456, MOD_BOLD),
        span(5, 5, 0x789ABC, 0),
    };
    std::string a = TokenJsonWriter::write(cr, "0123456789", test_opts());
    std::string b = TokenJsonWriter::write(cr, "0123456789", test_opts());
    CHECK(a == b);
}

TEST_CASE("TokenJsonWriter: header fields appear in canonical order") {
    ColorizeResult cr;
    std::string out = TokenJsonWriter::write(cr, "", test_opts());
    auto p_source       = out.find("\"source\":");
    auto p_language     = out.find("\"language\":");
    auto p_theme        = out.find("\"theme\":");
    auto p_config_hash  = out.find("\"config_hash\":");
    auto p_token_count  = out.find("\"token_count\":");
    auto p_tokens       = out.find("\"tokens\":");
    CHECK(p_source < p_language);
    CHECK(p_language < p_theme);
    CHECK(p_theme < p_config_hash);
    CHECK(p_config_hash < p_token_count);
    CHECK(p_token_count < p_tokens);
}

TEST_CASE("TokenJsonWriter: MOD_STRIKETHROUGH alone emits only \"strikethrough\"") {
    ColorizeResult cr;
    cr.spans = { span(0, 4, 0xFFFFFF, MOD_STRIKETHROUGH) };
    std::string out = TokenJsonWriter::write(cr, "test", test_opts());
    CHECK(out.find("\"mods\": [\"strikethrough\"]") != std::string::npos);
}
