#include "runtime/layout/utf8_offset_map.h"

#include <algorithm>

namespace wlx::runtime::layout {

namespace {

// One code-point walk shared by both entry points: fills the offset table and,
// when `out_utf8` is non-null, emits the UTF-8 bytes in the same pass.
std::vector<uint32_t> walk(const std::wstring& text, std::string* out_utf8) {
    std::vector<uint32_t> table;
    table.reserve(text.size() + 1);  // one per wchar + sentinel
    uint32_t byte_pos = 0;
    for (size_t i = 0; i < text.size(); i++) {
        table.push_back(byte_pos);
        uint32_t cp = text[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < text.size() &&
            text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
            // Surrogate pair -> one 4-byte code point (NOT two 3-byte CESU-8
            // units, which tree-sitter would reject as invalid UTF-8). The low
            // unit repeats the pair's start offset so a span boundary at a
            // code-point edge maps to whole pairs.
            cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            i++;
            table.push_back(byte_pos);
        } else if (cp >= 0xD800 && cp < 0xE000) {
            cp = 0xFFFD;  // lone surrogate: invalid UTF-16; emit U+FFFD
        }
        if (cp < 0x80) {
            if (out_utf8) *out_utf8 += static_cast<char>(cp);
            byte_pos += 1;
        } else if (cp < 0x800) {
            if (out_utf8) {
                *out_utf8 += static_cast<char>(0xC0 | (cp >> 6));
                *out_utf8 += static_cast<char>(0x80 | (cp & 0x3F));
            }
            byte_pos += 2;
        } else if (cp < 0x10000) {
            if (out_utf8) {
                *out_utf8 += static_cast<char>(0xE0 | (cp >> 12));
                *out_utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                *out_utf8 += static_cast<char>(0x80 | (cp & 0x3F));
            }
            byte_pos += 3;
        } else {
            if (out_utf8) {
                *out_utf8 += static_cast<char>(0xF0 | (cp >> 18));
                *out_utf8 += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                *out_utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                *out_utf8 += static_cast<char>(0x80 | (cp & 0x3F));
            }
            byte_pos += 4;
        }
    }
    table.push_back(byte_pos);  // sentinel for end
    return table;
}

}  // namespace

Utf8Mapping utf8_with_offsets(const std::wstring& text) {
    Utf8Mapping m;
    m.utf8.reserve(text.size() * 3);  // typical upper bound (pairs: 4 < 2*3)
    m.wchar_to_byte = walk(text, &m.utf8);
    return m;
}

std::vector<uint32_t> utf8_offsets(const std::wstring& text) {
    return walk(text, nullptr);
}

uint32_t byte_to_wchar(const std::vector<uint32_t>* table, uint32_t byte_off) {
    if (!table) return byte_off;
    auto it = std::lower_bound(table->begin(), table->end(), byte_off);
    return static_cast<uint32_t>(it - table->begin());
}

}  // namespace wlx::runtime::layout
