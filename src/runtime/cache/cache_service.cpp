#include "runtime/cache/cache_service.h"

#include "runtime/layout/layout_document.h"

#include <algorithm>

namespace wlx::runtime::cache {


using parser::Document;
using layout::LayoutDocument;

void CacheService::store_parse(const ParseCacheKey& key, std::shared_ptr<Document> doc) {
    parse_cache_[key] = std::move(doc);
}

std::shared_ptr<Document> CacheService::lookup_parse(const ParseCacheKey& key) {
    auto it = parse_cache_.find(key);
    if (it != parse_cache_.end())
        return it->second;
    return nullptr;
}

void CacheService::store_layout(const LayoutCacheKey& key, std::shared_ptr<LayoutDocument> layout) {
    layout_cache_[key] = std::move(layout);
}

std::shared_ptr<LayoutDocument> CacheService::lookup_layout(const LayoutCacheKey& key) {
    auto it = layout_cache_.find(key);
    if (it != layout_cache_.end())
        return it->second;
    return nullptr;
}

void CacheService::invalidate(const std::wstring& path) {
    for (auto it = parse_cache_.begin(); it != parse_cache_.end();) {
        if (it->first.path == path) it = parse_cache_.erase(it);
        else ++it;
    }
    for (auto it = layout_cache_.begin(); it != layout_cache_.end();) {
        if (it->first.parse_key.path == path) it = layout_cache_.erase(it);
        else ++it;
    }
}

void CacheService::clear() {
    parse_cache_.clear();
    layout_cache_.clear();
}

int CacheService::bucket_width(int viewport_width) {
    return ((viewport_width + 24) / 50) * 50;
}

}  // namespace wlx::runtime::cache
