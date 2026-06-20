#pragma once

#include "core_dll/colorizer/colorizer.h"
#include "core_dll/registry/core_config.h"
#include "core_dll/theme/helix_theme.h"
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace wlx::core::registry {


// Process-wide singleton living inside wlx-listerine-core.dll. Lazy-
// initialized via std::call_once on the first wlx_core_acquire call.
// Discovers its own install dir via GetModuleFileNameW(g_core_module).
//
// All public entry points take the registry's mutex so a colorize() call
// holds the lock for the full Lexilla lex + style->span mapping. Two windows
// colorizing concurrently serialize through this mutex. Acceptable given the
// streaming lex is fast; a fine-grained scheme is possible but defers to a
// future iteration.
class CoreRegistry {
public:
    static CoreRegistry& instance();

    colorizer::ColorizeResult colorize(std::string_view source,
                                       const std::string& language,
                                       bool dark_mode,
                                       uint32_t range_start = 0,
                                       uint32_t range_end   = 0);

    bool supports(const std::string& language);
    std::vector<std::string> available_languages() const;
    const theme::HelixTheme& theme(bool dark_mode) const;

private:
    CoreRegistry();
    static std::wstring resolve_core_dir();

    mutable std::mutex mu_;
    std::wstring core_dir_;
    CoreConfig cfg_;
    std::unique_ptr<colorizer::Colorizer> colorizer_;  // replaced piece-by-piece in later tasks
};

}  // namespace wlx::core::registry
