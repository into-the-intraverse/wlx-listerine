#pragma once

#include "runtime/layout/layout_document.h"

namespace wlx::runtime::layout {

// Fill doc.line_tops with one document-space Y per logical line, in order.
//
// Numbering model (viewport-independent):
//   * every block contributes at least one logical line at its text-run top;
//   * Paragraph / CodeFence blocks contribute one additional line per embedded
//     hard break ('\n') — located via IDWriteTextLayout::HitTestTextPosition;
//   * soft word-wrap never advances the count;
//   * blocks sharing a top (table-row cells) collapse to a single line.
void build_line_index(LayoutDocument& doc);

}  // namespace wlx::runtime::layout
