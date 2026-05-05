#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct ParseCacheKey {
    std::wstring path;
    uint64_t size = 0;
    uint64_t mtime = 0;
    uint32_t parser_version = 0;

    bool operator==(const ParseCacheKey& o) const {
        return path == o.path && size == o.size && mtime == o.mtime
            && parser_version == o.parser_version;
    }
};

struct ParseCacheKeyHash {
    size_t operator()(const ParseCacheKey& k) const {
        size_t h = std::hash<std::wstring>{}(k.path);
        h ^= std::hash<uint64_t>{}(k.size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(k.mtime) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(k.parser_version) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
