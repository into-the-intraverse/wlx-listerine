#include <doctest/doctest.h>
#include "search_hud_painter.h"

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
