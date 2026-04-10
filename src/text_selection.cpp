#include "layout_engine.h"
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
