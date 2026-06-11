#include "runtime/interaction/interaction_engine.h"
#include <cwctype>

namespace wlx::runtime::interaction {

using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;

InteractionEngine::InteractionEngine(const LayoutDocument& layout)
    : layout_(layout) {}

InteractionEngine::HitResult InteractionEngine::hit_test(float x, float y) const {
    HitResult result;
    // In grid mode blocks[bi] represents source line (first_block_line + bi);
    // HitResult.block_index carries the PUBLIC source-line index.
    // Identity when first_block_line == 0 (whole-file docs and all md layouts).
    const int line_base = layout_.first_block_line;

    for (int bi = 0; bi < static_cast<int>(layout_.blocks.size()); bi++) {
        auto& block = layout_.blocks[bi];

        // Quick vertical bounds check. Blocks are laid out in non-decreasing
        // rect.top order (table cells in a row share a top), so once a block
        // starts below y nothing later can contain it.
        if (block.rect.top > y)
            break;
        if (y > block.rect.bottom)
            continue;

        // Check interactive spans within this block
        for (int si = 0; si < static_cast<int>(block.spans.size()); si++) {
            auto& span = block.spans[si];
            if (x >= span.rect.left && x <= span.rect.right &&
                y >= span.rect.top && y <= span.rect.bottom) {
                result.hit = true;
                result.block_index = line_base + bi;
                result.span_index = si;
                result.target = span.target;
                return result;
            }
        }
    }

    return result;
}

InteractionEngine::LinkAction InteractionEngine::resolve(const LinkTarget& target) const {
    LinkAction action;

    switch (target.kind) {
    case LinkKind::InternalAnchor: {
        auto ay = anchor_y(target.anchor_fragment);
        if (ay.has_value()) {
            action.action = Action::ScrollToAnchor;
            action.scroll_y = *ay;
        }
        break;
    }

    case LinkKind::RelativeDoc:
        action.action = Action::ReloadDocument;
        action.target = target.url;
        if (!target.anchor_fragment.empty())
            action.target += L"#" + target.anchor_fragment;
        break;

    case LinkKind::ExternalUrl:
        action.action = Action::OpenExternal;
        action.target = target.url;
        break;
    }

    return action;
}

static std::wstring normalize_fragment(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    while (!s.empty() && s.front() == L'-') s.erase(s.begin());
    while (!s.empty() && s.back()  == L'-') s.pop_back();
    return s;
}

std::optional<float> InteractionEngine::anchor_y(const std::wstring& slug) const {
    std::wstring needle = normalize_fragment(slug);
    for (auto& anchor : layout_.anchors) {
        if (anchor.slug == needle)
            return anchor.y_offset;
    }
    return std::nullopt;
}

}  // namespace wlx::runtime::interaction
