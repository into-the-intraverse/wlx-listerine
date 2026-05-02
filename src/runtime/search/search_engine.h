#pragma once

#include "runtime/layout/layout_engine.h"

#include <string>
#include <vector>

struct SearchMatch {
    int block_index = -1;
    int char_start = 0;  // offset into block's flattened text (UTF-16 code units)
    int char_end = 0;
};

struct SearchQuery {
    std::wstring needle;
    bool match_case = false;
    bool whole_words = false;
    bool backwards = false;  // cursor-advancement hint; not used by find_all
};

inline bool operator==(const SearchQuery& a, const SearchQuery& b) {
    return a.needle == b.needle
        && a.match_case == b.match_case
        && a.whole_words == b.whole_words;
    // `backwards` intentionally excluded — it controls advancement, not results
}
inline bool operator!=(const SearchQuery& a, const SearchQuery& b) { return !(a == b); }

class SearchIndex {
public:
    void build(const LayoutDocument& layout);
    std::vector<SearchMatch> find_all(const SearchQuery& q) const;
    bool empty() const { return flat_.empty(); }

private:
    std::wstring flat_;
    std::wstring flat_lower_;
    std::vector<int> block_starts_;
};
