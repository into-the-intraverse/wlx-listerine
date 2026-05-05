#include "runtime/host/clipboard.h"

#include <cstring>

namespace wlx::runtime::host {

bool copy_to_clipboard(HWND owner, const std::wstring& text) {
    if (text.empty()) return false;
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hg) {
        void* p = GlobalLock(hg);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(hg);
            SetClipboardData(CF_UNICODETEXT, hg);
        }
    }
    CloseClipboard();
    return true;
}

}  // namespace wlx::runtime::host
