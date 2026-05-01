#pragma once

// Export decoration for the wlx-listerine-core DLL.
//
// When building the DLL itself (target wlx-listerine-core), CMake defines
// WLX_CORE_BUILDING; declarations decorated with WLX_CORE_API are exported.
// When consumed (wlx-core, plugins, tests, screenshot_tool), no define is
// set; the same declarations become dllimport.
//
// All cross-DLL public surfaces (classes, free functions) MUST carry this
// macro on their declaration. Member function decoration is implied by the
// class-level decoration on MSVC.

#ifdef WLX_CORE_BUILDING
#  define WLX_CORE_API __declspec(dllexport)
#else
#  define WLX_CORE_API __declspec(dllimport)
#endif
