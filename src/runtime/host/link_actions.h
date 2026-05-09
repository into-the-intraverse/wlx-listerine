#pragma once

#include "runtime/diagnostics/wlx_trace.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace wlx::runtime::host {

// Open `url` in the system default handler via ShellExecuteW.
// Logs a WLX_TRACE diagnostic on failure (using whatever WLX_TRACE_TAG
// the including translation unit defined). Header-inline so the trace
// macro picks up the per-plugin tag at the include site.
inline void open_external_url(const std::wstring& url) {
    HINSTANCE hi = ShellExecuteW(nullptr, L"open", url.c_str(),
                                 nullptr, nullptr, SW_SHOW);
    if (reinterpret_cast<INT_PTR>(hi) <= 32) {
        WLX_TRACE(L"open_external_url: ShellExecuteW failed (code %lld) for %s",
                  static_cast<long long>(reinterpret_cast<INT_PTR>(hi)),
                  url.c_str());
    }
}

}  // namespace wlx::runtime::host
