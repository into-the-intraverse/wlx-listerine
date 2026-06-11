#pragma once

#include "runtime/cache/layout_cache_key.h"
#include "runtime/cache/parse_cache_key.h"
#include "runtime/parser/document.h"

#include <list>
#include <memory>
#include <unordered_map>
#include <utility>

namespace wlx::runtime::layout { struct LayoutDocument; }  // forward declaration

namespace wlx::runtime::cache {

class CacheService {
public:
    // Per-cache entry cap. Keys include mtime/size, so without eviction every
    // file edit would strand the old Document/LayoutDocument (and its
    // IDWriteTextLayouts) for the TC process lifetime. Least-recently-used
    // entries are evicted on store once a cache exceeds this.
    static constexpr size_t kMaxEntries = 16;

    // Per-cache approximate byte budget. LRU entries are evicted from the back
    // while total_bytes > kMaxBytesPerCache AND map.size() > 1 (the > 1 guard
    // ensures a single oversized entry always survives its own store). The
    // per-entry byte figure is a CALIBRATED proxy, not a true heap accounting:
    // memory_estimate.h adds a per-allocation/per-block overhead tuned against
    // the md bench's per-phase working-set rows (big.md lazy skeleton ~84 MB
    // real vs ~8 MB of summed struct/text bytes) so the budget tracks real
    // residency within ~2x instead of under-counting it ~10x.
    static constexpr size_t kMaxBytesPerCache = 64 * 1024 * 1024;

    // Parse cache
    void store_parse(const ParseCacheKey& key, std::shared_ptr<parser::Document> doc);
    std::shared_ptr<parser::Document> lookup_parse(const ParseCacheKey& key);

    // Layout cache
    void store_layout(const LayoutCacheKey& key, std::shared_ptr<layout::LayoutDocument> layout);
    std::shared_ptr<layout::LayoutDocument> lookup_layout(const LayoutCacheKey& key);

    void clear();

    // Viewport width quantization to reduce cache churn
    static int bucket_width(int viewport_width);

    size_t parse_cache_size() const { return parse_cache_.map.size(); }
    size_t layout_cache_size() const { return layout_cache_.map.size(); }

    // Approximate total bytes held by each cache. Intended for tests and diagnostics.
    size_t parse_total_bytes() const { return parse_cache_.total_bytes; }
    size_t layout_total_bytes() const { return layout_cache_.total_bytes; }

private:
    // Minimal LRU map: MRU at the front of `lru`, cap-evict from the back.
    // Same eviction pattern as the core DLL's GrammarCache, minus TTL/pinning.
    template <typename Key, typename Value, typename Hash>
    struct LruMap {
        struct Entry {
            Value value;
            size_t bytes = 0;
            typename std::list<Key>::iterator lru_pos;
        };
        std::unordered_map<Key, Entry, Hash> map;
        std::list<Key> lru;
        size_t total_bytes = 0;

        void store(const Key& key, Value value, size_t bytes) {
            auto it = map.find(key);
            if (it != map.end()) {
                // Overwrite: adjust total for the size difference. A bigger
                // replacement can push past the byte budget, so evict here too.
                total_bytes -= it->second.bytes;
                total_bytes += bytes;
                it->second.value = std::move(value);
                it->second.bytes = bytes;
                lru.splice(lru.begin(), lru, it->second.lru_pos);  // promote to MRU
                evict_over_budget();
                return;
            }
            lru.push_front(key);
            map.emplace(key, Entry{std::move(value), bytes, lru.begin()});
            total_bytes += bytes;
            evict_over_budget();
        }

        // Evict from the LRU back while over the entry cap OR over the byte
        // budget (but never evict the last entry — the just-stored item must
        // survive even if alone over budget).
        void evict_over_budget() {
            while ((map.size() > kMaxEntries)
                   || (total_bytes > kMaxBytesPerCache && map.size() > 1)) {
                auto& back_key = lru.back();
                auto back_it = map.find(back_key);
                total_bytes -= back_it->second.bytes;
                map.erase(back_it);
                lru.pop_back();
            }
        }

        Value lookup(const Key& key) {
            auto it = map.find(key);
            if (it == map.end()) return nullptr;
            lru.splice(lru.begin(), lru, it->second.lru_pos);      // promote to MRU
            return it->second.value;
        }

        void clear() {
            map.clear();
            lru.clear();
            total_bytes = 0;
        }
    };

    LruMap<ParseCacheKey, std::shared_ptr<parser::Document>, ParseCacheKeyHash> parse_cache_;
    LruMap<LayoutCacheKey, std::shared_ptr<layout::LayoutDocument>, LayoutCacheKeyHash> layout_cache_;
};

}  // namespace wlx::runtime::cache
