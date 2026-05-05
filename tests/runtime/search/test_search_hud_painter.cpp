#include <doctest/doctest.h>
#include "runtime/search/search_hud_painter.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
#include <wrl/client.h>

using namespace wlx::runtime::search;
using namespace wlx::runtime::theme;

using Microsoft::WRL::ComPtr;

static ComPtr<IDWriteFactory> create_dwrite_factory() {
    ComPtr<IDWriteFactory> factory;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    return factory;
}

// Synthetic rects in DIPs. Mirrors the painter's typical layout for a
// "1 / 1" bar (text_w ~= 24): counter 8..44, prev 48..72, next 76..100.
static SearchHudHitRects make_rects() {
    SearchHudHitRects r;
    r.counter = D2D1::RectF( 8, 4,  44, 24);
    r.prev    = D2D1::RectF(48, 2,  72, 26);
    r.next    = D2D1::RectF(76, 2, 100, 26);
    r.bounds  = D2D1::RectF( 0, 0, 108, 28);
    r.width   = 108;
    r.height  = 28;
    return r;
}

TEST_CASE("hit_test_button at 96 DPI (no scaling)") {
    auto r = make_rects();

    SUBCASE("cursor in prev rect → 0") {
        CHECK(hit_test_button(r, 50, 10, 96) == 0);
    }
    SUBCASE("cursor in next rect → 1") {
        CHECK(hit_test_button(r, 80, 10, 96) == 1);
    }
    SUBCASE("counter pill is not a click target") {
        CHECK(hit_test_button(r, 20, 10, 96) == -1);
    }
    SUBCASE("cursor far past the bar → -1") {
        CHECK(hit_test_button(r, 200, 10, 96) == -1);
    }
    SUBCASE("cursor past bar height → -1") {
        CHECK(hit_test_button(r, 50, 50, 96) == -1);
    }
    SUBCASE("right edge is exclusive") {
        // prev right edge is 72 — clicking exactly at x=72 should NOT hit prev.
        CHECK(hit_test_button(r, 72, 10, 96) != 0);
    }
}

TEST_CASE("hit_test_button at 144 DPI (150% scaling)") {
    auto r = make_rects();

    SUBCASE("physical click on visible prev button → 0") {
        // prev DIP 48..72 renders at physical 72..108; click at 90.
        // 90 / 1.5 = 60 → in [48, 72) → prev.
        CHECK(hit_test_button(r, 90, 15, 144) == 0);
    }
    SUBCASE("physical click on visible next button → 1") {
        // next DIP 76..100 renders at physical 114..150; click at 130.
        // 130 / 1.5 ≈ 86.67 → in [76, 100) → next.
        CHECK(hit_test_button(r, 130, 15, 144) == 1);
    }
    SUBCASE("physical click in DIP coords (the bug we fixed) → no longer hits") {
        // Pre-fix bug: clicking at physical x=50 (DIP=33 after scaling) used to
        // be compared raw against DIP 48..72 and miss. With the fix, x=50 → DIP
        // 33, which is past the counter pill (8..44) and before prev (48..72).
        CHECK(hit_test_button(r, 50, 15, 144) == -1);
    }
}

TEST_CASE("hit_test_button at 192 DPI (200% scaling)") {
    auto r = make_rects();

    SUBCASE("physical click on visible next button → 1") {
        // next DIP 76..100 renders at physical 152..200; click at 170.
        // 170 / 2.0 = 85 → in [76, 100) → next.
        CHECK(hit_test_button(r, 170, 20, 192) == 1);
    }
}

TEST_CASE("hit_test_button DPI fallback") {
    auto r = make_rects();

    SUBCASE("dpi=0 behaves identically to 96") {
        CHECK(hit_test_button(r, 50, 10, 0) == hit_test_button(r, 50, 10, 96));
        CHECK(hit_test_button(r, 80, 10, 0) == hit_test_button(r, 80, 10, 96));
    }
}

TEST_CASE("SearchHudPainter::layout — bar geometry invariants") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);

    ThemeService theme;
    SearchHudPainter painter(factory.Get(), theme);

    SUBCASE("zero results: bar still has positive size") {
        SearchHudState s{};
        auto r = painter.layout(s);
        CHECK(r.width  > 0);
        CHECK(r.height > 0);
    }

    SUBCASE("widget order: counter ← prev ← next, no overlaps") {
        SearchHudState s{1, 27, -1};
        auto r = painter.layout(s);
        CHECK(r.counter.right <= r.prev.left);
        CHECK(r.prev.right    <= r.next.left);
    }

    SUBCASE("buttons share the same vertical band") {
        SearchHudState s{1, 27, -1};
        auto r = painter.layout(s);
        CHECK(r.prev.top    == r.next.top);
        CHECK(r.prev.bottom == r.next.bottom);
    }

    SUBCASE("buttons are square and identically sized") {
        SearchHudState s{1, 27, -1};
        auto r = painter.layout(s);
        const float prev_w = r.prev.right  - r.prev.left;
        const float prev_h = r.prev.bottom - r.prev.top;
        const float next_w = r.next.right  - r.next.left;
        const float next_h = r.next.bottom - r.next.top;
        CHECK(prev_w == doctest::Approx(prev_h));
        CHECK(next_w == doctest::Approx(next_h));
        CHECK(prev_w == doctest::Approx(next_w));
    }

    SUBCASE("bounds origin at (0,0); covers the full bar") {
        SearchHudState s{1, 27, -1};
        auto r = painter.layout(s);
        CHECK(r.bounds.left   == 0);
        CHECK(r.bounds.top    == 0);
        CHECK(r.bounds.right  == doctest::Approx(r.width));
        CHECK(r.bounds.bottom == doctest::Approx(r.height));
    }

    SUBCASE("all hit rects fit within bounds") {
        SearchHudState s{1, 27, -1};
        auto r = painter.layout(s);
        for (const auto& rect : {r.counter, r.prev, r.next}) {
            CHECK(rect.left   >= r.bounds.left);
            CHECK(rect.right  <= r.bounds.right);
            CHECK(rect.top    >= r.bounds.top);
            CHECK(rect.bottom <= r.bounds.bottom);
        }
    }

    SUBCASE("width grows with the digit count of the counter text") {
        SearchHudState narrow{1, 1,      -1};
        SearchHudState wide  {1, 100000, -1};
        auto rn = painter.layout(narrow);
        auto rw = painter.layout(wide);
        CHECK(rw.width > rn.width);
        CHECK(rn.height == doctest::Approx(rw.height));
    }

    SUBCASE("hovered_button does not affect layout (paint-only state)") {
        SearchHudState a{1, 27, -1};
        SearchHudState b{1, 27,  0};
        SearchHudState c{1, 27,  1};
        auto ra = painter.layout(a);
        auto rb = painter.layout(b);
        auto rc = painter.layout(c);
        CHECK(ra.width      == doctest::Approx(rb.width));
        CHECK(ra.width      == doctest::Approx(rc.width));
        CHECK(ra.prev.left  == doctest::Approx(rb.prev.left));
        CHECK(ra.next.right == doctest::Approx(rc.next.right));
    }
}
