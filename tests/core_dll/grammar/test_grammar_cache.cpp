#include <doctest/doctest.h>
#include "core_dll/grammar/grammar_cache.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

using namespace wlx::core::grammar;

using namespace std::chrono_literals;

namespace {
const TSLanguage* fake_lang(int id) {
    return reinterpret_cast<const TSLanguage*>(static_cast<uintptr_t>(0x1000 + id));
}
HMODULE fake_handle(int id) {
    return reinterpret_cast<HMODULE>(static_cast<uintptr_t>(0x2000 + id));
}

GrammarCache::Loader make_loader(int& counter) {
    return [&counter](const std::wstring&, const std::string&) {
        int id = counter++;
        return GrammarCache::LoadResult{ fake_handle(id), fake_lang(id) };
    };
}

GrammarCache::Releaser noop_releaser() {
    return [](HMODULE) {};  // synthetic handles must not reach FreeLibrary
}
} // namespace

TEST_CASE("GrammarCache: register_entry normalizes CRLF in query source") {
    // Regression: `; inherits: c\r\n` previously survived intact and the
    // `; inherits:` parser (which only splits on '\n') looked up grammar
    // "c\r" -- empty -- so cpp lost all C-derived queries on CI runners
    // that check .scm files out with core.autocrlf=true. Normalizing CRLF
    // -> LF at register_entry time keeps every downstream parser sane.
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(8, 5min, [&] { return now; }, make_loader(counter), noop_releaser());
    c.register_entry("cpp", L"cpp.dll", "; inherits: c\r\n(call_expression) @function\r\n");
    const std::string normalized = c.raw_query_source("cpp");
    CHECK(normalized.find('\r') == std::string::npos);
    CHECK(normalized == "; inherits: c\n(call_expression) @function\n");
}

TEST_CASE("GrammarCache: get_grammar loads on first call") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(8, 5min, [&] { return now; }, make_loader(counter), noop_releaser());
    c.register_entry("a", L"a.dll", "");
    CHECK(c.get_grammar("a") == fake_lang(0));
    CHECK(c.is_loaded("a"));
    CHECK(c.loaded_count() == 1);
}

TEST_CASE("GrammarCache: lru promotion on repeat hits") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(8, 5min, [&] { return now; }, make_loader(counter), noop_releaser());
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");
    c.register_entry("c", L"c.dll", "");

    c.get_grammar("a"); now += 1s;
    c.get_grammar("b"); now += 1s;
    c.get_grammar("c"); now += 1s;
    c.get_grammar("a");
    CHECK(c.loaded_count() == 3);
}

TEST_CASE("GrammarCache: soft cap with fresh tail keeps everything") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(2, 5min, [&] { return now; }, make_loader(counter), noop_releaser());
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");
    c.register_entry("c", L"c.dll", "");

    c.get_grammar("a"); now += 1s;
    c.get_grammar("b"); now += 1s;
    c.get_grammar("c");
    CHECK(c.loaded_count() == 3);
}

TEST_CASE("GrammarCache: evicts when tail is stale") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(2, 5min, [&] { return now; }, make_loader(counter), noop_releaser());
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");
    c.register_entry("c", L"c.dll", "");

    c.get_grammar("a");
    now += 6min;
    c.get_grammar("b");
    now += 1min;
    c.get_grammar("c");

    CHECK(c.loaded_count() == 2);
    CHECK_FALSE(c.is_loaded("a"));
    CHECK(c.is_loaded("b"));
    CHECK(c.is_loaded("c"));
}

TEST_CASE("GrammarCache: evict sweep stops at first fresh entry") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(1, 5min, [&] { return now; }, make_loader(counter), noop_releaser());
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");
    c.register_entry("c", L"c.dll", "");

    c.get_grammar("a");
    now += 6min;
    c.get_grammar("b");
    now += 6min;
    c.get_grammar("c");

    CHECK_FALSE(c.is_loaded("a"));
    CHECK_FALSE(c.is_loaded("b"));
    CHECK(c.is_loaded("c"));
    CHECK(c.loaded_count() == 1);
}

TEST_CASE("GrammarCache: failed load not on LRU and not retried") {
    GrammarCache::SteadyTp now{};
    auto failing_loader = [](const std::wstring&, const std::string&) {
        return GrammarCache::LoadResult{};
    };
    GrammarCache c(8, 5min, [&] { return now; }, failing_loader, noop_releaser());
    c.register_entry("a", L"a.dll", "");

    CHECK(c.get_grammar("a") == nullptr);
    CHECK(c.loaded_count() == 0);
    CHECK_FALSE(c.is_loaded("a"));

    CHECK(c.get_grammar("a") == nullptr);
}

TEST_CASE("GrammarCache: reload after evict") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(1, 5min, [&] { return now; }, make_loader(counter), noop_releaser());
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");

    auto* a1 = c.get_grammar("a");
    now += 6min;
    c.get_grammar("b");
    CHECK_FALSE(c.is_loaded("a"));

    now += 1min;
    auto* a2 = c.get_grammar("a");
    CHECK(a2 != nullptr);
    CHECK(c.is_loaded("a"));
    CHECK(c.loaded_count() == 2);
}

// Counting releaser: records which synthetic handles were freed, so a test can
// assert a pinned grammar's handle was NOT passed to FreeLibrary.
namespace {
GrammarCache::Releaser counting_releaser(std::vector<HMODULE>& freed) {
    return [&freed](HMODULE h) { freed.push_back(h); };
}
} // namespace

TEST_CASE("GrammarCache: pinned grammar survives eviction pressure") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    std::vector<HMODULE> freed;
    // cap=1 so loading a 2nd/3rd grammar always triggers an evict sweep.
    GrammarCache c(1, 5min, [&] { return now; }, make_loader(counter),
                   counting_releaser(freed));
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");
    c.register_entry("c", L"c.dll", "");

    // Load "a", capture its language/handle identity (fake_lang/fake_handle(0)).
    auto* a_lang = c.get_grammar("a");
    REQUIRE(a_lang == fake_lang(0));
    REQUIRE(c.is_loaded("a"));

    // Make "a" stale so it WOULD be evicted, then pin it.
    now += 6min;
    c.pin("a");

    // Trigger eviction: load "b" (loaded_count 2 > cap 1 -> evict sweep).
    auto* b_lang = c.get_grammar("b");
    REQUIRE(b_lang == fake_lang(1));

    // Pinned "a" survives despite being stale: still loaded, identity intact,
    // and its handle was never freed.
    CHECK(c.is_loaded("a"));
    CHECK(c.get_grammar("a") == a_lang);   // same TSLanguage pointer
    CHECK(std::find(freed.begin(), freed.end(), fake_handle(0)) == freed.end());

    // Now load "c" while "a" is still pinned. The only evictable stale entry
    // is "b" (it must go); "a" must remain.
    now += 6min;
    auto* c_lang = c.get_grammar("c");
    REQUIRE(c_lang == fake_lang(2));
    CHECK(c.is_loaded("a"));               // pinned, mid-LRU, still alive
    CHECK_FALSE(c.is_loaded("b"));         // unpinned + stale -> evicted
    CHECK(c.is_loaded("c"));
    CHECK(std::find(freed.begin(), freed.end(), fake_handle(1)) != freed.end());
    CHECK(std::find(freed.begin(), freed.end(), fake_handle(0)) == freed.end());
}

TEST_CASE("GrammarCache: unpinned-then-stale grammar is evicted on next load") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    std::vector<HMODULE> freed;
    GrammarCache c(1, 5min, [&] { return now; }, make_loader(counter),
                   counting_releaser(freed));
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");
    c.register_entry("d", L"d.dll", "");

    c.get_grammar("a");          // a: handle 0
    now += 6min;
    c.pin("a");
    c.get_grammar("b");          // a pinned+stale survives; b: handle 1, loaded=2
    CHECK(c.is_loaded("a"));
    CHECK(std::find(freed.begin(), freed.end(), fake_handle(0)) == freed.end());

    c.unpin("a");                // a now unpinned, still stale
    now += 6min;                 // b also stale now
    c.get_grammar("d");          // load d -> evict sweep: a (oldest, unpinned,
                                 // stale) and b both evictable; sweep to cap.
    CHECK_FALSE(c.is_loaded("a"));  // unpinned + stale -> evicted
    CHECK(std::find(freed.begin(), freed.end(), fake_handle(0)) != freed.end());
    CHECK(c.is_loaded("d"));
}

TEST_CASE("GrammarCache: pin/unpin reference count balance") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    std::vector<HMODULE> freed;
    GrammarCache c(1, 5min, [&] { return now; }, make_loader(counter),
                   counting_releaser(freed));
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");
    c.register_entry("d", L"d.dll", "");

    c.get_grammar("a");          // handle 0
    now += 6min;
    c.pin("a");
    c.pin("a");                  // pin_count = 2
    c.get_grammar("b");          // a pinned+stale -> survives
    CHECK(c.is_loaded("a"));

    c.unpin("a");                // pin_count = 1, still pinned
    now += 6min;
    c.get_grammar("d");          // evict sweep: a still pinned -> survives;
                                 // b unpinned+stale -> evicted
    CHECK(c.is_loaded("a"));     // single unpin left it pinned
    CHECK(std::find(freed.begin(), freed.end(), fake_handle(0)) == freed.end());

    c.unpin("a");                // pin_count = 0, now evictable
    now += 6min;
    c.register_entry("e", L"e.dll", "");
    c.get_grammar("e");          // evict sweep: a now unpinned + stale -> evicted
    CHECK_FALSE(c.is_loaded("a"));
    CHECK(std::find(freed.begin(), freed.end(), fake_handle(0)) != freed.end());
}

TEST_CASE("GrammarCache: pin on unknown language is a safe no-op") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(8, 5min, [&] { return now; }, make_loader(counter), noop_releaser());
    c.register_entry("a", L"a.dll", "");

    c.pin("zzz-unknown");        // must NOT create a phantom entry
    c.unpin("zzz-unknown");      // also a no-op
    CHECK_FALSE(c.is_known("zzz-unknown"));
    CHECK(c.available_languages().size() == 1);  // only "a" registered

    // unpin below zero is a no-op (no underflow).
    c.unpin("a");
    c.unpin("a");
    CHECK(c.is_known("a"));
}
