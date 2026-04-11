#include "colorizer.h"
#include "grammar_registry.h"
#include "query_highlighter.h"
#include "theme_loader.h"

#include <tree_sitter/api.h>

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

    auto* tree = grammar_registry_->parse(language, source);
    if (!tree) return result;

    auto* query = grammar_registry_->get_query(language);
    if (!query) {
        ts_tree_delete(tree);
        return result;
    }

    auto palette = theme_loader_->palette_for(language, dark_mode);
    result.spans = QueryHighlighter::highlight(tree, query, palette);

    ts_tree_delete(tree);
    return result;
}
