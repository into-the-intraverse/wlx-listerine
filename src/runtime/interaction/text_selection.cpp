#include "runtime/layout/layout_engine.h"
#include <algorithm>

static std::wstring block_full_text(const LayoutBlock& block) {
    std::wstring result;
    for (auto& run : block.text_runs)
        result += run.text;
    return result;
}

std::wstring extract_selected_text(const LayoutDocument& layout,
                                   TextPosition start, TextPosition end) {
    if (!start.valid() || !end.valid()) return {};
    if (end < start) std::swap(start, end);

    std::wstring result;
    int block_count = static_cast<int>(layout.blocks.size());

    for (int i = start.block_index; i <= end.block_index && i < block_count; i++) {
        auto& block = layout.blocks[i];
        std::wstring full = block_full_text(block);

        if (full.empty()) {
            if (!result.empty()) result += L"\r\n";
            continue;
        }

        int from = 0;
        int to = static_cast<int>(full.size());

        if (i == start.block_index)
            from = std::clamp(start.char_offset, 0, to);
        if (i == end.block_index)
            to = std::clamp(end.char_offset, 0, to);

        if (from >= to && i == start.block_index && i == end.block_index)
            continue;

        if (!result.empty()) result += L"\r\n";

        if ((block.type == BlockType::ListItem || block.type == BlockType::TaskList)
            && !block.bullet_text.empty() && from == 0) {
            result += block.bullet_text;
        }

        result += full.substr(static_cast<size_t>(from),
                              static_cast<size_t>(to - from));
    }

    return result;
}

std::pair<int, int> find_word_boundaries(const std::wstring& text, int offset) {
    int len = static_cast<int>(text.size());
    if (len == 0) return {0, 0};
    offset = std::clamp(offset, 0, len - 1);

    auto is_word_char = [](wchar_t c) {
        return !iswspace(c) && !iswpunct(c);
    };

    bool on_word = is_word_char(text[offset]);

    int start = offset;
    int end = offset;

    if (on_word) {
        while (start > 0 && is_word_char(text[start - 1])) start--;
        while (end < len && is_word_char(text[end])) end++;
    } else {
        auto same_class = [&](wchar_t c) {
            return !is_word_char(c) && iswspace(c) == iswspace(text[offset]);
        };
        while (start > 0 && same_class(text[start - 1])) start--;
        while (end < len && same_class(text[end])) end++;
    }

    return {start, end};
}
