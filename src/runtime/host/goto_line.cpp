#include "runtime/host/goto_line.h"

namespace wlx::runtime::host {

namespace {
constexpr size_t kMaxDigits = 7;  // up to 9,999,999 lines — fits int
}

GotoStep goto_handle_key(GotoPrompt& p, unsigned vk) {
    if (!p.active) return {GotoAction::Ignore, 0};

    if (vk >= '0' && vk <= '9') {
        if (p.buffer.size() < kMaxDigits)
            p.buffer.push_back(static_cast<wchar_t>(vk));
        return {GotoAction::Redraw, 0};
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        if (p.buffer.size() < kMaxDigits)
            p.buffer.push_back(static_cast<wchar_t>(L'0' + (vk - VK_NUMPAD0)));
        return {GotoAction::Redraw, 0};
    }
    if (vk == VK_BACK) {
        if (!p.buffer.empty()) p.buffer.pop_back();
        return {GotoAction::Redraw, 0};
    }
    if (vk == VK_RETURN) {
        int line = p.buffer.empty() ? 0 : _wtoi(p.buffer.c_str());
        bool jump = !p.buffer.empty();
        p.active = false;
        p.buffer.clear();
        return jump ? GotoStep{GotoAction::Jump, line} : GotoStep{GotoAction::Close, 0};
    }
    if (vk == VK_ESCAPE) {
        p.active = false;
        p.buffer.clear();
        return {GotoAction::Close, 0};
    }
    return {GotoAction::Ignore, 0};
}

float line_scroll_target(const std::vector<float>& line_tops, int n, float max_scroll_y) {
    if (line_tops.empty()) return 0.0f;
    int total = static_cast<int>(line_tops.size());
    n = std::clamp(n, 1, total);
    return std::clamp(line_tops[static_cast<size_t>(n - 1)], 0.0f, max_scroll_y);
}

}  // namespace wlx::runtime::host
