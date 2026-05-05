#include "runtime/host/window_class.h"

namespace wlx::runtime::host {

ATOM ensure_window_class(HMODULE module, const wchar_t* class_name, WNDPROC wnd_proc) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = module;
    // IDC_ARROW expands via MAKEINTRESOURCE which is char* unless UNICODE is
    // defined for this TU. Use the explicit wide form so the lifted module
    // doesn't need to set UNICODE itself.
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = class_name;
    return RegisterClassExW(&wc);
}

}  // namespace wlx::runtime::host
