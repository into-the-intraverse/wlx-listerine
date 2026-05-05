#pragma once

#include <string>

namespace wlx::runtime::layout {


struct AnchorEntry {
    std::wstring slug;
    float y_offset = 0;
};

}  // namespace wlx::runtime::layout
