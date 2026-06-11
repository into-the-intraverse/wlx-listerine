#include "runtime/cache/cache_service.h"

#include "runtime/cache/memory_estimate.h"
#include "runtime/layout/layout_document.h"

namespace wlx::runtime::cache {


using parser::Document;
using layout::LayoutDocument;

void CacheService::store_parse(const ParseCacheKey& key, std::shared_ptr<Document> doc) {
    size_t bytes = doc ? estimate_document_memory(*doc) : 0;
    parse_cache_.store(key, std::move(doc), bytes);
}

std::shared_ptr<Document> CacheService::lookup_parse(const ParseCacheKey& key) {
    return parse_cache_.lookup(key);
}

void CacheService::store_layout(const LayoutCacheKey& key, std::shared_ptr<LayoutDocument> layout) {
    size_t bytes = layout ? estimate_layout_memory(*layout) : 0;
    layout_cache_.store(key, std::move(layout), bytes);
}

std::shared_ptr<LayoutDocument> CacheService::lookup_layout(const LayoutCacheKey& key) {
    return layout_cache_.lookup(key);
}

void CacheService::clear() {
    parse_cache_.clear();
    layout_cache_.clear();
}

int CacheService::bucket_width(int viewport_width) {
    // FLOOR to the 50-DIP bucket below (never round up): do_layout lays out at
    // the bucket width, so the cached layout must never be WIDER than the
    // viewport — a wider layout clips wrapped text at the right edge, while
    // floor's error is only a blank right strip of < 50 DIPs. Degenerate
    // viewports < 50 floor to bucket 0; do_layout clamps its layout width
    // to >= 1.
    return (viewport_width / 50) * 50;
}

}  // namespace wlx::runtime::cache
