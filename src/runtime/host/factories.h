#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d2d1.h>
#include <dwrite.h>

namespace wlx::runtime::host {

// Initialize the D2D1 single-threaded factory and the DWrite shared factory
// if they aren't already. Idempotent; call from each plugin's first ListLoadW.
void ensure_factories();

// Accessors for the cached factories. Return nullptr if ensure_factories()
// hasn't yet succeeded.
//
// Important: wlx-core is a static lib statically linked into each plugin
// DLL, so each .wlx64 has its own copy of the factory holders. Pointers
// returned here are not interchangeable across plugin DLLs — but every
// caller within a given plugin sees the same instance.
ID2D1Factory*   d2d_factory();
IDWriteFactory* dwrite_factory();

// Move the factory ComPtr ownership into a never-freed heap allocation and
// null out the file-static holders. Call from DLL_PROCESS_DETACH only —
// COM Release() under the loader lock can deadlock against terminated
// threads (the DWrite shared factory in particular).
void leak_factories_on_detach();

}  // namespace wlx::runtime::host
