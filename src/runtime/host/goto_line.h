#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "runtime/host/scroll_handler.h"

namespace wlx::runtime::host {

// Inline "go to line" prompt state. One per window.
struct GotoPrompt {
    bool active = false;
    std::wstring buffer;  // digits typed so far
};

enum class GotoAction { Ignore, Redraw, Jump, Close };

struct GotoStep {
    GotoAction action = GotoAction::Ignore;
    int line = 0;  // 1-based; valid only when action == Jump
};

// Pure state-machine step for one virtual-key while the prompt is active.
// Mutates `p`; returns what the host should do. No Win32 side effects.
GotoStep goto_handle_key(GotoPrompt& p, unsigned vk);

// Pure: document-space scroll_y that puts logical line `n` (1-based) at the
// viewport top, clamped to [1,total] and to [0,max_scroll_y]. 0 if empty.
float line_scroll_target(const std::vector<float>& line_tops, int n, float max_scroll_y);

// Scroll the view so logical line `n` is at the top. Uses the existing
// Scrollable duck-typing (v.layout->line_tops, v.scroll_y, v.max_scroll_y).
template <Scrollable V>
void scroll_to_line(V& v, int n) {
    if (!v.layout || v.layout->line_tops.empty()) return;
    v.scroll_y = line_scroll_target(v.layout->line_tops, n, v.max_scroll_y);
    update_scrollbar(v);
    if (v.hwnd) InvalidateRect(v.hwnd, nullptr, FALSE);
}

}  // namespace wlx::runtime::host
