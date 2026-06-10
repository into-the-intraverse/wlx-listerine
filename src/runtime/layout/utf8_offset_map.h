#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wlx::runtime::layout {

// UTF-16 -> UTF-8 conversion plus a cumulative byte-offset table (one entry per
// UTF-16 unit + end sentinel) for mapping colorizer byte offsets back to wchar
// indices. The table is non-decreasing and both units of a surrogate pair map
// to the pair's first byte, so a byte offset at a code-point boundary can never
// split the pair. Lone surrogates (invalid UTF-16) are emitted as U+FFFD.
// Shared by the md code-fence layout (whole fence) and the colorizer layout
// (per source line).
struct Utf8Mapping {
    std::string utf8;
    std::vector<uint32_t> wchar_to_byte;  // non-decreasing; size() == text.size() + 1
};
Utf8Mapping utf8_with_offsets(const std::wstring& text);

// Offset-table-only variant of utf8_with_offsets for callers that already hold
// the UTF-8 bytes: the same walk, without allocating the UTF-8 copy.
std::vector<uint32_t> utf8_offsets(const std::wstring& text);

// Map a UTF-8 byte offset to the first wchar index whose byte offset is
// >= byte_off (std::lower_bound; the end sentinel guarantees a hit for any
// in-range offset). `table` is from utf8_with_offsets/utf8_offsets, or nullptr
// for pure-ASCII text, where byte offsets == wchar offsets (identity fast path).
uint32_t byte_to_wchar(const std::vector<uint32_t>* table, uint32_t byte_off);

}  // namespace wlx::runtime::layout
