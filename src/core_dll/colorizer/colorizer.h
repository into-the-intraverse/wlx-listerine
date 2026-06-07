#pragma once

#include "core_dll/colorizer/color_span.h"
#include "core_dll/colorizer/colorize_result.h"
#include "core_dll/theme/helix_theme.h"
#include "wlx_core/abi.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace wlx::core::grammar { class GrammarRegistry; }

namespace wlx::core::colorizer {

// Per-call colorize timing breakdown, populated only when a non-null pointer is
// passed to the instrumented colorize() overload (diagnostic/bench use). The
// grammar_load and query_compile fields are non-trivial only on the FIRST file
// of a language (cold path); on warm calls they are ~0 because the loaded DLL
// and compiled query are cached process-wide.
struct ColorizeTimings {
    double grammar_load_ms  = 0;  // first get_grammar -> LoadLibrary (cold only)
    double parse_ms         = 0;  // ts_parser_parse_string
    double query_compile_ms = 0;  // first get_query -> ts_query_new (cold only)
    double highlight_ms     = 0;  // query cursor exec + span flatten
};

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

    ColorizeResult colorize(std::string_view source,
                            const std::string& language,
                            bool dark_mode,
                            uint32_t range_start = 0,
                            uint32_t range_end   = 0);

    // Instrumented variant: same result, but writes a per-phase timing
    // breakdown into *timings when non-null. Used by the screenshot tool's
    // --bench path to attribute the colorize cost (cold grammar load + query
    // compile vs. parse vs. highlight) and to measure the warm path.
    ColorizeResult colorize(std::string_view source,
                            const std::string& language,
                            bool dark_mode,
                            ColorizeTimings* timings,
                            uint32_t range_start = 0,
                            uint32_t range_end   = 0);

    bool supports(const std::string& language) const;

    // Force the cold grammar DLL load + query compile for `language` so a later
    // colorize() of it hits the warm cache. Cheap no-op if already loaded.
    void prewarm(const std::string& language);

    std::vector<std::string> available_languages() const;

    const theme::HelixTheme& theme(bool dark_mode) const;

private:
    // Load the requested mode's theme, applying the light-mode fallback chain
    // (explicit override -> "<theme>_light" if present -> built-in default).
    theme::HelixTheme load_theme_for(bool dark_mode) const;

    std::unique_ptr<grammar::GrammarRegistry> grammar_registry_;
    std::wstring theme_dir_;
    std::string theme_name_;
    std::string theme_light_name_;
    // Themes are loaded lazily on first use (a file is opened in exactly one
    // mode, so the other mode's TOML parse is deferred until a dark/light
    // toggle). Mutable because theme()/colorize() resolve them through const
    // accessors; safe under the CoreRegistry mutex that serializes all calls.
    mutable theme::HelixTheme dark_theme_;
    mutable theme::HelixTheme light_theme_;
    mutable bool dark_loaded_ = false;
    mutable bool light_loaded_ = false;
};

}  // namespace wlx::core::colorizer
