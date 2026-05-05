#pragma once


namespace wlx::runtime::layout {

struct TextPosition {
    int block_index = -1;
    int char_offset = 0;

    bool valid() const { return block_index >= 0; }

    bool operator==(const TextPosition& o) const {
        return block_index == o.block_index && char_offset == o.char_offset;
    }
    bool operator!=(const TextPosition& o) const { return !(*this == o); }
    bool operator<(const TextPosition& o) const {
        if (block_index != o.block_index) return block_index < o.block_index;
        return char_offset < o.char_offset;
    }
    bool operator>(const TextPosition& o) const { return o < *this; }
    bool operator<=(const TextPosition& o) const { return !(o < *this); }
    bool operator>=(const TextPosition& o) const { return !(*this < o); }
};

}  // namespace wlx::runtime::layout
