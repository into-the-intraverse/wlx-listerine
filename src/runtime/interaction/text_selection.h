#pragma once

#include "runtime/layout/layout_document.h"
#include "runtime/layout/text_position.h"

#include <string>
#include <utility>

namespace wlx::runtime::interaction {


// Extract the wide-string contents of [start, end] from the laid-out
// document, joining blocks with CRLF and prefixing list-item bullet text
// when the selection covers the start of an item.
std::wstring extract_selected_text(const layout::LayoutDocument& layout,
                                   layout::TextPosition start, layout::TextPosition end);

// Word-class extents around `offset` in `text`. Treats whitespace and
// punctuation each as their own class (so double-clicking inside a run
// of whitespace selects only that whitespace, not the adjacent words).
std::pair<int, int> find_word_boundaries(const std::wstring& text, int offset);

}  // namespace wlx::runtime::interaction
