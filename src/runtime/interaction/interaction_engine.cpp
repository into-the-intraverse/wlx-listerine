#include "runtime/interaction/interaction_engine.h"

InteractionEngine::InteractionEngine(const LayoutDocument& layout)
    : layout_(layout) {}

InteractionEngine::HitResult InteractionEngine::hit_test(float x, float y) const {
    HitResult result;

    for (int bi = 0; bi < static_cast<int>(layout_.blocks.size()); bi++) {
        auto& block = layout_.blocks[bi];

        // Quick vertical bounds check
        if (y < block.rect.top || y > block.rect.bottom)
            continue;

        // Check interactive spans within this block
        for (int si = 0; si < static_cast<int>(block.spans.size()); si++) {
            auto& span = block.spans[si];
            if (x >= span.rect.left && x <= span.rect.right &&
                y >= span.rect.top && y <= span.rect.bottom) {
                result.hit = true;
                result.block_index = bi;
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

std::optional<float> InteractionEngine::anchor_y(const std::wstring& slug) const {
    for (auto& anchor : layout_.anchors) {
        if (anchor.slug == slug)
            return anchor.y_offset;
    }
    return std::nullopt;
}
