#include "plugin_colorizer/colorize/span_table.h"

#include <algorithm>
#include <cassert>

namespace wlx::plugin_colorizer::colorize {

using wlx::core::colorizer::ColorizeResult;
using wlx::core::colorizer::ColorSpan;

void SpanTable::append_chunk(const ColorizeResult& chunk,
                             uint32_t chunk_lo, uint32_t chunk_hi) {
    assert(chunk_lo == swept_hi_);
    for (const ColorSpan& s : chunk.spans) {
        if (s.start < chunk_lo || s.start >= chunk_hi) continue;  // owned by a neighbor chunk
        spans_.push_back(s);
    }
    swept_hi_ = chunk_hi;
}

ColorizeResult SpanTable::slice(uint32_t lo, uint32_t hi) const {
    // start + length cannot overflow: spans lie within a uint32_t-sized file (ABI caps len), so span end <= file_size <= UINT32_MAX.
    ColorizeResult out;
    if (lo >= hi || spans_.empty()) return out;
    auto first = std::lower_bound(
        spans_.begin(), spans_.end(), lo,
        [](const ColorSpan& s, uint32_t v) { return s.start < v; });
    // The predecessor may still overlap lo (non-overlapping => at most one).
    if (first != spans_.begin()) {
        auto prev = std::prev(first);
        if (prev->start + prev->length > lo) first = prev;
    }
    for (auto it = first; it != spans_.end() && it->start < hi; ++it) {
        if (it->start + it->length <= lo) continue;  // ends before the window
        out.spans.push_back(*it);
    }
    return out;
}

}  // namespace wlx::plugin_colorizer::colorize
