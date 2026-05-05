#include "runtime/host/module_path.h"

namespace wlx::runtime::host {

std::wstring get_module_dir(HMODULE module) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(module, path, MAX_PATH);
    std::wstring dir(path);
    auto pos = dir.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? dir.substr(0, pos + 1) : dir;
}

}  // namespace wlx::runtime::host
