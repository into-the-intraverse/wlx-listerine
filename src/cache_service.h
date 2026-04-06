#pragma once

#include "document_model.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

struct LayoutDocument; // forward declaration

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

struct ParseCacheKeyHash {
    size_t operator()(const ParseCacheKey& k) const {
        size_t h = std::hash<std::wstring>{}(k.path);
        h ^= std::hash<uint64_t>{}(k.size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(k.mtime) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(k.parser_version) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
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

class CacheService {
public:
    // Parse cache
    void store_parse(const ParseCacheKey& key, std::shared_ptr<Document> doc);
    std::shared_ptr<Document> lookup_parse(const ParseCacheKey& key);

    // Layout cache
    void store_layout(const LayoutCacheKey& key, std::shared_ptr<LayoutDocument> layout);
    std::shared_ptr<LayoutDocument> lookup_layout(const LayoutCacheKey& key);

    // Invalidation
    void invalidate(const std::wstring& path);
    void clear();

    // Viewport width quantization to reduce cache churn
    static int bucket_width(int viewport_width);

    size_t parse_cache_size() const { return parse_cache_.size(); }
    size_t layout_cache_size() const { return layout_cache_.size(); }

private:
    std::unordered_map<ParseCacheKey, std::shared_ptr<Document>, ParseCacheKeyHash> parse_cache_;
    std::unordered_map<LayoutCacheKey, std::shared_ptr<LayoutDocument>, LayoutCacheKeyHash> layout_cache_;
};
