#include "runtime/host/module_path.h"

namespace wlx::runtime::host {

std::wstring get_module_dir(HMODULE module) {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(module, buf.data(),
                                     static_cast<DWORD>(buf.size()));
        if (n == 0) return L"";
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        // Truncated: grow and retry. Sanity ceiling at 32768 wchars
        // (Windows long-path absolute limit).
        buf.resize(buf.size() * 2);
        if (buf.size() > 32768) return L"";
    }
    auto pos = buf.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? buf.substr(0, pos + 1) : buf;
}

}  // namespace wlx::runtime::host
