#include <doctest/doctest.h>
#include <wlx_core/abi.h>
#include <wlx_core/abi_spans_to_result.h>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

// Lexilla needs no per-grammar files — the engine is always available.
static bool has_grammars() {
    return true;
}

TEST_CASE("ABI version constant matches DLL export") {
    CHECK(wlx_core_abi_version() == WLX_CORE_ABI_VERSION);
}

TEST_CASE("acquire is idempotent") {
    auto* a = wlx_core_acquire();
    auto* b = wlx_core_acquire();
    CHECK(a != nullptr);
    CHECK(a == b);
    wlx_core_release(a);
    wlx_core_release(b);
}

TEST_CASE("singleton initialized once across threads") {
    std::vector<WlxCore*> handles(8, nullptr);
    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i) {
        ts.emplace_back([&, i] { handles[i] = wlx_core_acquire(); });
    }
    for (auto& t : ts) t.join();
    for (auto* h : handles) {
        CHECK(h != nullptr);
        CHECK(h == handles[0]);
    }
    for (auto* h : handles) wlx_core_release(h);
}

TEST_CASE("supports returns 1 for known languages"
    * doctest::skip(!has_grammars())) {
    auto* core = wlx_core_acquire();
    CHECK(wlx_core_supports(core, "cpp") == 1);
    CHECK(wlx_core_supports(core, "definitely-not-a-language") == 0);
    wlx_core_release(core);
}

TEST_CASE("colorize round-trips a tiny C source"
    * doctest::skip(!has_grammars())) {
    auto* core = wlx_core_acquire();
    const char* src = "int main(){return 0;}";
    WlxColorSpan* spans = nullptr;
    uint32_t count = 0;
    int rc = wlx_core_colorize(core, src, (uint32_t)strlen(src),
                               "cpp", /*dark=*/1, 0, 0, &spans, &count);
    CHECK(rc == 0);
    CHECK(count > 0);
    CHECK(spans != nullptr);
    wlx_core_free_spans(spans);
    wlx_core_release(core);
}

TEST_CASE("ABI version is 6") {
    CHECK(wlx_core_abi_version() == 6);
}

TEST_CASE("abi_spans_to_result: null spans yield an empty result") {
    CHECK(wlx_core::abi_spans_to_result(nullptr, 0).spans.empty());
    CHECK(wlx_core::abi_spans_to_result(nullptr, 5).spans.empty());
}

TEST_CASE("abi_spans_to_result converts colorize output field-by-field"
    * doctest::skip(!has_grammars())) {
    auto* core = wlx_core_acquire();
    const char* src = "int main(){return 0;}";
    uint32_t len = (uint32_t)strlen(src);

    // Two identical colorize calls: one is converted (and freed) by the helper,
    // the other stays raw for the field-by-field comparison.
    WlxColorSpan* raw = nullptr; uint32_t nraw = 0;
    REQUIRE(wlx_core_colorize(core, src, len, "cpp", 1, 0, 0, &raw, &nraw) == 0);
    REQUIRE(nraw > 0);
    WlxColorSpan* conv = nullptr; uint32_t nconv = 0;
    REQUIRE(wlx_core_colorize(core, src, len, "cpp", 1, 0, 0, &conv, &nconv) == 0);
    REQUIRE(nconv == nraw);

    auto result = wlx_core::abi_spans_to_result(conv, nconv);
    REQUIRE(result.spans.size() == nraw);
    for (uint32_t i = 0; i < nraw; ++i) {
        CHECK(result.spans[i].start     == raw[i].start);
        CHECK(result.spans[i].length    == raw[i].length);
        CHECK(result.spans[i].color     == raw[i].color);
        CHECK(result.spans[i].bg_color  == raw[i].bg_color);
        CHECK(result.spans[i].has_bg    == (raw[i].has_bg != 0));
        CHECK(result.spans[i].modifiers == raw[i].modifiers);
    }
    wlx_core_free_spans(raw);
    wlx_core_release(core);
}

TEST_CASE("wlx_core_list_languages returns a non-empty list including cpp"
    * doctest::skip(!has_grammars())) {
    WlxCore* core = wlx_core_acquire();
    REQUIRE(core != nullptr);

    WlxLanguageList list{};
    list._reserved = 0xDEADBEEFu;  // must be written back to zero (abi.h contract)
    REQUIRE(wlx_core_list_languages(core, &list) == 0);
    REQUIRE(list.count > 0);
    REQUIRE(list.ids != nullptr);
    CHECK(list._reserved == 0);

    bool saw_cpp = false;
    for (uint32_t i = 0; i < list.count; i++) {
        REQUIRE(list.ids[i] != nullptr);
        if (std::string(list.ids[i]) == "cpp") { saw_cpp = true; break; }
    }
    CHECK(saw_cpp);

    wlx_core_free_language_list(&list);
    CHECK(list.ids == nullptr);
    CHECK(list.count == 0);

    wlx_core_release(core);
}

TEST_CASE("wlx_core_list_languages returns -1 on null inputs") {
    WlxLanguageList list{};
    CHECK(wlx_core_list_languages(nullptr, &list) < 0);

    WlxCore* core = wlx_core_acquire();
    REQUIRE(core != nullptr);
    CHECK(wlx_core_list_languages(core, nullptr) < 0);
    wlx_core_release(core);
}
