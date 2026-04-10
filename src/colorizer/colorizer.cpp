#include "colorizer.h"
#include "grammar_registry.h"
#include "theme_loader.h"

Colorizer::Colorizer(const std::wstring& grammar_dir, const std::wstring& theme_dir) {
    // Will be populated in subsequent tasks
}

Colorizer::~Colorizer() = default;

ColorizeResult Colorizer::colorize(const std::string& source,
                                   const std::string& language,
                                   bool dark_mode) const {
    return {};
}

bool Colorizer::supports(const std::string& language) const {
    return false;
}

std::vector<std::string> Colorizer::available_languages() const {
    return {};
}
