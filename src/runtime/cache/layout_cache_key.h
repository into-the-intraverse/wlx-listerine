#pragma once

#include "runtime/cache/parse_cache_key.h"

#include <cstdint>
#include <functional>

namespace wlx::runtime::cache {


struct LayoutCacheKey {
    ParseCacheKey parse_key;
    int viewport_width_bucket = 0;
    uint32_t dpi = 0;
    uint64_t theme_hash = 0;

    bool operator==(const LayoutCacheKey& o) const {
        return parse_key == o.parse_key && viewport_width_bucket == o.viewport_width_bucket
            && dpi == o.dpi && theme_hash == o.theme_hash;
    }
};

struct LayoutCacheKeyHash {
    size_t operator()(const LayoutCacheKey& k) const {
        size_t h = ParseCacheKeyHash{}(k.parse_key);
        h ^= std::hash<int>{}(k.viewport_width_bucket) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(k.dpi) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(k.theme_hash) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

}  // namespace wlx::runtime::cache
