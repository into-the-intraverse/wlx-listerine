#pragma once

#include "runtime/layout/layout_document.h"
#include "runtime/search/search_match.h"
#include "runtime/search/search_query.h"

#include <string>
#include <vector>

namespace wlx::runtime::search {


class SearchIndex {
public:
    void build(const layout::LayoutDocument& layout);
    std::vector<SearchMatch> find_all(const SearchQuery& q) const;
    bool empty() const { return flat_.empty(); }

private:
    std::wstring flat_;
    std::wstring flat_lower_;
    std::vector<int> block_starts_;
};

}  // namespace wlx::runtime::search
