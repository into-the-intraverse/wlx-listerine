#include "core_dll/lexilla/lexilla_highlighter.h"
#include "core_dll/lexilla/lex_document.h"

#include <algorithm>
#include <optional>

// Vendored Lexilla/Scintilla headers — wrap so they can't fail the /W4 /WX build.
#if defined(_MSC_VER)
#  pragma warning(push, 3)
#endif
#include "ILexer.h"
#include "Lexilla.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

namespace wlx::core::lexilla {

using colorizer::ColorSpan;
using theme::HelixTheme;
using theme::ResolvedStyle;

namespace {

// Map a Lexilla lexicalClass tag phrase (from ILexer5::TagsOfStyle) to a Helix
// theme scope. Tags are space-separated, roughly hierarchical phrases shared
// across lexers ("comment line", "literal string interpolated", ...). Matching
// most-specific-first; "" means leave uncolored (falls back to default fg, as
// the tree-sitter path did for uncaptured nodes).
std::string tag_to_scope(std::string_view t) {
    // LexHTML prefixes embedded-script tags with a "client <lang> " / "server
    // <lang> " context (e.g. "client javascript keyword", "server php literal
    // string"). Strip that two-word prefix so the generic category resolves.
    if (t.compare(0, 7, "client ") == 0 || t.compare(0, 7, "server ") == 0) {
        std::string_view rest = t.substr(7);                  // past "client "/"server "
        if (auto sp = rest.find(' '); sp != std::string_view::npos)
            t = rest.substr(sp + 1);                          // past the language word
        else
            t = std::string_view{};
    }
    auto is = [&](std::string_view p) {
        return t == p || (t.size() > p.size() &&
                          t.compare(0, p.size(), p) == 0 && t[p.size()] == ' ');
    };
    if (t.empty() || is("default")) return "";
    if (is("tag operator")) return "operator";                // must precede "tag"
    if (is("tag") || is("error tag")) return "keyword";       // color tags incl. unknown
    if (is("attribute") || is("error attribute")) return "variable";
    if (is("comment")) return "comment";
    if (is("keyword")) return "keyword";
    if (is("preprocessor")) return "keyword.directive";
    if (is("operator")) return "operator";
    if (is("literal numeric")) return "constant.numeric";
    if (is("literal string character")) return "constant.character";
    if (is("literal template")) return "string";
    if (is("here-doc")) return "string";
    if (is("literal string")) return "string";        // incl. interpolated/raw/multiline
    if (is("literal regex")) return "string";
    if (is("literal uuid")) return "constant";
    if (is("literal")) return "constant";             // entities/CDATA/SGML special
    if (is("identifier")) return "";
    if (is("error")) return "";
    return "";
}

}  // namespace

LexResult lex(const LexerSpec& spec, std::string_view source) {
    LexResult out;
    if (spec.lexer_name.empty()) return out;

    Scintilla::ILexer5* lexer = CreateLexer(spec.lexer_name.c_str());
    if (!lexer) return out;
    // Lexilla lexers are heap-allocated; Release() frees them.
    struct Guard {
        Scintilla::ILexer5* l;
        ~Guard() { if (l) l->Release(); }
    } guard{lexer};

    for (const auto& [key, val] : spec.properties)
        lexer->PropertySet(key.c_str(), val.c_str());
    for (size_t i = 0; i < spec.word_lists.size(); ++i)
        lexer->WordListSet(static_cast<int>(i), spec.word_lists[i].c_str());

    if (!source.empty()) {
        LexDocument doc(source);
        lexer->Lex(0, static_cast<Sci_Position>(source.size()), 0, &doc);
        out.styles.assign(doc.styles().substr(0, source.size()));
    }

    // Resolve each possible style byte to a scope once: explicit override first,
    // else the lexer's semantic tag.
    const int named = lexer->NamedStyles();
    out.style_scope.assign(256, std::string());
    for (int st = 0; st < 256; ++st) {
        if (auto ov = spec.style_scopes.find(st); ov != spec.style_scopes.end())
            out.style_scope[static_cast<size_t>(st)] = ov->second;
        else if (st < named)
            out.style_scope[static_cast<size_t>(st)] = tag_to_scope(lexer->TagsOfStyle(st));
    }
    return out;
}

std::vector<ColorSpan> spans_from_lex(const LexResult& lexed, const HelixTheme& theme,
                                      uint32_t range_start, uint32_t range_end) {
    std::vector<ColorSpan> result;
    const std::string_view styles = lexed.styles;
    const uint32_t n = static_cast<uint32_t>(styles.size());
    if (n == 0 || lexed.style_scope.size() < 256) return result;

    const uint32_t lo = range_start;
    const uint32_t hi = (range_end > range_start) ? std::min(range_end, n) : n;
    if (lo >= hi) return result;

    // Resolve each distinct style byte to a theme style once.
    std::unordered_map<int, std::optional<ResolvedStyle>> memo;
    auto resolve_style = [&](int st) -> const std::optional<ResolvedStyle>& {
        if (auto it = memo.find(st); it != memo.end()) return it->second;
        std::optional<ResolvedStyle> rs;
        const std::string& scope = lexed.style_scope[static_cast<size_t>(st)];
        if (!scope.empty()) rs = theme.resolve(scope);
        return memo.emplace(st, rs).first->second;
    };

    uint32_t i = 0;
    while (i < n) {
        const unsigned char st = static_cast<unsigned char>(styles[i]);
        uint32_t j = i + 1;
        while (j < n && static_cast<unsigned char>(styles[j]) == st) ++j;

        const auto& rs = resolve_style(st);
        if (rs && (rs->has_fg || rs->has_bg || rs->modifiers != 0)) {
            // Emit the FULL run [i,j) if it overlaps [lo,hi) — unclipped, matching
            // the tree-sitter highlight_range contract the SpanTable relies on: a
            // run crossing a chunk boundary arrives in both chunks and is
            // attributed to exactly one by its start (see SpanTable::append_chunk).
            if (i < hi && j > lo)
                result.push_back({i, j - i, rs->fg, rs->bg, rs->has_bg, rs->modifiers});
        }
        i = j;
    }
    return result;
}

std::vector<ColorSpan> highlight(const LexerSpec& spec, std::string_view source,
                                 const HelixTheme& theme,
                                 uint32_t range_start, uint32_t range_end) {
    return spans_from_lex(lex(spec, source), theme, range_start, range_end);
}

}  // namespace wlx::core::lexilla
