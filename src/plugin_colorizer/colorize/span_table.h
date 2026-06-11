#pragma once

#include "core_dll/colorizer/colorize_result.h"

#include <cstdint>
#include <vector>

namespace wlx::plugin_colorizer::colorize {

// Whole-file syntax colors in compact flat form: ColorSpans sorted by byte
// start, non-overlapping (QueryHighlighter's output contract), plus a
// watermark of how far the linear sweep has progressed. Once complete() the
// table replaces the retained tree-sitter tree: scroll coloring becomes a
// binary-searched slice instead of a tree query.
//
// Single writer: the sweep appends in file order (chunk_lo == swept_hi()), so
// the vector is sorted by construction and never needs a merge.
class SpanTable {
public:
    // Append one sweep chunk's highlight result for the byte window
    // [chunk_lo, chunk_hi). Keeps only spans whose START lies inside the
    // window: highlight_range returns every span OVERLAPPING the window, so a
    // span crossing the previous chunk's end arrives twice — its start
    // attributes it to exactly one chunk. Requires chunk_lo == swept_hi().
    void append_chunk(const wlx::core::colorizer::ColorizeResult& chunk,
                      uint32_t chunk_lo, uint32_t chunk_hi);

    // All spans overlapping [lo, hi), in start order. Because spans are
    // non-overlapping and sorted, at most ONE span starting before lo can
    // overlap it (the predecessor) — step back one after the lower_bound.
    wlx::core::colorizer::ColorizeResult slice(uint32_t lo, uint32_t hi) const;

    bool complete(uint32_t file_size) const { return swept_hi_ >= file_size; }
    uint32_t swept_hi() const { return swept_hi_; }
    size_t size() const { return spans_.size(); }
    size_t approx_bytes() const {
        return spans_.capacity() * sizeof(wlx::core::colorizer::ColorSpan);
    }
    void clear() { spans_.clear(); spans_.shrink_to_fit(); swept_hi_ = 0; }

private:
    std::vector<wlx::core::colorizer::ColorSpan> spans_;
    uint32_t swept_hi_ = 0;
};

}  // namespace wlx::plugin_colorizer::colorize
