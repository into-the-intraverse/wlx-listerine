#include "colorizer.h"
#include "grammar_registry.h"
#include "query_highlighter.h"

#include <tree_sitter/api.h>
#include <filesystem>

Colorizer::Colorizer(const std::wstring& grammar_dir,
                     const std::wstring& theme_dir,
                     const std::string& theme_name,
                     const std::string& theme_light_name)
    : grammar_registry_(std::make_unique<GrammarRegistry>(grammar_dir))
{
    std::filesystem::path dir(theme_dir);

    // Load dark/default theme
    dark_theme_ = HelixTheme::load(theme_name, dir);

    // Load light theme: explicit override, or "<theme>_light" convention, or same as dark
    if (!theme_light_name.empty()) {
        light_theme_ = HelixTheme::load(theme_light_name, dir);
    } else {
        // Try "<theme_name>_light" automatically
        std::string light_candidate = theme_name + "_light";
        auto light_path = dir / (light_candidate + ".toml");
        if (std::filesystem::exists(light_path)) {
            light_theme_ = HelixTheme::load(light_candidate, dir);
        } else {
            // Fall back to built-in light defaults
            light_theme_ = HelixTheme::make_default(false);
        }
    }
}

Colorizer::~Colorizer() = default;

bool Colorizer::supports(const std::string& language) const {
    return grammar_registry_->supports(language);
}

std::vector<std::string> Colorizer::available_languages() const {
    return grammar_registry_->available_languages();
}

const HelixTheme& Colorizer::theme(bool dark_mode) const {
    return dark_mode ? dark_theme_ : light_theme_;
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

    const auto& t = theme(dark_mode);
    uint32_t default_color = dark_mode ? 0xD4D4D4 : 0x1F2328;
    result.spans = QueryHighlighter::highlight(tree, query, t, source, default_color);

    ts_tree_delete(tree);
    return result;
}
