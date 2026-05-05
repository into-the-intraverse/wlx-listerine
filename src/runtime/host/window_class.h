#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

namespace wlx::runtime::host {

// Register a per-plugin window class with the standard set of styles
// (CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS), the standard cursor
// (IDC_ARROW), and a black background brush. The caller chooses the
// class name and the WndProc.
//
// Returns the ATOM from RegisterClassExW (0 on failure). Caller is
// responsible for caching the ATOM and skipping subsequent calls when
// non-zero — this function does not cache.
ATOM ensure_window_class(HMODULE module, const wchar_t* class_name, WNDPROC wnd_proc);

}  // namespace wlx::runtime::host
