#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d2d1.h>

struct CodeBgRect {
    D2D1_RECT_F rect = {};  // relative to text run origin
};
