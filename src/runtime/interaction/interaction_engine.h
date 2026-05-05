#pragma once

#include "runtime/layout/layout_document.h"
#include "runtime/parser/link_target.h"

#include <optional>
#include <string>

namespace wlx::runtime::interaction {

class InteractionEngine {
public:
    explicit InteractionEngine(const layout::LayoutDocument& layout);

    struct HitResult {
        bool hit = false;
        int block_index = -1;
        int span_index = -1;
        parser::LinkTarget target;
    };

    HitResult hit_test(float x, float y) const;

    enum class Action { None, ScrollToAnchor, ReloadDocument, OpenExternal };

    struct LinkAction {
        Action action = Action::None;
        std::wstring target;
        float scroll_y = 0;
    };

    LinkAction resolve(const parser::LinkTarget& target) const;
    std::optional<float> anchor_y(const std::wstring& slug) const;

private:
    const layout::LayoutDocument& layout_;
};

}  // namespace wlx::runtime::interaction
