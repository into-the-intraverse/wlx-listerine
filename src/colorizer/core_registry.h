#pragma once

#include "colorizer.h"
#include "helix_theme.h"
#include <memory>
#include <mutex>
#include <string>

// Process-wide singleton living inside wlx-listerine-core.dll. Lazy-
// initialized via std::call_once on the first wlx_core_acquire call.
// Discovers its own install dir via GetModuleFileNameW(g_core_module).
//
// All public entry points take the registry's mutex so a colorize() call
// holds the lock for the full parse + query + tree-delete flow. Trees are
// caller-owned and torn down inside the same locked region — eviction
// (added in Task 5) cannot race with active use. Because the lock spans
// parse + query + tree-delete, two windows colorizing concurrently
// serialize through this mutex. Acceptable given typical markdown
// code-block sizes are sub-KB; a fine-grained scheme is possible but
// defers to a future iteration.
class CoreRegistry {
public:
    static CoreRegistry& instance();

    ColorizeResult colorize(const std::string& source,
                            const std::string& language,
                            bool dark_mode);
    bool supports(const std::string& language);
    const HelixTheme& theme(bool dark_mode) const;

private:
    CoreRegistry();
    static std::wstring resolve_core_dir();

    mutable std::mutex mu_;
    std::unique_ptr<Colorizer> colorizer_;  // replaced piece-by-piece in later tasks
    std::wstring core_dir_;
};
