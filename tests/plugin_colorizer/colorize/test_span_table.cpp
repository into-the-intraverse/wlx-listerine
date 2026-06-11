// tests/plugin_colorizer/colorize/test_span_table.cpp
#include <doctest/doctest.h>

#include "plugin_colorizer/colorize/span_table.h"
#include "plugin_colorizer/colorize/sweep_chunk.h"

using wlx::core::colorizer::ColorSpan;
using wlx::core::colorizer::ColorizeResult;
using wlx::plugin_colorizer::colorize::SpanTable;
using wlx::plugin_colorizer::colorize::next_chunk_bytes;

static ColorSpan span(uint32_t start, uint32_t len, uint32_t color = 0xABCDEF) {
    ColorSpan s;
    s.start = start;
    s.length = len;
    s.color = color;
    return s;
}

TEST_CASE("span table: append + slice round-trips a single chunk") {
    SpanTable t;
    ColorizeResult chunk;
    chunk.spans = {span(0, 5), span(10, 4), span(20, 6)};
    t.append_chunk(chunk, 0, 30);
    CHECK(t.swept_hi() == 30);
    CHECK(t.size() == 3);
    auto all = t.slice(0, 30);
    REQUIRE(all.spans.size() == 3);
    CHECK(all.spans[1].start == 10);
}

TEST_CASE("span table: boundary-crossing span is not duplicated across chunks") {
    // highlight_range returns every span OVERLAPPING the window, so a span
    // crossing a chunk edge comes back from BOTH chunks. append_chunk keeps
    // only spans whose START is inside the chunk window.
    SpanTable t;
    ColorizeResult c1;
    c1.spans = {span(0, 5), span(28, 10)};   // 28..38 crosses the 0..30 edge
    t.append_chunk(c1, 0, 30);
    ColorizeResult c2;
    c2.spans = {span(28, 10), span(40, 3)};  // 28-starter repeats; must be dropped
    t.append_chunk(c2, 30, 60);
    CHECK(t.size() == 3);
    auto s = t.slice(0, 60);
    REQUIRE(s.spans.size() == 3);
    CHECK(s.spans[1].start == 28);
    CHECK(s.spans[2].start == 40);
}

TEST_CASE("span table: slice includes the predecessor span overlapping lo") {
    SpanTable t;
    ColorizeResult chunk;
    chunk.spans = {span(0, 5), span(8, 10), span(30, 2)};  // 8..18 overlaps lo=12
    t.append_chunk(chunk, 0, 40);
    auto s = t.slice(12, 31);
    REQUIRE(s.spans.size() == 2);
    CHECK(s.spans[0].start == 8);
    CHECK(s.spans[1].start == 30);
}

TEST_CASE("span table: slice excludes spans entirely outside [lo,hi)") {
    SpanTable t;
    ColorizeResult chunk;
    chunk.spans = {span(0, 5), span(10, 5), span(20, 5)};
    t.append_chunk(chunk, 0, 30);
    CHECK(t.slice(5, 10).spans.empty());     // gap between spans
    CHECK(t.slice(25, 30).spans.empty());    // past the last span end
    auto s = t.slice(10, 11);
    REQUIRE(s.spans.size() == 1);
    CHECK(s.spans[0].start == 10);
}

TEST_CASE("span table: completeness watermark") {
    SpanTable t;
    CHECK(t.complete(0));          // empty file is trivially complete
    CHECK(!t.complete(100));
    t.append_chunk({}, 0, 60);     // chunk with no spans still advances the sweep
    CHECK(t.swept_hi() == 60);
    CHECK(!t.complete(100));
    t.append_chunk({}, 60, 100);
    CHECK(t.complete(100));
    t.clear();
    CHECK(!t.complete(100));
    CHECK(t.size() == 0);
}

TEST_CASE("adaptive sweep chunk targets ~25ms of highlight per chunk") {
    // pathological language: 64 KB took 480 ms -> shrink hard, clamped at 16 KB
    CHECK(next_chunk_bytes(64 * 1024, 480.0) == 16 * 1024);
    // fast language: 64 KB took 2 ms -> grow, clamped at 1 MB
    CHECK(next_chunk_bytes(64 * 1024, 2.0) == 1024 * 1024);
    // on-target stays put (within clamps)
    CHECK(next_chunk_bytes(256 * 1024, 25.0) == 256 * 1024);
    // degenerate timing -> max growth, no div-by-zero
    CHECK(next_chunk_bytes(64 * 1024, 0.0) == 1024 * 1024);
}

TEST_CASE("sweep abort: generation bump / close flag cancel between chunks") {
    using wlx::runtime::host::ViewLiveToken;
    using wlx::plugin_colorizer::colorize::sweep_superseded;
    ViewLiveToken live;
    live.generation.store(7);
    CHECK(!sweep_superseded(live, 7));
    live.generation.store(8);                 // reload/relang/dark-flip bumped it
    CHECK(sweep_superseded(live, 7));
    live.generation.store(7);
    live.closed.store(true);                  // ListCloseWindow
    CHECK(sweep_superseded(live, 7));
}
