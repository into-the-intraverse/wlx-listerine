#pragma once

#include <string>

namespace wlx::tools::screenshot {

struct Options {
    std::wstring input_path;
    std::wstring config_path = L"config/wlx-listerine-md.toml";
    bool         config_path_explicit = false;  // true iff --config was passed
    int   width  = 800;
    int   height = 600;
    float scroll = 0;
    bool  full   = false;
    bool  dark   = false;
    bool  bench  = false;
    bool  lazy   = false;  // route the markdown pipeline through the lazy layout + viewport materialize

    std::wstring search;     // empty == no search
    int          search_step = 0;

    // Colorizer-mode fields (added in Stage 3.1; populated by Stage 3.2's
    // arg parser; consumed by Stage 3.3+'s colorizer pipeline). Reserved
    // here so the Options header stays stable across the next several stages.
    bool         colorizer        = false;
    std::wstring lang;            // empty = infer from extension
    std::wstring cpp_grammar;     // "standard" | "unreal" | empty
    bool         dump_tokens      = false;
    std::wstring display_config;  // optional TOML override path
    bool         cached_tree     = false;  // colorizer: parse once + viewport highlight_range
};

}  // namespace wlx::tools::screenshot
