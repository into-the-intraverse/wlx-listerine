#pragma once

#include <string>
#include <string_view>

namespace wlx::runtime::host {

// Pure: collapses internal whitespace runs to a single space, trims
// leading/trailing whitespace, truncates to 1500 wchars, percent-encodes
// the UTF-8 bytes per RFC 3986 (unreserved set: A-Z a-z 0-9 - . _ ~),
// and prepends the Google search prefix. Returns an empty string for
// empty / whitespace-only input.
std::wstring build_google_search_url(std::wstring_view query);

// Side-effect: builds the URL via build_google_search_url and ShellExecuteWs
// it. No-op on empty result. Failures (no shell handler) trace via
// WLX_TRACE; no user-visible error.
void search_with_google(std::wstring_view query);

}  // namespace wlx::runtime::host
