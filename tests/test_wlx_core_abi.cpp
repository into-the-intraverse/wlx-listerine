#include <doctest/doctest.h>
#include <wlx_core/abi.h>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#include "colorizer.h"

// Test fixture: install a Colorizer for the test harness, mirroring what a
// plugin would do. Must come before any TEST_CASE that calls acquire/colorize.
extern "C" __declspec(dllimport) void
wlx_core_install_for_task2(std::unique_ptr<Colorizer>);

static bool has_grammars() {
    return std::filesystem::exists("grammars/c/tree-sitter-c.dll");
}

static struct TestCoreInit {
    TestCoreInit() {
        if (has_grammars()) {
            wlx_core_install_for_task2(std::make_unique<Colorizer>(
                L"grammars", L"config/themes", "default", ""));
        }
    }
} g_test_core_init;

TEST_CASE("ABI version constant matches DLL export") {
    CHECK(wlx_core_abi_version() == WLX_CORE_ABI_VERSION);
}

TEST_CASE("acquire is idempotent") {
    auto* a = wlx_core_acquire();
    auto* b = wlx_core_acquire();
    if (has_grammars()) {
        CHECK(a != nullptr);
    }
    CHECK(a == b);
    wlx_core_release(a);
    wlx_core_release(b);
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
                               "c", /*dark=*/1, &spans, &count);
    CHECK(rc == 0);
    CHECK(count > 0);
    CHECK(spans != nullptr);
    wlx_core_free_spans(spans);
    wlx_core_release(core);
}
