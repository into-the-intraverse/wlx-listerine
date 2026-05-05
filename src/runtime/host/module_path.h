#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>

namespace wlx::runtime::host {

// Directory portion of `module`'s on-disk path, including the trailing
// separator. Returns the full path unchanged if it has no separator.
std::wstring get_module_dir(HMODULE module);

}  // namespace wlx::runtime::host
