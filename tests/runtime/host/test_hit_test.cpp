// tests/runtime/host/test_hit_test.cpp
//
// Tests that hit_test_position emits SOURCE-LINE (public) block indices when
// first_block_line > 0.  We cannot call HitTestPoint without a real
// IDWriteTextLayout, so we exercise the SNAP fallback (nearest-block by rect
// midpoint): text_runs must be non-empty and blocks must have valid rects, but
// run.layout may be null — the exact-hit inner loop skips null layouts and
// falls through to the snap path.
//
// Placement: tests.exe (wlx-core is linked; hit_test.cpp is part of the
// wlx-core static lib; colorizer-tests does NOT link wlx-core host files).

#include <doctest/doctest.h>

#include "runtime/host/hit_test.h"
#include "runtime/layout/layout_block.h"
#include "runtime/layout/layout_document.h"

#include <d2d1.h>

using wlx::runtime::host::hit_test_position;
using wlx::runtime::layout::LayoutBlock;
using wlx::runtime::layout::LayoutDocument;
using wlx::runtime::layout::TextRun;

namespace {

// Build a synthetic grid LayoutDocument: `count` blocks at successive Y
// positions, each 10 DIPs tall, starting at y=0.  text_runs are non-empty
// (a single run with a non-empty text string and a null layout) so the snap
// fallback can operate.  first_block_line is set to `base`.
LayoutDocument make_grid_doc(int base, int count) {
    LayoutDocument doc;
    doc.first_block_line = base;
    doc.grid_line_count  = base + count;
    const float h = 10.0f;
    for (int i = 0; i < count; ++i) {
        LayoutBlock b;
        b.rect = D2D1::RectF(0.0f, static_cast<float>(i) * h,
                             200.0f, static_cast<float>(i) * h + h);
        TextRun run;
        run.text = L"x";                // non-empty so the snap loop counts it
        run.rect = b.rect;
        run.layout = nullptr;           // null => exact loop skips; snap kicks in
        b.text_runs.push_back(std::move(run));
        doc.blocks.push_back(std::move(b));
    }
    return doc;
}

}  // namespace

TEST_CASE("hit_test_position: base==0 returns vector index (no-op regression)") {
    // 3 blocks at lines 0, 1, 2.
    auto doc = make_grid_doc(0, 3);

    // Y=5 is in block 0's rect [0,10); snap returns block 0 -> public index 0.
    auto pos = hit_test_position(doc, 100.0f, 5.0f);
    REQUIRE(pos.valid());
    CHECK(pos.block_index == 0);

    // Y=15 is in block 1's rect [10,20); snap returns block 1 -> public index 1.
    pos = hit_test_position(doc, 100.0f, 15.0f);
    REQUIRE(pos.valid());
    CHECK(pos.block_index == 1);

    // Y=25 is in block 2's rect [20,30); snap returns block 2 -> public index 2.
    pos = hit_test_position(doc, 100.0f, 25.0f);
    REQUIRE(pos.valid());
    CHECK(pos.block_index == 2);
}

TEST_CASE("hit_test_position: base==40 returns source-line indices (+40 offset)") {
    // 3 blocks representing source lines 40, 41, 42.
    // blocks[0].rect = [0,10), blocks[1].rect = [10,20), blocks[2].rect = [20,30).
    auto doc = make_grid_doc(40, 3);

    // Y=5  -> nearest block is 0 (mid=5) -> public index = 40+0 = 40.
    auto pos = hit_test_position(doc, 100.0f, 5.0f);
    REQUIRE(pos.valid());
    CHECK(pos.block_index == 40);

    // Y=15 -> nearest block is 1 (mid=15) -> public index = 40+1 = 41.
    pos = hit_test_position(doc, 100.0f, 15.0f);
    REQUIRE(pos.valid());
    CHECK(pos.block_index == 41);

    // Y=25 -> nearest block is 2 (mid=25) -> public index = 40+2 = 42.
    pos = hit_test_position(doc, 100.0f, 25.0f);
    REQUIRE(pos.valid());
    CHECK(pos.block_index == 42);
}

TEST_CASE("hit_test_position: base==40, snap above first block returns line 40 offset 0") {
    // Y=-5 is above all blocks; snap picks the closest block (block 0, mid=5)
    // and since y < mid the function returns {line_base+0, 0}.
    auto doc = make_grid_doc(40, 3);
    auto pos = hit_test_position(doc, 100.0f, -5.0f);
    REQUIRE(pos.valid());
    CHECK(pos.block_index == 40);
    CHECK(pos.char_offset == 0);
}

TEST_CASE("hit_test_position: base==40, snap below last block returns line 42 at end") {
    // Y=100 is below all blocks; snap picks block 2 (mid=25, closest).
    // y > mid so the function returns {line_base+2, block_text_length(block)}.
    auto doc = make_grid_doc(40, 3);
    auto pos = hit_test_position(doc, 100.0f, 100.0f);
    REQUIRE(pos.valid());
    CHECK(pos.block_index == 42);
    // block_text_length = len("x") = 1
    CHECK(pos.char_offset == 1);
}
