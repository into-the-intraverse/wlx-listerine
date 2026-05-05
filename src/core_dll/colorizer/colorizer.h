#pragma once

#include "core_dll/colorizer/color_span.h"
#include "core_dll/colorizer/colorize_result.h"
#include "core_dll/theme/helix_theme.h"
#include "wlx_core/abi.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wlx::core::grammar { class GrammarRegistry; }

namespace wlx::core::colorizer {

class WLX_CORE_API Colorizer {
public:
    // theme_dir: directory containing Helix-format .toml theme files
    // theme_name: default theme (used for dark mode, or both modes)
    // theme_light_name: optional light-mode override (empty = use theme_name)
    Colorizer(const std::wstring& grammar_dir,
              const std::wstring& theme_dir,
              const std::string& theme_name = "default",
              const std::string& theme_light_name = "",
              uint32_t grammar_cap = 8,
              uint32_t grammar_ttl_minutes = 5);
    ~Colorizer();

    ColorizeResult colorize(const std::string& source,
                            const std::string& language,
                            bool dark_mode);

    bool supports(const std::string& language) const;
    std::vector<std::string> available_languages() const;

    const theme::HelixTheme& theme(bool dark_mode) const;

private:
    std::unique_ptr<grammar::GrammarRegistry> grammar_registry_;
    theme::HelixTheme dark_theme_;
    theme::HelixTheme light_theme_;
};

}  // namespace wlx::core::colorizer
