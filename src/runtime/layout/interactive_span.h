#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/parser/document_model.h"

#include <d2d1.h>

struct InteractiveSpan {
    LinkTarget target;
    D2D1_RECT_F rect = {};
};
