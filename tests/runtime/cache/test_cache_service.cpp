#include <doctest/doctest.h>
#include "runtime/cache/cache_service.h"
#include "runtime/parser/block_node.h"
#include "runtime/parser/document.h"

using namespace wlx::runtime::cache;
using namespace wlx::runtime::parser;

TEST_CASE("Viewport width bucketing") {
    CHECK(CacheService::bucket_width(0) == 0);
    CHECK(CacheService::bucket_width(1) == 0);
    CHECK(CacheService::bucket_width(25) == 0);
    CHECK(CacheService::bucket_width(26) == 50);
    CHECK(CacheService::bucket_width(50) == 50);
    CHECK(CacheService::bucket_width(75) == 50);
    CHECK(CacheService::bucket_width(76) == 100);
    CHECK(CacheService::bucket_width(800) == 800);
    CHECK(CacheService::bucket_width(810) == 800);
    CHECK(CacheService::bucket_width(825) == 800);
    CHECK(CacheService::bucket_width(826) == 850);
    CHECK(CacheService::bucket_width(1920) == 1900);
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

TEST_CASE("Invalidate by path") {
    CacheService cache;

    ParseCacheKey key1{L"a.md", 100, 1000, 1};
    ParseCacheKey key2{L"b.md", 200, 2000, 1};

    cache.store_parse(key1, std::make_shared<Document>());
    cache.store_parse(key2, std::make_shared<Document>());

    CHECK(cache.parse_cache_size() == 2);

    cache.invalidate(L"a.md");

    CHECK(cache.lookup_parse(key1) .get() == nullptr);
    CHECK(cache.lookup_parse(key2) .get() != nullptr);
    CHECK(cache.parse_cache_size() == 1);
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
