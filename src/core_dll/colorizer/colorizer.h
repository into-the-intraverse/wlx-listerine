#pragma once

#include "core_dll/colorizer/color_span.h"
#include "core_dll/colorizer/colorize_result.h"
#include "core_dll/theme/helix_theme.h"
#include "wlx_core/abi.h"

#include <tree_sitter/api.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wlx::core::grammar { class GrammarRegistry; }

// Opaque cached parse tree handed across the C ABI (see wlx_core/abi.h's
// `typedef struct WlxTree WlxTree;`). Defined at global scope so it is the same
// `::WlxTree` the ABI typedef names. Core-only: plugins never see the layout.
// Owns `source_copy` because highlighting RE-READS the source text (capture_text
// / predicate evaluation), so the tree must outlive any caller buffer.
struct WlxTree {
    TSTree* tree = nullptr;
    std::string language;
    std::string source_copy;
};

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

    // Parse `source` once; returns an owned WlxTree (caller frees via free_tree).
    // Pins the grammar so the tree's TSLanguage stays valid until free_tree.
    // Returns nullptr on unknown language / parse failure (no pin leaked).
    WlxTree* parse_tree(std::string_view source, const std::string& language);

    // Highlight [range_start,range_end) (0,0 => whole doc) against a parsed tree.
    ColorizeResult highlight_tree_range(WlxTree* t, bool dark_mode,
                                        uint32_t range_start, uint32_t range_end);

    // Delete the tree and unpin its grammar. Safe on nullptr.
    void free_tree(WlxTree* t);

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

    // Capture-style memo for QueryHighlighter: rebuilding the table costs a
    // theme resolve (with string allocs) per capture, which adds up on
    // per-scroll viewport rehighlights. Keyed per language and mode,
    // revalidated against the query pointer (eviction may recompile a query;
    // a language always recompiles from the same .scm, so an address reuse
    // can't yield a stale table). Mutable + serialized under the CoreRegistry
    // mutex, like the theme slots above.
    struct CaptureStyleMemo {
        const TSQuery* query = nullptr;
        std::vector<theme::ResolvedStyle> styles;
    };
    mutable std::unordered_map<std::string, CaptureStyleMemo> style_memo_[2];  // [dark]

    const std::vector<theme::ResolvedStyle>& capture_styles_for(
        const std::string& language, const TSQuery* query, bool dark_mode) const;
};

}  // namespace wlx::core::colorizer
