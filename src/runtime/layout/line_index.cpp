#include "runtime/layout/line_index.h"

#include <cmath>

namespace wlx::runtime::layout {

using parser::BlockType;

void build_line_index(LayoutDocument& doc) {
    doc.line_tops.clear();
    constexpr float kEps = 0.5f;
    float last = -1.0e9f;

    auto push = [&](float y) {
        if (std::fabs(y - last) > kEps) {
            doc.line_tops.push_back(y);
            last = y;
        }
    };

    for (const auto& block : doc.blocks) {
        if (block.text_runs.empty()) {
            push(block.rect.top);
            continue;
        }

        const TextRun& run = block.text_runs.front();
        const float base = run.rect.top;  // code fences inset the run below the block top
        push(base);

        // Only paragraphs and code fences carry meaningful hard breaks. Other
        // block types (table cells, list items, headings) stay one line so a
        // multi-line table cell can't desync the per-row collapse above.
        const bool enumerate =
            (block.type == BlockType::Paragraph || block.type == BlockType::CodeFence);
        if (!enumerate || !run.layout) continue;

        const std::wstring& text = run.text;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] != L'\n') continue;
            UINT32 offset = static_cast<UINT32>(i + 1);  // first char after the break
            DWRITE_HIT_TEST_METRICS m = {};
            float px = 0.0f, py = 0.0f;
            if (SUCCEEDED(run.layout->HitTestTextPosition(offset, FALSE, &px, &py, &m)))
                push(base + py);
        }
    }
}

}  // namespace wlx::runtime::layout
