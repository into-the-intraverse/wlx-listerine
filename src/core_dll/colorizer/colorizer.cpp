#include "core_dll/colorizer/colorizer.h"
#include "core_dll/grammar/grammar_registry.h"
#include "core_dll/highlighting/query_highlighter.h"

#include <tree_sitter/api.h>
#include <chrono>
#include <filesystem>

namespace wlx::core::colorizer {

using namespace wlx::core::grammar;
using namespace wlx::core::highlighting;
using namespace wlx::core::theme;

Colorizer::Colorizer(const std::wstring& grammar_dir,
                     const std::wstring& theme_dir,
                     const std::string& theme_name,
                     const std::string& theme_light_name,
                     uint32_t grammar_cap,
                     uint32_t grammar_ttl_minutes)
    : grammar_registry_(std::make_unique<GrammarRegistry>(
          grammar_dir, grammar_cap,
          std::chrono::seconds(grammar_ttl_minutes * 60)))
    , theme_dir_(theme_dir)
    , theme_name_(theme_name)
    , theme_light_name_(theme_light_name)
{
    // Themes are loaded lazily on first theme()/colorize() — see load_theme_for.
}

Colorizer::~Colorizer() = default;

HelixTheme Colorizer::load_theme_for(bool dark_mode) const {
    std::filesystem::path dir(theme_dir_);
    if (dark_mode) {
        return HelixTheme::load(theme_name_, dir);
    }
    // Light theme: explicit override, or "<theme>_light" convention, or default.
    if (!theme_light_name_.empty()) {
        return HelixTheme::load(theme_light_name_, dir);
    }
    std::string light_candidate = theme_name_ + "_light";
    auto light_path = dir / (light_candidate + ".toml");
    if (std::filesystem::exists(light_path)) {
        return HelixTheme::load(light_candidate, dir);
    }
    return HelixTheme::make_default(false);
}

bool Colorizer::supports(const std::string& language) const {
    return grammar_registry_->supports(language);
}

void Colorizer::prewarm(const std::string& language) {
    // Trigger the cold path now: get_grammar maps the DLL (LoadLibrary) and
    // get_query compiles the query (ts_query_new ~50ms). Both cache in the
    // GrammarCache, so the subsequent real colorize() of this language is warm.
    if (grammar_registry_->get_grammar(language))
        grammar_registry_->get_query(language);
}

WlxTree* Colorizer::parse_tree(std::string_view source, const std::string& language) {
    if (!grammar_registry_->get_grammar(language)) return nullptr;  // unknown/failed load
    std::string copy(source);
    TSTree* tree = grammar_registry_->parse(language, copy);        // parse the OWNED copy
    if (!tree) return nullptr;                                      // no pin on failure
    grammar_registry_->pin(language);                              // keep TSLanguage alive
    return new WlxTree{tree, language, std::move(copy)};
}

ColorizeResult Colorizer::highlight_tree_range(WlxTree* t, bool dark_mode,
                                               uint32_t range_start, uint32_t range_end) {
    ColorizeResult result;
    if (!t || !t->tree) return result;
    const TSQuery* query = grammar_registry_->get_query(t->language); // grammar pinned => loaded
    if (!query) return result;
    const auto& th = theme(dark_mode);
    uint32_t default_color = dark_mode ? 0xD4D4D4 : 0x1F2328;        // match colorize()'s default
    result.spans = QueryHighlighter::highlight(
        t->tree, query, th, t->source_copy, default_color, range_start, range_end);
    return result;
}

void Colorizer::free_tree(WlxTree* t) {
    if (!t) return;
    if (t->tree) ts_tree_delete(t->tree);
    grammar_registry_->unpin(t->language);
    delete t;
}

std::vector<std::string> Colorizer::available_languages() const {
    return grammar_registry_->available_languages();
}

const HelixTheme& Colorizer::theme(bool dark_mode) const {
    HelixTheme& slot  = dark_mode ? dark_theme_  : light_theme_;
    bool&       ready = dark_mode ? dark_loaded_ : light_loaded_;
    if (!ready) {
        slot = load_theme_for(dark_mode);
        ready = true;
    }
    return slot;
}

ColorizeResult Colorizer::colorize(std::string_view source,
                                   const std::string& language,
                                   bool dark_mode,
                                   uint32_t range_start,
                                   uint32_t range_end) {
    return colorize(source, language, dark_mode, nullptr, range_start, range_end);
}

ColorizeResult Colorizer::colorize(std::string_view source,
                                   const std::string& language,
                                   bool dark_mode,
                                   ColorizeTimings* timings,
                                   uint32_t range_start,
                                   uint32_t range_end) {
    ColorizeResult result;

    using clock = std::chrono::steady_clock;
    auto ms = [](clock::time_point a, clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };

    // Load the grammar first so its (cold) LoadLibrary cost is attributed to
    // grammar_load rather than folded into parse. The get_grammar call inside
    // parse() below is then a warm map lookup.
    auto t0 = clock::now();
    const TSLanguage* tslang = grammar_registry_->get_grammar(language);
    auto t1 = clock::now();
    if (!tslang) return result;

    auto* tree = grammar_registry_->parse(language, source);
    auto t2 = clock::now();
    if (!tree) return result;

    auto* query = grammar_registry_->get_query(language);
    auto t3 = clock::now();
    if (!query) {
        ts_tree_delete(tree);
        return result;
    }

    const auto& t = theme(dark_mode);
    uint32_t default_color = dark_mode ? 0xD4D4D4 : 0x1F2328;
    result.spans = QueryHighlighter::highlight(tree, query, t, source, default_color,
                                               range_start, range_end);
    auto t4 = clock::now();

    ts_tree_delete(tree);

    if (timings) {
        timings->grammar_load_ms  = ms(t0, t1);
        timings->parse_ms         = ms(t1, t2);
        timings->query_compile_ms = ms(t2, t3);
        timings->highlight_ms     = ms(t3, t4);
    }
    return result;
}

}  // namespace wlx::core::colorizer
