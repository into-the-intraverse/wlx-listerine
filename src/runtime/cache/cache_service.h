#pragma once

#include "runtime/cache/layout_cache_key.h"
#include "runtime/cache/parse_cache_key.h"
#include "runtime/parser/document.h"

#include <memory>
#include <string>
#include <unordered_map>

struct LayoutDocument; // forward declaration

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
