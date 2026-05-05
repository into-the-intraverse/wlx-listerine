#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>

namespace wlx::runtime::host {

// Replace the system clipboard contents with `text` as CF_UNICODETEXT.
// `owner` is the HWND that owns the clipboard during the open/close;
// must be non-null for OpenClipboard to succeed under the standard
// Windows ownership rules. Returns false if `text` is empty or
// OpenClipboard fails (typical: another app owns it). Allocation
// failures are silently swallowed — the clipboard is still cleared.
bool copy_to_clipboard(HWND owner, const std::wstring& text);

}  // namespace wlx::runtime::host
