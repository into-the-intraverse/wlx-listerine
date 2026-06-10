#pragma once

#include "core_dll/theme/resolved_style.h"
#include "wlx_core/abi.h"
#include "wlx_core/text_modifier.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace wlx::core::theme {


// Loads and resolves a Helix-format TOML theme file.
//
// Theme format (same as Helix editor):
//   - flat scope-to-style entries at root level
//   - optional [palette] section for named colors
//   - optional `inherits = "parent_theme"` for theme inheritance
//   - style values: string (fg color) or table { fg, bg, modifiers }
//
// Scope resolution uses hierarchical fallback:
//   "function.method.private" -> "function.method" -> "function"
class WLX_CORE_API HelixTheme {
public:
    // Load a theme by name from the given directory.
    // Follows `inherits` chains.  Returns a default theme on failure —
    // dark or light per `dark_fallback`, so a failed light-theme load
    // doesn't yield dark colors on a light background.
    static HelixTheme load(const std::string& theme_name,
                           const std::filesystem::path& theme_dir,
                           bool dark_fallback = true);

    // Hierarchical scope lookup.  Tries exact match, then strips last
    // dot-segment repeatedly.  Returns nullopt if no scope matches.
    std::optional<ResolvedStyle> resolve(const std::string& scope) const;

    // Convenience: resolve fg color with a fallback value.
    uint32_t resolve_fg(const std::string& scope, uint32_t fallback) const;

    // Check if the theme has any styles loaded.
    bool empty() const { return styles_.empty(); }

    // Build a default theme with VS-Code-inspired colors (dark or light).
    static HelixTheme make_default(bool dark_mode);

private:
    std::unordered_map<std::string, ResolvedStyle> styles_;
};

}  // namespace wlx::core::theme
