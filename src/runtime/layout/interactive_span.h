#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/parser/link_target.h"

#include <d2d1.h>

namespace wlx::runtime::layout {


struct InteractiveSpan {
    parser::LinkTarget target;
    D2D1_RECT_F rect = {};
};

}  // namespace wlx::runtime::layout
