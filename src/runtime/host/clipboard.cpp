#include "runtime/host/clipboard.h"

#include <cstring>

namespace wlx::runtime::host {

bool copy_to_clipboard(HWND owner, const std::wstring& text) {
    if (text.empty()) return false;
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    bool ok = false;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hg) {
        void* p = GlobalLock(hg);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(hg);
            ok = SetClipboardData(CF_UNICODETEXT, hg) != nullptr;
        }
        // Ownership transfers to the system only on SetClipboardData success.
        if (!ok) GlobalFree(hg);
    }
    CloseClipboard();
    return ok;
}

}  // namespace wlx::runtime::host
