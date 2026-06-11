#pragma once

#include "runtime/layout/layout_document.h"
#include "runtime/search/search_match.h"
#include "runtime/search/search_query.h"

#include <functional>
#include <string>
#include <vector>

namespace wlx::runtime::search {


class SearchIndex {
public:
    void build(const layout::LayoutDocument& layout);

    // Grid mode: build from per-line text callbacks instead of layout blocks.
    // block_starts_ positions are then LINE indices, so find_all emits matches
    // in the public (source-line) index space. line_text must return the
    // EXPANDED (tab-expanded) line so match char offsets line up with what
    // hit-testing and the renderer produce.
    void build_lines(int line_count,
                     const std::function<std::wstring(int)>& line_text);

    std::vector<SearchMatch> find_all(const SearchQuery& q) const;
    bool empty() const { return flat_.empty(); }

private:
    std::wstring flat_;
    std::wstring flat_lower_;
    std::vector<int> block_starts_;
};

}  // namespace wlx::runtime::search
