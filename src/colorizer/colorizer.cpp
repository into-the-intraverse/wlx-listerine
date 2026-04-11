#include "colorizer.h"
#include "grammar_registry.h"
#include "tokenizer.h"
#include "scope_mapper.h"
#include "theme_loader.h"

Colorizer::Colorizer(const std::wstring& grammar_dir, const std::wstring& theme_dir)
    : grammar_registry_(std::make_unique<GrammarRegistry>(grammar_dir))
    , theme_loader_(std::make_unique<ThemeLoader>(theme_dir)) {}

Colorizer::~Colorizer() = default;

bool Colorizer::supports(const std::string& language) const {
    return grammar_registry_->supports(language);
}

std::vector<std::string> Colorizer::available_languages() const {
    return grammar_registry_->available_languages();
}

void Colorizer::set_language_theme(const std::string& language, const std::string& theme_name) {
    theme_loader_->set_language_theme(language, theme_name);
}

ColorizeResult Colorizer::colorize(const std::string& source,
                                   const std::string& language,
                                   bool dark_mode) {
    ColorizeResult result;

    auto* grammar = grammar_registry_->get_grammar(language);
    if (!grammar) return result;

    auto token_spans = Tokenizer::tokenize(grammar, source);
    auto palette = theme_loader_->palette_for(language, dark_mode);
    auto lang_ctx = ScopeMapper::for_language(language);

    result.spans.reserve(token_spans.size());
    for (auto& ts : token_spans) {
        Scope scope = ScopeMapper::map(lang_ctx, ts.node_type);
        uint32_t color = scope_to_color(scope, palette);

        ColorSpan cs;
        cs.start = ts.start;
        cs.length = ts.length;
        cs.color = color;
        result.spans.push_back(cs);
    }

    return result;
}
