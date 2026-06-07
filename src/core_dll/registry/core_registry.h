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

    colorizer::ColorizeResult colorize(std::string_view source,
                                       const std::string& language,
                                       bool dark_mode,
                                       uint32_t range_start = 0,
                                       uint32_t range_end   = 0);

    // Cached-tree path: parse once, then highlight viewport byte-ranges against
    // the cached tree without re-parsing. WlxTree is the global opaque handle
    // defined in colorizer.h (named by abi.h's typedef). All three take the
    // registry mutex for their whole body, like colorize().
    WlxTree* parse_tree(std::string_view source, const std::string& language);
    colorizer::ColorizeResult highlight_tree_range(WlxTree* t, bool dark_mode,
                                                   uint32_t range_start,
                                                   uint32_t range_end);
    void free_tree(WlxTree* t);

    bool supports(const std::string& language);
    // Force the cold grammar load + query compile for `language` to happen now
    // (under the registry mutex) so a later colorize() of it is warm.
    void prewarm(const std::string& language);
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
