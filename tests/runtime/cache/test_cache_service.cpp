#include <doctest/doctest.h>
#include "runtime/cache/cache_service.h"
#include "runtime/cache/memory_estimate.h"
#include "runtime/layout/layout_document.h"
#include "runtime/parser/block_node.h"
#include "runtime/parser/document.h"

#include <string>

using namespace wlx::runtime::cache;
using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;

TEST_CASE("Viewport width bucketing floors (cached layout never wider than viewport)") {
    CHECK(CacheService::bucket_width(0) == 0);
    CHECK(CacheService::bucket_width(1) == 0);
    CHECK(CacheService::bucket_width(49) == 0);
    CHECK(CacheService::bucket_width(50) == 50);
    CHECK(CacheService::bucket_width(75) == 50);
    CHECK(CacheService::bucket_width(99) == 50);
    CHECK(CacheService::bucket_width(100) == 100);
    CHECK(CacheService::bucket_width(800) == 800);
    CHECK(CacheService::bucket_width(810) == 800);
    CHECK(CacheService::bucket_width(849) == 800);
    CHECK(CacheService::bucket_width(850) == 850);
    CHECK(CacheService::bucket_width(1920) == 1900);

    // Floor invariant: the bucket (= layout width) never exceeds the viewport.
    for (int w : {1, 26, 49, 51, 76, 333, 826, 1919})
        CHECK(CacheService::bucket_width(w) <= w);
}

TEST_CASE("Parse cache store and lookup") {
    CacheService cache;

    ParseCacheKey key{L"test.md", 100, 12345, 1};
    auto doc = std::make_shared<Document>();
    doc->blocks.push_back(BlockNode{BlockType::Paragraph});

    cache.store_parse(key, doc);

    auto result = cache.lookup_parse(key);
    REQUIRE(result .get() != nullptr);
    CHECK(result->blocks.size() == 1);
    CHECK(result->blocks[0].type == BlockType::Paragraph);
}

TEST_CASE("Parse cache miss") {
    CacheService cache;

    ParseCacheKey key{L"nonexistent.md", 0, 0, 1};
    CHECK(cache.lookup_parse(key) .get() == nullptr);
}

TEST_CASE("Parse cache key mismatch on mtime") {
    CacheService cache;

    ParseCacheKey key1{L"test.md", 100, 1000, 1};
    ParseCacheKey key2{L"test.md", 100, 2000, 1};

    cache.store_parse(key1, std::make_shared<Document>());
    CHECK(cache.lookup_parse(key1) .get() != nullptr);
    CHECK(cache.lookup_parse(key2) .get() == nullptr);
}

TEST_CASE("Parse cache key mismatch on parser version") {
    CacheService cache;

    ParseCacheKey key1{L"test.md", 100, 1000, 1};
    ParseCacheKey key2{L"test.md", 100, 1000, 2};

    cache.store_parse(key1, std::make_shared<Document>());
    CHECK(cache.lookup_parse(key1) .get() != nullptr);
    CHECK(cache.lookup_parse(key2) .get() == nullptr);
}

TEST_CASE("Parse cache caps at kMaxEntries") {
    CacheService cache;

    for (uint64_t i = 0; i < CacheService::kMaxEntries + 4; i++)
        cache.store_parse({L"f.md", 100, /*mtime=*/i, 1}, std::make_shared<Document>());

    CHECK(cache.parse_cache_size() == CacheService::kMaxEntries);
    // Oldest four evicted, rest survive.
    CHECK(cache.lookup_parse({L"f.md", 100, 0, 1}) .get() == nullptr);
    CHECK(cache.lookup_parse({L"f.md", 100, 3, 1}) .get() == nullptr);
    CHECK(cache.lookup_parse({L"f.md", 100, 4, 1}) .get() != nullptr);
    CHECK(cache.lookup_parse({L"f.md", 100, CacheService::kMaxEntries + 3, 1}) .get() != nullptr);
}

TEST_CASE("Parse cache eviction is LRU: lookup promotes") {
    CacheService cache;

    for (uint64_t i = 0; i < CacheService::kMaxEntries; i++)
        cache.store_parse({L"f.md", 100, /*mtime=*/i, 1}, std::make_shared<Document>());

    // Touch the oldest entry so mtime=1 becomes the LRU tail instead.
    CHECK(cache.lookup_parse({L"f.md", 100, 0, 1}) .get() != nullptr);

    cache.store_parse({L"f.md", 100, 999, 1}, std::make_shared<Document>());

    CHECK(cache.parse_cache_size() == CacheService::kMaxEntries);
    CHECK(cache.lookup_parse({L"f.md", 100, 0, 1}) .get() != nullptr);  // promoted -> survived
    CHECK(cache.lookup_parse({L"f.md", 100, 1, 1}) .get() == nullptr);  // LRU tail -> evicted
}

TEST_CASE("Layout cache caps at kMaxEntries") {
    CacheService cache;

    LayoutCacheKey k;
    k.parse_key = ParseCacheKey{L"f.md", 100, 1000, 1};
    for (size_t i = 0; i < CacheService::kMaxEntries + 1; i++) {
        k.viewport_width_bucket = static_cast<int>((i + 1) * 50);
        cache.store_layout(k, std::make_shared<LayoutDocument>());
    }

    CHECK(cache.layout_cache_size() == CacheService::kMaxEntries);
    k.viewport_width_bucket = 50;
    CHECK(cache.lookup_layout(k) .get() == nullptr);   // oldest -> evicted
    k.viewport_width_bucket = 100;
    CHECK(cache.lookup_layout(k) .get() != nullptr);
}

TEST_CASE("Clear empties all caches") {
    CacheService cache;

    cache.store_parse({L"a.md", 100, 1000, 1}, std::make_shared<Document>());
    cache.store_parse({L"b.md", 200, 2000, 1}, std::make_shared<Document>());

    CHECK(cache.parse_cache_size() == 2);

    cache.clear();

    CHECK(cache.parse_cache_size() == 0);
    CHECK(cache.layout_cache_size() == 0);
}

TEST_CASE("Multiple entries for same file different params") {
    CacheService cache;

    ParseCacheKey key1{L"test.md", 100, 1000, 1};
    ParseCacheKey key2{L"test.md", 100, 2000, 1};

    auto doc1 = std::make_shared<Document>();
    doc1->blocks.push_back(BlockNode{BlockType::Heading});

    auto doc2 = std::make_shared<Document>();
    doc2->blocks.push_back(BlockNode{BlockType::Paragraph});

    cache.store_parse(key1, doc1);
    cache.store_parse(key2, doc2);

    CHECK(cache.parse_cache_size() == 2);
    CHECK(cache.lookup_parse(key1)->blocks[0].type == BlockType::Heading);
    CHECK(cache.lookup_parse(key2)->blocks[0].type == BlockType::Paragraph);
}

TEST_CASE("Overwrite existing cache entry") {
    CacheService cache;

    ParseCacheKey key{L"test.md", 100, 1000, 1};

    auto doc1 = std::make_shared<Document>();
    doc1->blocks.push_back(BlockNode{BlockType::Heading});
    cache.store_parse(key, doc1);

    auto doc2 = std::make_shared<Document>();
    doc2->blocks.push_back(BlockNode{BlockType::Paragraph});
    cache.store_parse(key, doc2);

    CHECK(cache.parse_cache_size() == 1);
    CHECK(cache.lookup_parse(key)->blocks[0].type == BlockType::Paragraph);
}

// ---------------------------------------------------------------------------
// Byte-budget helpers and tests
// ---------------------------------------------------------------------------

// Returns a ParseCacheKey with a distinct path for each index i.
static ParseCacheKey key_for(int i) {
    return ParseCacheKey{std::wstring(L"bench") + std::to_wstring(i) + L".md",
                         /*size=*/100, /*mtime=*/static_cast<uint64_t>(i + 1), /*parser_version=*/1};
}

// Builds a Document whose estimate_document_memory() is approximately `mb` MiB.
// One top-level block with one InlineNode holding mb*1024*1024/2 wchar_t chars
// (each wchar_t is 2 bytes on Windows, so the text alone is ~mb MiB).
static std::shared_ptr<Document> doc_of_mb(size_t mb) {
    auto doc = std::make_shared<Document>();
    BlockNode block;
    block.type = BlockType::Paragraph;
    InlineNode inl;
    inl.type = InlineType::Text;
    inl.text.assign(mb * 1024 * 1024 / 2, L'x');
    block.inlines.push_back(std::move(inl));
    doc->blocks.push_back(std::move(block));
    return doc;
}

TEST_CASE("cache byte budget: eviction frees LRU entries until under budget") {
    CacheService c;
    for (int i = 0; i < 5; ++i)
        c.store_parse(key_for(i), doc_of_mb(20));   // 5 x ~20 MB vs 64 MB budget
    CHECK(c.parse_total_bytes() <= CacheService::kMaxBytesPerCache);
    CHECK(c.lookup_parse(key_for(4)).get() != nullptr);   // just-stored always survives
    CHECK(c.lookup_parse(key_for(0)).get() == nullptr);   // LRU tail evicted
}

TEST_CASE("cache byte budget: an oversize single entry still survives its own store") {
    CacheService c;
    c.store_parse(key_for(0), doc_of_mb(100));      // alone over budget
    CHECK(c.lookup_parse(key_for(0)).get() != nullptr);   // map.size() > 1 guard
}

TEST_CASE("cache byte budget: overwrite updates the tracked total") {
    CacheService c;
    c.store_parse(key_for(0), doc_of_mb(20));
    const size_t before = c.parse_total_bytes();
    c.store_parse(key_for(0), doc_of_mb(1));        // same key, smaller doc
    CHECK(c.parse_total_bytes() < before);
}
