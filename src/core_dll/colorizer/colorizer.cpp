#include "core_dll/colorizer/colorizer.h"
#include "core_dll/lexilla/lexer_registry.h"

#include <chrono>
#include <filesystem>

namespace wlx::core::colorizer {

using namespace wlx::core::theme;

Colorizer::Colorizer(const std::wstring& /*grammar_dir*/,
                     const std::wstring& theme_dir,
                     const std::string& theme_name,
                     const std::string& theme_light_name,
                     uint32_t /*grammar_cap*/,
                     uint32_t /*grammar_ttl_minutes*/)
    : theme_dir_(theme_dir)
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
    // A failed load must fall back to the LIGHT defaults, not the dark ones.
    if (!theme_light_name_.empty()) {
        return HelixTheme::load(theme_light_name_, dir, /*dark_fallback=*/false);
    }
    std::string light_candidate = theme_name_ + "_light";
    auto light_path = dir / (light_candidate + ".toml");
    if (std::filesystem::exists(light_path)) {
        return HelixTheme::load(light_candidate, dir, /*dark_fallback=*/false);
    }
    return HelixTheme::make_default(false);
}

bool Colorizer::supports(const std::string& language) const {
    return lexilla::lexer_spec_for(language) != nullptr;
}

std::vector<std::string> Colorizer::available_languages() const {
    return lexilla::registered_languages();
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

    const lexilla::LexerSpec* spec = lexilla::lexer_spec_for(language);
    if (!spec) return result;  // unknown language -> plain text (no spans)

    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    result.spans = lexilla::highlight(*spec, source, theme(dark_mode),
                                      range_start, range_end);
    auto t1 = clock::now();

    if (timings) {
        timings->grammar_load_ms  = 0;
        timings->parse_ms         = 0;
        timings->query_compile_ms = 0;
        timings->highlight_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    return result;
}

}  // namespace wlx::core::colorizer
