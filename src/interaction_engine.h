#pragma once

#include "layout_engine.h"
#include <optional>

class InteractionEngine {
public:
    explicit InteractionEngine(const LayoutDocument& layout);

    struct HitResult {
        bool hit = false;
        int block_index = -1;
        int span_index = -1;
        LinkTarget target;
    };

    HitResult hit_test(float x, float y) const;

    enum class Action { None, ScrollToAnchor, ReloadDocument, OpenExternal };

    struct LinkAction {
        Action action = Action::None;
        std::wstring target;
        float scroll_y = 0;
    };

    LinkAction resolve(const LinkTarget& target) const;
    std::optional<float> anchor_y(const std::wstring& slug) const;

private:
    const LayoutDocument& layout_;
};
