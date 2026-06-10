#pragma once

#include "runtime/diagnostics/wlx_trace.h"

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <string_view>

namespace wlx::runtime::host {

// True if `url` is safe to hand to ShellExecuteW "open". Untrusted document
// text reaches open_external_url (auto-linked URLs, markdown hrefs), and
// ShellExecuteW would happily launch local/UNC executables — so only a short
// scheme allowlist gets through: no exemptions for local paths of any kind
// (the EditConfig menu action goes through open_config_file below instead).
inline bool is_openable_url(std::wstring_view url) {
    constexpr std::wstring_view kAllowedSchemes[] = {
        L"https://", L"http://", L"ftp://", L"mailto:",
    };
    auto lower = [](wchar_t c) {
        return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    };
    auto starts_with_ci = [&](std::wstring_view prefix) {
        if (url.size() <= prefix.size()) return false;  // require a non-empty body
        for (size_t i = 0; i < prefix.size(); ++i)
            if (lower(url[i]) != prefix[i]) return false;
        return true;
    };
    for (auto scheme : kAllowedSchemes)
        if (starts_with_ci(scheme)) return true;
    return false;
}

// Open `url` in the system default handler via ShellExecuteW. URLs that fail
// the is_openable_url allowlist are silently dropped (traced) — file:// and
// friends from untrusted viewed text must never launch local executables.
// Logs a WLX_TRACE diagnostic on failure (using whatever WLX_TRACE_TAG
// the including translation unit defined). Header-inline so the trace
// macro picks up the per-plugin tag at the include site.
inline void open_external_url(const std::wstring& url) {
    if (!is_openable_url(url)) {
        WLX_TRACE(L"open_external_url: blocked non-allowlisted url %s",
                  url.c_str());
        return;
    }
    HINSTANCE hi = ShellExecuteW(nullptr, L"open", url.c_str(),
                                 nullptr, nullptr, SW_SHOW);
    if (reinterpret_cast<INT_PTR>(hi) <= 32) {
        WLX_TRACE(L"open_external_url: ShellExecuteW failed (code %lld) for %s",
                  static_cast<long long>(reinterpret_cast<INT_PTR>(hi)),
                  url.c_str());
    }
}

// TRUSTED opener for the plugin's own config file (EditConfig menu action):
// ShellExecuteW "open" with NO is_openable_url allowlist, so module-dir
// install locations of every shape work (drive-letter and UNC alike).
// Callers must only ever pass the plugin-computed config path — NEVER
// document content or anything else attacker-influenced.
inline void open_config_file(const std::wstring& path) {
    HINSTANCE hi = ShellExecuteW(nullptr, L"open", path.c_str(),
                                 nullptr, nullptr, SW_SHOW);
    if (reinterpret_cast<INT_PTR>(hi) <= 32) {
        WLX_TRACE(L"open_config_file: ShellExecuteW failed (code %lld) for %s",
                  static_cast<long long>(reinterpret_cast<INT_PTR>(hi)),
                  path.c_str());
    }
}

}  // namespace wlx::runtime::host
