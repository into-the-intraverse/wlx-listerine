#pragma once

#include "search_engine.h"
#include "runtime/layout/layout_engine.h"

#include <concepts>
#include <type_traits>
#include <vector>

template <typename V>
concept SearchState = requires(V& v) {
    { v.layout };
    { v.search_index }  -> std::same_as<SearchIndex&>;
    { v.matches }       -> std::same_as<std::vector<SearchMatch>&>;
    { v.current_match } -> std::same_as<int&>;
    { v.last_query }    -> std::same_as<SearchQuery&>;
    { v.index_dirty }   -> std::same_as<bool&>;
};

struct SearchStepResult {
    bool has_match;
    int cursor;
    std::vector<SearchMatch> matches;
    bool index_was_rebuilt;
};

template <SearchState V>
SearchStepResult search_step(V& vs, const SearchQuery& q, bool findfirst) {
    bool rebuilt = false;
    if (vs.index_dirty) {
        vs.search_index.build(*vs.layout);
        vs.index_dirty = false;
        rebuilt = true;
    }
    const bool query_changed = q != vs.last_query;
    const bool requery = findfirst || rebuilt || query_changed;
    if (requery) {
        vs.matches = vs.search_index.find_all(q);
        if (findfirst || query_changed) {
            vs.current_match = -1;
        } else if (vs.current_match >= static_cast<int>(vs.matches.size())) {
            vs.current_match = static_cast<int>(vs.matches.size()) - 1;
        }
        vs.last_query = q;
    }
    if (vs.matches.empty()) {
        vs.current_match = -1;
        return {false, -1, {}, rebuilt};
    }
    const int n = static_cast<int>(vs.matches.size());
    vs.current_match = q.backwards
        ? (vs.current_match <= 0 ? n - 1 : vs.current_match - 1)
        : (vs.current_match + 1) % n;
    return {true, vs.current_match, vs.matches, rebuilt};
}
