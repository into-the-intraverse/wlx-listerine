#pragma once

#include "core_dll/colorizer/color_span.h"
#include "core_dll/colorizer/colorize_result.h"
#include "wlx_core/text_modifier.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace wlx::tools::screenshot {

struct TokenJsonOptions {
    std::string source_name;     // basename, e.g. "sample.cpp"
    std::string language;        // tree-sitter language id, e.g. "cpp"
    std::string theme_name;      // e.g. "default_dark"
    std::string config_hash;     // hex digest, computed by caller (writer never hashes)
};

// Header-only deterministic JSON serializer for ColorizeResult.
//
// Output keys appear in fixed order: source, language, theme, config_hash,
// token_count, tokens. Each token has keys (line, col, len, color, mods) in
// that order. Header strings are JSON-escaped (backslash, quote, and control
// characters as \uXXXX).
//
// Tokens are sorted by (line, col, -len) and adjacent tokens with identical
// (color, mods) collapse into one. Modifier names appear sorted alphabetically
// in lowercase.
//
// Byte→char offset conversion walks the UTF-8 source assuming valid UTF-8
// (the colorizer rejects invalid encodings upstream). line/col are 1-based
// over wchar positions. 4-byte UTF-8 (supplementary planes) counts as two
// wchar units in UTF-16, mirroring how the layout engine treats them.
class TokenJsonWriter {
public:
    static std::string write(const wlx::core::colorizer::ColorizeResult& result,
                             std::string_view source_utf8,
                             const TokenJsonOptions& opts);

private:
    struct Token {
        uint32_t line;
        uint32_t col;
        uint32_t len;
        uint32_t color;
        uint8_t  mods;
    };

    struct ByteSite { uint32_t wchar_off; uint32_t line; uint32_t col; };

    static std::vector<ByteSite> index_source(std::string_view src);
    static std::vector<Token> build_tokens(
        const wlx::core::colorizer::ColorizeResult& result,
        const std::vector<ByteSite>& sites,
        std::string_view src);
    static void sort_and_collapse(std::vector<Token>& tokens);
    static std::string escape_json(std::string_view s);
    static std::string mods_array(uint8_t modifiers);
    static std::string color_hex(uint32_t rgb);
};

inline std::vector<TokenJsonWriter::ByteSite>
TokenJsonWriter::index_source(std::string_view src) {
    std::vector<ByteSite> sites(src.size() + 1);
    uint32_t line = 1;
    uint32_t col = 1;
    uint32_t wch = 0;
    size_t i = 0;
    while (i <= src.size()) {
        sites[i] = { wch, line, col };
        if (i == src.size()) break;
        unsigned char c = static_cast<unsigned char>(src[i]);
        size_t step = 1;
        if      ((c & 0x80) == 0x00) step = 1;
        else if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;
        // Clamp a truncated multibyte tail so the final pass still writes
        // sites[src.size()] — otherwise it stays zero-initialized and token
        // lengths ending there underflow.
        step = std::min(step, src.size() - i);
        for (size_t k = 1; k < step; ++k)
            sites[i + k] = { wch, line, col };
        // col advances in the same UTF-16 units as wchar_off, so token len
        // (a wchar_off difference) composes with col in sort_and_collapse.
        const uint32_t units = (step == 4) ? 2u : 1u;
        wch += units;
        if (c == '\n') { ++line; col = 1; }
        else           { col += units; }
        i += step;
    }
    return sites;
}

inline std::vector<TokenJsonWriter::Token>
TokenJsonWriter::build_tokens(const wlx::core::colorizer::ColorizeResult& result,
                              const std::vector<ByteSite>& sites,
                              std::string_view src) {
    std::vector<Token> out;
    out.reserve(result.spans.size());
    for (const auto& s : result.spans) {
        if (s.length == 0) continue;
        if (s.start >= sites.size()) continue;
        const auto& at_start = sites[s.start];
        size_t end_byte = static_cast<size_t>(s.start) + s.length;
        if (end_byte > src.size()) end_byte = src.size();
        const auto& at_end = sites[end_byte];
        Token t;
        t.line  = at_start.line;
        t.col   = at_start.col;
        t.len   = at_end.wchar_off - at_start.wchar_off;
        t.color = s.color & 0x00FFFFFFu;
        t.mods  = s.modifiers;
        if (t.len > 0) out.push_back(t);
    }
    return out;
}

inline void TokenJsonWriter::sort_and_collapse(std::vector<Token>& tokens) {
    std::sort(tokens.begin(), tokens.end(), [](const Token& a, const Token& b){
        if (a.line != b.line) return a.line < b.line;
        if (a.col  != b.col ) return a.col  < b.col;
        return a.len > b.len;
    });
    std::vector<Token> merged;
    merged.reserve(tokens.size());
    for (auto& t : tokens) {
        if (!merged.empty()) {
            auto& prev = merged.back();
            bool adjacent =
                prev.line == t.line &&
                prev.col + prev.len == t.col &&
                prev.color == t.color &&
                prev.mods  == t.mods;
            if (adjacent) {
                prev.len += t.len;
                continue;
            }
        }
        merged.push_back(t);
    }
    tokens.swap(merged);
}

inline std::string TokenJsonWriter::escape_json(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04X", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

inline std::string TokenJsonWriter::mods_array(uint8_t modifiers) {
    std::vector<const char*> names;
    // The check order below is hand-tuned so the resulting names appear in
    // alphabetical order (bold < italic < strikethrough < underline). If a
    // new TextModifier is added, insert its push_back at the lexicographically
    // correct position — the test for canonical mod ordering will fail
    // otherwise.
    if (modifiers & MOD_BOLD)          names.push_back("bold");
    if (modifiers & MOD_ITALIC)        names.push_back("italic");
    if (modifiers & MOD_STRIKETHROUGH) names.push_back("strikethrough");
    if (modifiers & MOD_UNDERLINE)     names.push_back("underline");
    std::string out = "[";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) out += ", ";
        out += "\"";
        out += names[i];
        out += "\"";
    }
    out += "]";
    return out;
}

inline std::string TokenJsonWriter::color_hex(uint32_t rgb) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06X", rgb & 0x00FFFFFFu);
    return std::string(buf);
}

inline std::string TokenJsonWriter::write(
    const wlx::core::colorizer::ColorizeResult& result,
    std::string_view source_utf8,
    const TokenJsonOptions& opts)
{
    auto sites  = index_source(source_utf8);
    auto tokens = build_tokens(result, sites, source_utf8);
    sort_and_collapse(tokens);

    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"source\": \""       << escape_json(opts.source_name) << "\",\n";
    ss << "  \"language\": \""     << escape_json(opts.language)    << "\",\n";
    ss << "  \"theme\": \""        << escape_json(opts.theme_name)  << "\",\n";
    ss << "  \"config_hash\": \""  << escape_json(opts.config_hash) << "\",\n";
    ss << "  \"token_count\": "    << tokens.size()    << ",\n";
    if (tokens.empty()) {
        ss << "  \"tokens\": []\n";
    } else {
        ss << "  \"tokens\": [\n";
        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& t = tokens[i];
            ss << "    { \"line\": " << t.line
               << ", \"col\": "      << t.col
               << ", \"len\": "      << t.len
               << ", \"color\": \""  << color_hex(t.color)
               << "\", \"mods\": "   << mods_array(t.mods)
               << " }";
            if (i + 1 != tokens.size()) ss << ",";
            ss << "\n";
        }
        ss << "  ]\n";
    }
    ss << "}\n";
    return ss.str();
}

}  // namespace wlx::tools::screenshot
