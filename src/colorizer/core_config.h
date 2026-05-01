#pragma once

#include "wlx_core_api.h"
#include <cstdint>
#include <string>

struct WLX_CORE_API CoreConfig {
    uint32_t    cap         = 8;     // soft LRU cap, count of loaded grammars
    uint32_t    ttl_minutes = 5;     // entries younger than this survive eviction sweep
    std::string theme       = "default";  // dark theme name (used as fallback for both modes)
    std::string theme_light = "";    // light-mode override; empty = auto-detect "<theme>_light"

    static CoreConfig load(const std::wstring& core_dir);
};
