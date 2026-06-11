#pragma once

#include "runtime/layout/layout_document.h"
#include "runtime/parser/document.h"

#include <cstddef>

namespace wlx::runtime::cache {

// Per-heap-allocation overhead proxy (header + rounding + slack), calibrated
// against the md bench's per-phase working-set rows (big.md lazy skeleton:
// ~84 MB real vs ~8 MB summed struct/text bytes — most of the gap is the
// thousands of separate small allocations + the recipe/ctx state behind the
// materialize closure that this walk cannot see).
inline constexpr size_t kPerAllocOverhead = 128;
inline constexpr size_t kPerBlockHiddenState = 1536;  // recipe + vector headers + slack share


// Approximate heap bytes occupied by a parsed Document (block tree + inline text).
// One level of children is included; deeper nesting (rare in practice) is not.
inline size_t estimate_document_memory(const parser::Document& doc) {
    size_t bytes = sizeof(parser::Document);
    for (auto& block : doc.blocks) {
        bytes += sizeof(parser::BlockNode);
        for (auto& inl : block.inlines)
            bytes += sizeof(parser::InlineNode) + inl.text.size() * sizeof(wchar_t)
                   + kPerAllocOverhead;
        for (auto& child : block.children) {
            bytes += sizeof(parser::BlockNode);
            for (auto& inl : child.inlines)
                bytes += sizeof(parser::InlineNode) + inl.text.size() * sizeof(wchar_t)
                       + kPerAllocOverhead;
        }
    }
    return bytes;
}

// Approximate heap bytes occupied by a LayoutDocument (blocks, runs, spans, anchors).
inline size_t estimate_layout_memory(const layout::LayoutDocument& layout) {
    size_t bytes = sizeof(layout::LayoutDocument);
    for (auto& block : layout.blocks) {
        bytes += sizeof(layout::LayoutBlock) + kPerBlockHiddenState;
        for (auto& run : block.text_runs)
            bytes += sizeof(layout::TextRun) + run.text.size() * sizeof(wchar_t)
                   + kPerAllocOverhead;
        bytes += block.spans.size() * sizeof(layout::InteractiveSpan) + kPerAllocOverhead;
    }
    bytes += layout.anchors.size() * sizeof(layout::AnchorEntry);
    return bytes;
}


}  // namespace wlx::runtime::cache
