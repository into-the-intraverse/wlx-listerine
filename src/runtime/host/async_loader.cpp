#include "runtime/host/async_loader.h"

#include <mutex>

namespace wlx::runtime::host {

// Anchor symbol so GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS resolves to the module
// this code is linked into (the .wlx64), not the EXE.
static void pin_anchor() {}

void pin_plugin_module_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        HMODULE h = nullptr;
        // PIN makes the reference permanent (refcount maxed; never unloads);
        // FROM_ADDRESS resolves the module from the anchor's address;
        // UNCHANGED_REFCOUNT avoids the extra increment FROM_ADDRESS would
        // otherwise apply (harmless under PIN, but explicit about intent).
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN |
                               GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&pin_anchor), &h);
    });
}

}  // namespace wlx::runtime::host
