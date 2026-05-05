#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/search/search_index.h"

#include <windows.h>
#include <algorithm>
#include <cwctype>

namespace wlx::runtime::search {

using namespace wlx::runtime::layout;

static std::wstring to_lower(std::wstring s) {
    if (!s.empty())
        CharLowerBuffW(&s[0], static_cast<DWORD>(s.size()));
    return s;
}

// Locale-dependent — iswalnum's behavior for non-ASCII alphanumerics (e.g.
// Cyrillic, Greek) depends on the thread's C locale. Acceptable for a file
// viewer's search UX; tightening this would require ICU or a custom table.
static bool is_word_char(wchar_t c) {
    return iswalnum(static_cast<wint_t>(c)) || c == L'_';
}

void SearchIndex::build(const LayoutDocument& layout) {
    flat_.clear();
    flat_lower_.clear();
    block_starts_.clear();
    block_starts_.reserve(layout.blocks.size());

    for (size_t i = 0; i < layout.blocks.size(); i++) {
        if (i > 0) flat_.push_back(L'\n');
        block_starts_.push_back(static_cast<int>(flat_.size()));
        for (const auto& run : layout.blocks[i].text_runs) {
            flat_ += run.text;
        }
    }
    flat_lower_ = to_lower(flat_);
}

std::vector<SearchMatch> SearchIndex::find_all(const SearchQuery& q) const {
    std::vector<SearchMatch> out;
    if (q.needle.empty() || flat_.empty()) return out;
    if (q.needle.size() > flat_.size()) return out;

    const std::wstring& hay = q.match_case ? flat_ : flat_lower_;
    const std::wstring ndl = q.match_case ? q.needle : to_lower(q.needle);

    size_t pos = 0;
    while ((pos = hay.find(ndl, pos)) != std::wstring::npos) {
        const size_t end = pos + ndl.size();

        auto it = std::upper_bound(block_starts_.begin(), block_starts_.end(),
                                   static_cast<int>(pos));
        const int bi = static_cast<int>(it - block_starts_.begin()) - 1;
        const int local_start = static_cast<int>(pos) - block_starts_[bi];
        const int local_end   = static_cast<int>(end) - block_starts_[bi];

        bool crosses_boundary = false;
        if (bi + 1 < static_cast<int>(block_starts_.size())) {
            const int next_block_start = block_starts_[bi + 1];
            // The '\n' separator sits at next_block_start - 1; a match must end before it.
            if (static_cast<int>(end) > next_block_start - 1)
                crosses_boundary = true;
        }

        bool ok = !crosses_boundary;
        if (ok && q.whole_words) {
            if (pos > 0 && is_word_char(flat_[pos - 1])) ok = false;
            if (end < flat_.size() && is_word_char(flat_[end])) ok = false;
        }

        if (ok) {
            out.push_back({bi, local_start, local_end});
            pos = end;
        } else {
            pos = pos + 1;
        }
    }
    return out;
}

}  // namespace wlx::runtime::search
