#pragma once

#include "core_dll/colorizer/color_span.h"
#include "core_dll/colorizer/colorize_result.h"
#include "core_dll/theme/helix_theme.h"
#include "wlx_core/abi.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace wlx::core::colorizer {

// Per-call colorize timing breakdown, populated only when a non-null pointer is
// passed to the instrumented colorize() overload (diagnostic/bench use). Only
// `highlight_ms` is meaningful for the Lexilla engine (lex + map); the other
// fields are retained at 0 for the bench tool's stable schema.
struct ColorizeTimings {
    double grammar_load_ms  = 0;  // retained for schema compat (0 for Lexilla)
    double parse_ms         = 0;  // retained for schema compat (0 for Lexilla)
    double query_compile_ms = 0;  // retained for schema compat (0 for Lexilla)
    double highlight_ms     = 0;  // lex + style->span mapping
};

class WLX_CORE_API Colorizer {
public:
    // grammar_dir is accepted for ABI/caller compatibility but unused (Lexilla
    // needs no per-grammar files). theme_dir/theme_name/theme_light_name drive
    // the Helix theme loader; grammar_cap/ttl are also vestigial.
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

    // Instrumented variant: same result, plus a timing breakdown when non-null.
    ColorizeResult colorize(std::string_view source,
                            const std::string& language,
                            bool dark_mode,
                            ColorizeTimings* timings,
                            uint32_t range_start = 0,
                            uint32_t range_end   = 0);

    bool supports(const std::string& language) const;

    std::vector<std::string> available_languages() const;

    const theme::HelixTheme& theme(bool dark_mode) const;

private:
    // Load the requested mode's theme, applying the light-mode fallback chain
    // (explicit override -> "<theme>_light" if present -> built-in default).
    theme::HelixTheme load_theme_for(bool dark_mode) const;

    std::wstring theme_dir_;
    std::string theme_name_;
    std::string theme_light_name_;
    // Themes are loaded lazily on first use. Mutable because theme()/colorize()
    // resolve them through const accessors; safe under the CoreRegistry mutex
    // that serializes all calls.
    mutable theme::HelixTheme dark_theme_;
    mutable theme::HelixTheme light_theme_;
    mutable bool dark_loaded_ = false;
    mutable bool light_loaded_ = false;
};

}  // namespace wlx::core::colorizer
