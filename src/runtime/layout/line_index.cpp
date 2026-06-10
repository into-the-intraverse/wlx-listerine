#include "runtime/layout/line_index.h"

#include <algorithm>
#include <cmath>

namespace wlx::runtime::layout {

using parser::BlockType;

void build_line_index(LayoutDocument& doc) {
    doc.line_tops.clear();
    doc.line_tops.reserve(doc.blocks.size());  // >= 1 top per block
    constexpr float kEps = 0.5f;
    float last = -1.0e9f;

    auto push = [&](float y) {
        if (std::fabs(y - last) > kEps) {
            doc.line_tops.push_back(y);
            last = y;
        }
    };

    for (const auto& block : doc.blocks) {
        // Blockquote containers span their children (same top as the first
        // child) and carry no text of their own — skip them so they can't add
        // a phantom line when the first child's run is inset (code fences).
        if (block.type == BlockType::BlockQuote) continue;
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
        if (!enumerate) continue;

        const std::wstring& text = run.text;
        if (run.layout) {
            // Exact: hit-test the top of each hard-break line.
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] != L'\n') continue;
                UINT32 offset = static_cast<UINT32>(i + 1);  // first char after the break
                DWRITE_HIT_TEST_METRICS m = {};
                float px = 0.0f, py = 0.0f;
                if (SUCCEEDED(run.layout->HitTestTextPosition(offset, FALSE, &px, &py, &m)))
                    push(base + py);
            }
        } else {
            // Lazy skeleton (not yet materialized): keep the logical-line COUNT
            // correct so the gutter numbers are right and stable while scrolling.
            // Positions are estimated by dividing the run's height evenly across
            // its hard-break lines — exact for no-wrap code fences; refined to
            // hit-tested values once the block is materialized (line index is
            // rebuilt then). The number/count never changes on materialize.
            int nbreaks = static_cast<int>(std::count(text.begin(), text.end(), L'\n'));
            if (nbreaks > 0) {
                float h = run.rect.bottom - run.rect.top;
                float line_h = h / static_cast<float>(nbreaks + 1);
                for (int k = 1; k <= nbreaks; ++k)
                    push(base + static_cast<float>(k) * line_h);
            }
        }
    }
}

}  // namespace wlx::runtime::layout
