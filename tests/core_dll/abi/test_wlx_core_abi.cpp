#include <doctest/doctest.h>
#include <wlx_core/abi.h>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

static bool has_grammars() {
    return std::filesystem::exists("grammars/c/tree-sitter-c.dll");
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
    CHECK(wlx_core_supports(core, "c") == 1);
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
                               "c", /*dark=*/1, 0, 0, &spans, &count);
    CHECK(rc == 0);
    CHECK(count > 0);
    CHECK(spans != nullptr);
    wlx_core_free_spans(spans);
    wlx_core_release(core);
}

TEST_CASE("ABI version is 5") {
    CHECK(wlx_core_abi_version() == 5);
}

TEST_CASE("wlx_core_parse returns a non-null tree for valid C source"
    * doctest::skip(!has_grammars())) {
    auto* core = wlx_core_acquire();
    const char* src = "int main(){return 0;}";
    WlxTree* t = wlx_core_parse(core, src, (uint32_t)strlen(src), "c");
    CHECK(t != nullptr);
    wlx_core_free_tree(core, t);
    wlx_core_release(core);
}

TEST_CASE("wlx_core_highlight_range full-range equals wlx_core_colorize byte-for-byte"
    * doctest::skip(!has_grammars())) {
    auto* core = wlx_core_acquire();
    const char* src = "int main(){return 0;}";
    uint32_t len = (uint32_t)strlen(src);
    for (int dark = 0; dark <= 1; ++dark) {
        WlxTree* t = wlx_core_parse(core, src, len, "c");
        REQUIRE(t != nullptr);

        WlxColorSpan* a = nullptr; uint32_t na = 0;
        CHECK(wlx_core_highlight_range(core, t, dark, 0, 0, &a, &na) == 0);

        WlxColorSpan* b = nullptr; uint32_t nb = 0;
        CHECK(wlx_core_colorize(core, src, len, "c", dark, 0, 0, &b, &nb) == 0);

        REQUIRE(na == nb);
        for (uint32_t i = 0; i < na; ++i) {
            CHECK(a[i].start     == b[i].start);
            CHECK(a[i].length    == b[i].length);
            CHECK(a[i].color     == b[i].color);
            CHECK(a[i].bg_color  == b[i].bg_color);
            CHECK(a[i].has_bg    == b[i].has_bg);
            CHECK(a[i].modifiers == b[i].modifiers);
        }

        wlx_core_free_spans(a);
        wlx_core_free_spans(b);
        wlx_core_free_tree(core, t);
    }
    wlx_core_release(core);
}

TEST_CASE("wlx_core_highlight_range sub-range yields a subset of full spans"
    * doctest::skip(!has_grammars())) {
    auto* core = wlx_core_acquire();
    const char* src = "int main(){return 0;}";
    uint32_t len = (uint32_t)strlen(src);
    WlxTree* t = wlx_core_parse(core, src, len, "c");
    REQUIRE(t != nullptr);

    WlxColorSpan* full = nullptr; uint32_t nfull = 0;
    CHECK(wlx_core_highlight_range(core, t, 1, 0, 0, &full, &nfull) == 0);

    const uint32_t S = 4, E = 11;  // a window over "main(){"
    WlxColorSpan* sub = nullptr; uint32_t nsub = 0;
    CHECK(wlx_core_highlight_range(core, t, 1, S, E, &sub, &nsub) == 0);

    CHECK(nsub <= nfull);
    for (uint32_t i = 0; i < nsub; ++i) {
        CHECK(sub[i].start >= S);
        CHECK(sub[i].start < E);
    }

    wlx_core_free_spans(full);
    wlx_core_free_spans(sub);
    wlx_core_free_tree(core, t);
    wlx_core_release(core);
}

TEST_CASE("wlx_core_free_tree is a safe no-op on a null tree") {
    WlxCore* core = wlx_core_acquire();
    wlx_core_free_tree(core, nullptr);  // must not crash
    wlx_core_free_tree(nullptr, nullptr);  // null core too
    wlx_core_release(core);
}

TEST_CASE("wlx_core_parse / wlx_core_highlight_range reject bad args") {
    WlxCore* core = wlx_core_acquire();
    CHECK(wlx_core_parse(nullptr, "x", 1, "c") == nullptr);
    CHECK(wlx_core_parse(core, nullptr, 0, "c") == nullptr);
    CHECK(wlx_core_parse(core, "x", 1, nullptr) == nullptr);

    WlxColorSpan* sp = nullptr; uint32_t n = 0;
    CHECK(wlx_core_highlight_range(nullptr, nullptr, 1, 0, 0, &sp, &n) < 0);
    CHECK(wlx_core_highlight_range(core, nullptr, 1, 0, 0, &sp, &n) < 0);
    wlx_core_release(core);
}

TEST_CASE("wlx_core_prewarm is a safe no-op on null args") {
    wlx_core_prewarm(nullptr, "c");   // must not crash
    WlxCore* core = wlx_core_acquire();
    wlx_core_prewarm(core, nullptr);  // must not crash
    wlx_core_release(core);
}

TEST_CASE("wlx_core_prewarm warms a grammar so a later colorize succeeds"
    * doctest::skip(!has_grammars())) {
    WlxCore* core = wlx_core_acquire();
    wlx_core_prewarm(core, "c");  // load grammar + compile query up front
    CHECK(wlx_core_supports(core, "c") == 1);
    const char* src = "int main(){return 0;}";
    WlxColorSpan* spans = nullptr;
    uint32_t count = 0;
    CHECK(wlx_core_colorize(core, src, (uint32_t)strlen(src),
                            "c", /*dark=*/1, 0, 0, &spans, &count) == 0);
    CHECK(count > 0);
    wlx_core_free_spans(spans);
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
