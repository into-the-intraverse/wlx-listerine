#include <doctest/doctest.h>

#include "plugin_colorizer/colorize/span_table.h"
#include "plugin_colorizer/colorize/sweep_chunk.h"
#include "wlx_core/abi.h"
#include "wlx_core/abi_spans_to_result.h"

#include <algorithm>
#include <string>

using namespace wlx::plugin_colorizer::colorize;

// A chunked sweep through the table must reproduce the exact span sequence of
// one whole-file highlight_range call — this is the "table-served colors are
// byte-identical to tree-served colors" guarantee from the spec.
TEST_CASE("sweep parity: chunked table == single whole-file highlight") {
    WlxCore* core = wlx_core::acquire_compatible();
    REQUIRE(core != nullptr);
    std::string src;
    for (int i = 0; i < 400; ++i)
        src += "static int value_" + std::to_string(i) +
               " = " + std::to_string(i) + "; /* trailing comment */\n";
    wlx_core::TreePtr tree(wlx_core_parse(core, src.c_str(),
                                          static_cast<uint32_t>(src.size()), "cpp"),
                           wlx_core::TreeDeleter{core});
    REQUIRE(tree);

    WlxColorSpan* spans = nullptr;
    uint32_t count = 0;
    REQUIRE(wlx_core_highlight_range(core, tree.get(), 1, 0, 0, &spans, &count) == 0);
    auto whole = wlx_core::abi_spans_to_result(spans, count);  // whole-doc reference

    SpanTable table;
    const auto fsize = static_cast<uint32_t>(src.size());
    const uint32_t chunk = 1024;                 // tiny chunks: maximal boundary stress
    while (!table.complete(fsize)) {
        const uint32_t lo = table.swept_hi();
        const uint32_t hi = std::min(fsize, lo + chunk);
        WlxColorSpan* cs = nullptr;
        uint32_t cc = 0;
        REQUIRE(wlx_core_highlight_range(core, tree.get(), 1, lo, hi, &cs, &cc) == 0);
        table.append_chunk(wlx_core::abi_spans_to_result(cs, cc), lo, hi);
    }

    auto swept = table.slice(0, fsize);
    REQUIRE(swept.spans.size() == whole.spans.size());
    for (size_t i = 0; i < whole.spans.size(); ++i) {
        CHECK(swept.spans[i].start == whole.spans[i].start);
        CHECK(swept.spans[i].length == whole.spans[i].length);
        CHECK(swept.spans[i].color == whole.spans[i].color);
        CHECK(swept.spans[i].modifiers == whole.spans[i].modifiers);
    }
}
