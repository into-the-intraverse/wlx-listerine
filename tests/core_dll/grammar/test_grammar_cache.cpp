#include <doctest/doctest.h>
#include "core_dll/grammar/grammar_cache.h"

#include <chrono>
#include <memory>

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
