#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <concepts>
#include <string>
#include <type_traits>

// Concept: the state that HostIntegration needs to drive the F2 reload and
// the File->Reload menu subclass. Plus an ADL `reload_view(V&, const wchar_t*)`
// free function in the plugin's translation unit.
template <typename V>
concept HostView = requires(V& v, const wchar_t* p) {
    { v.hwnd }            -> std::convertible_to<HWND>;
    { v.file_path }       -> std::same_as<std::wstring&>;
    { v.subclass_target } -> std::same_as<HWND&>;
    reload_view(v, p);
};

namespace wlx_host_common_internal_ {
struct ConceptProbe {
    HWND hwnd = nullptr;
    std::wstring file_path;
    HWND subclass_target = nullptr;
};
inline void reload_view(ConceptProbe&, const wchar_t*) {}
static_assert(HostView<ConceptProbe>);
}
