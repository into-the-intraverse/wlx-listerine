#pragma once

#include <string>

namespace wlx::runtime::search {


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

}  // namespace wlx::runtime::search
