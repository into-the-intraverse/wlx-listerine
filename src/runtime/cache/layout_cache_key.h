#pragma once

#include "runtime/cache/parse_cache_key.h"

#include <cstdint>
#include <functional>

namespace wlx::runtime::cache {


struct LayoutCacheKey {
    ParseCacheKey parse_key;
    int viewport_width_bucket = 0;
    uint64_t theme_hash = 0;
    // These three change the produced LayoutDocument but are NOT captured by
    // theme_hash (which hashes both palettes together) — they must be in the
    // key or a dark/light toggle, wrap toggle, or line-number toggle would
    // serve a stale layout.
    bool dark_mode = false;
    bool wrap_text = false;
    bool line_numbers = false;

    bool operator==(const LayoutCacheKey& o) const {
        return parse_key == o.parse_key && viewport_width_bucket == o.viewport_width_bucket
            && theme_hash == o.theme_hash
            && dark_mode == o.dark_mode && wrap_text == o.wrap_text
            && line_numbers == o.line_numbers;
    }
};

struct LayoutCacheKeyHash {
    size_t operator()(const LayoutCacheKey& k) const {
        size_t h = ParseCacheKeyHash{}(k.parse_key);
        h ^= std::hash<int>{}(k.viewport_width_bucket) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(k.theme_hash) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(k.dark_mode) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(k.wrap_text) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(k.line_numbers) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

}  // namespace wlx::runtime::cache
