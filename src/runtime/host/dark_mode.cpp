#include "runtime/host/dark_mode.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace wlx::runtime::host {

// DWMWA_USE_IMMERSIVE_DARK_MODE (value 20) — available since Win10 20H1.
// Older SDKs may not define it.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

void apply_dark_mode(HWND hwnd, bool dark) {
    BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
    // SetWindowTheme themes the scrollbar (DwmSetWindowAttribute only affects title bar)
    SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

}  // namespace wlx::runtime::host
