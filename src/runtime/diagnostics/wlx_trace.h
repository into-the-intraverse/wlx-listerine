#pragma once

// Compile-time tracer for diagnosing how Total Commander's Lister talks to
// the WLX plugin (which messages arrive at WndProc, which exports get called,
// what wParam/lParam carry). Output goes to OutputDebugStringW — capture with
// Sysinternals DebugView (run as admin if TC also runs elevated).
//
// Enable from CMake configure step:
//     cmake --preset conan-default -DWLX_TRACE_ENABLE=ON
//
// Each plugin sets WLX_TRACE_TAG before including this header so md vs.
// colorizer lines can be told apart in the same DebugView capture.

#ifdef WLX_TRACE_ENABLE
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#endif

namespace wlx::runtime::diagnostics {

#ifdef WLX_TRACE_ENABLE

#ifndef WLX_TRACE_TAG
#define WLX_TRACE_TAG L"wlx"
#endif

inline void wlx_trace_emit_(const wchar_t* fmt, ...) {
    wchar_t body[768];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(body, _countof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    wchar_t line[1024];
    _snwprintf_s(line, _countof(line), _TRUNCATE,
                 L"[%s] %s\n", WLX_TRACE_TAG, body);
    OutputDebugStringW(line);
}

#define WLX_TRACE(...) wlx_trace_emit_(__VA_ARGS__)

// Decode common Win32 message names to keep traces readable.
inline const wchar_t* wlx_trace_msg_name_(UINT msg) {
    switch (msg) {
    case WM_KEYDOWN:    return L"WM_KEYDOWN";
    case WM_KEYUP:      return L"WM_KEYUP";
    case WM_SYSKEYDOWN: return L"WM_SYSKEYDOWN";
    case WM_SYSKEYUP:   return L"WM_SYSKEYUP";
    case WM_CHAR:       return L"WM_CHAR";
    case WM_SYSCHAR:    return L"WM_SYSCHAR";
    case WM_COMMAND:    return L"WM_COMMAND";
    case WM_SYSCOMMAND: return L"WM_SYSCOMMAND";
    case WM_NOTIFY:     return L"WM_NOTIFY";
    case WM_USER:       return L"WM_USER";
    default:            return L"?";
    }
}

#else  // WLX_TRACE_ENABLE not defined — strip to no-ops

#define WLX_TRACE(...) ((void)0)
inline const wchar_t* wlx_trace_msg_name_(unsigned) { return L"?"; }

#endif  // WLX_TRACE_ENABLE

}  // namespace wlx::runtime::diagnostics
