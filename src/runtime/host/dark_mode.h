#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

namespace wlx::runtime::host {

// Apply or remove the Win11 immersive dark-mode title bar + scrollbar theme
// to a top-level child window. Idempotent; safe to call from WM_THEMECHANGED.
void apply_dark_mode(HWND hwnd, bool dark);

}  // namespace wlx::runtime::host
