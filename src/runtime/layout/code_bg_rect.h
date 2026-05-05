#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d2d1.h>

namespace wlx::runtime::layout {


struct CodeBgRect {
    D2D1_RECT_F rect = {};  // relative to text run origin
};

}  // namespace wlx::runtime::layout
