#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>

#include <concepts>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

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

template <HostView V>
class HostIntegration {
public:
    void attach(V* vs, HWND parent_hint);
    void detach(V* vs);
    void emergency_cleanup();

    // Test / diagnostic accessors.
    UINT reload_menu_id() const { return reload_menu_id_; }
    void set_reload_menu_id_for_test(UINT id) { reload_menu_id_ = id; }
    const std::unordered_map<HWND, V*>& views_for_test() const { return views_; }

    // Invoked by the subclass proc; also exposed publicly for unit tests that
    // want to synthesize menu commands without a real message pump.
    static LRESULT CALLBACK parent_subclass_proc_(HWND hwnd, UINT msg, WPARAM wp,
                                                   LPARAM lp, UINT_PTR, DWORD_PTR);

private:
    static LRESULT CALLBACK get_msg_hook_(int code, WPARAM wp, LPARAM lp);
    static HWND find_menu_owner_(HWND start);
    static UINT find_reload_id_via_accel_resources_();
    static UINT find_menu_item_by_accel_(HMENU menu, const wchar_t* accel);

    static constexpr UINT_PTR kSubclassId = 0x574C5850;  // 'WLXP'

    inline static HostIntegration* self_ = nullptr;

    std::unordered_map<HWND, V*> views_;
    std::unordered_map<HWND, int> parent_refcount_;
    HHOOK msg_hook_ = nullptr;
    int hook_refcount_ = 0;
    UINT reload_menu_id_ = 0;
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
