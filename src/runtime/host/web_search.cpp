#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/host/web_search.h"

#include <windows.h>
#include <shellapi.h>

#include <cwctype>
#include <string>

#define WLX_TRACE_TAG L"wlx-host"
#include "runtime/diagnostics/wlx_trace.h"

namespace wlx::runtime::host {

namespace {

constexpr size_t kMaxQueryWchars = 1500;
constexpr wchar_t kPrefix[] = L"https://www.google.com/search?q=";

// RFC 3986 unreserved set; everything else is percent-encoded.
bool is_unreserved(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ||
           (c >= L'a' && c <= L'z') ||
           (c >= L'0' && c <= L'9') ||
           c == L'-' || c == L'.' || c == L'_' || c == L'~';
}

std::wstring normalize_whitespace(std::wstring_view in) {
    std::wstring out;
    out.reserve(in.size());
    bool prev_space = true;  // start as true so leading whitespace is dropped
    for (wchar_t c : in) {
        if (iswspace(static_cast<wint_t>(c))) {
            if (!prev_space) {
                out.push_back(L' ');
                prev_space = true;
            }
        } else {
            out.push_back(c);
            prev_space = false;
        }
    }
    // Strip the trailing space left over by a whitespace run at end-of-input.
    // The collapse loop emits a space the moment whitespace begins (when
    // prev_space becomes false → true); if input ends mid-whitespace, that
    // emitted space has nothing legitimate after it.
    if (!out.empty() && out.back() == L' ')
        out.pop_back();
    return out;
}

std::wstring percent_encode_utf8(std::wstring_view in) {
    // Convert UTF-16 input to UTF-8, then percent-encode per byte.
    int needed = WideCharToMultiByte(CP_UTF8, 0, in.data(),
                                     static_cast<int>(in.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string utf8(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, in.data(), static_cast<int>(in.size()),
                        utf8.data(), needed, nullptr, nullptr);

    static constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(utf8.size() * 3);
    for (unsigned char b : utf8) {
        // Reapply unreserved test on the byte value (ASCII subset).
        if (is_unreserved(static_cast<wchar_t>(b))) {
            out.push_back(static_cast<wchar_t>(b));
        } else {
            out.push_back(L'%');
            out.push_back(hex[(b >> 4) & 0xF]);
            out.push_back(hex[b & 0xF]);
        }
    }
    return out;
}

}  // namespace

std::wstring build_google_search_url(std::wstring_view query) {
    std::wstring normalized = normalize_whitespace(query);
    if (normalized.empty()) return {};
    if (normalized.size() > kMaxQueryWchars)
        normalized.resize(kMaxQueryWchars);
    // Construct the result in-place from the prefix literal, then append the
    // encoded query. Avoids the temporary `std::wstring(kPrefix)` that
    // `kPrefix + encode(...)` would build before the concatenation.
    std::wstring url(kPrefix);
    url += percent_encode_utf8(normalized);
    return url;
}

void search_with_google(std::wstring_view query) {
    auto url = build_google_search_url(query);
    if (url.empty()) return;
    HINSTANCE hi = ShellExecuteW(nullptr, L"open", url.c_str(),
                                 nullptr, nullptr, SW_SHOW);
    if (reinterpret_cast<INT_PTR>(hi) <= 32) {
        // The HINSTANCE return is overloaded to carry an integer error code
        // (1..31, per MSDN's ShellExecute table) when the call fails. Print
        // it as a signed integer so traces are readable; %p would just show
        // a pointer-width hex value.
        WLX_TRACE(L"search_with_google: ShellExecuteW failed (code %lld)",
                  static_cast<long long>(reinterpret_cast<INT_PTR>(hi)));
    }
}

}  // namespace wlx::runtime::host
