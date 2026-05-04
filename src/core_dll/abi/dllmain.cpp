#define NOMINMAX
#include <windows.h>

// Captured at DLL_PROCESS_ATTACH so CoreRegistry can derive its install dir
// via GetModuleFileNameW. Plain assignment is safe — Windows guarantees
// DllMain runs serialized.
HMODULE g_core_module = nullptr;

extern "C" BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_core_module = static_cast<HMODULE>(hInst);
        DisableThreadLibraryCalls(hInst);
    }
    // DLL_PROCESS_DETACH: do nothing. Singleton is intentionally leaked
    // (see specs/2026-05-01-lazy-grammar-loading-design.md §3.4).
    return TRUE;
}
