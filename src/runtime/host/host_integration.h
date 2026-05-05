#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/host/host_view.h"

#include <windows.h>
#include <commctrl.h>

#include <unordered_map>
#include <vector>

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

template <HostView V>
LRESULT CALLBACK HostIntegration<V>::get_msg_hook_(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == PM_REMOVE && self_) {
        MSG* m = reinterpret_cast<MSG*>(lp);
        if (m && m->message == WM_KEYDOWN && m->wParam == VK_F2) {
            auto it = self_->views_.find(m->hwnd);
            if (it != self_->views_.end()) {
                V* vs = it->second;
                if (!vs->file_path.empty()) {
                    reload_view(*vs, vs->file_path.c_str());
                }
                // Eat F2 iff we already know the reload cmd (otherwise let TC
                // dispatch it normally so WM_INITMENUPOPUP discovery still
                // has a chance to learn the ID).
                if (self_->reload_menu_id_ != 0) {
                    m->message = WM_NULL;
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

template <HostView V>
void HostIntegration<V>::attach(V* vs, HWND parent_hint) {
    self_ = this;
    views_[vs->hwnd] = vs;

    if (hook_refcount_++ == 0) {
        msg_hook_ = SetWindowsHookExW(WH_GETMESSAGE, get_msg_hook_,
                                       nullptr, GetCurrentThreadId());
    }

    if (reload_menu_id_ == 0) {
        reload_menu_id_ = find_reload_id_via_accel_resources_();
    }

    HWND target = find_menu_owner_(parent_hint);
    if (!target) target = parent_hint;
    vs->subclass_target = target;
    if (target && parent_refcount_[target]++ == 0) {
        SetWindowSubclass(target, parent_subclass_proc_, kSubclassId, 0);
    }
}

template <HostView V>
void HostIntegration<V>::detach(V* vs) {
    if (vs->subclass_target) {
        auto it = parent_refcount_.find(vs->subclass_target);
        if (it != parent_refcount_.end() && --it->second <= 0) {
            RemoveWindowSubclass(vs->subclass_target, parent_subclass_proc_, kSubclassId);
            parent_refcount_.erase(it);
        }
    }
    views_.erase(vs->hwnd);
    if (hook_refcount_ > 0 && --hook_refcount_ == 0 && msg_hook_) {
        UnhookWindowsHookEx(msg_hook_);
        msg_hook_ = nullptr;
    }
    if (views_.empty()) self_ = nullptr;
}

template <HostView V>
void HostIntegration<V>::emergency_cleanup() {
    for (auto& [parent, _] : parent_refcount_) {
        RemoveWindowSubclass(parent, parent_subclass_proc_, kSubclassId);
    }
    parent_refcount_.clear();
    if (msg_hook_) {
        UnhookWindowsHookEx(msg_hook_);
        msg_hook_ = nullptr;
    }
    hook_refcount_ = 0;
    views_.clear();
    self_ = nullptr;
}

template <HostView V>
LRESULT CALLBACK HostIntegration<V>::parent_subclass_proc_(HWND hwnd, UINT msg,
                                                             WPARAM wp, LPARAM lp,
                                                             UINT_PTR, DWORD_PTR) {
    if (!self_) return DefSubclassProc(hwnd, msg, wp, lp);

    // Lazy text-based discovery: some TC builds label menu items with a tab
    // followed by "F2". Use that if our resource scan didn't yield anything.
    if (msg == WM_INITMENUPOPUP && self_->reload_menu_id_ == 0) {
        if (HMENU menu = reinterpret_cast<HMENU>(wp)) {
            if (UINT id = find_menu_item_by_accel_(menu, L"F2"); id) {
                self_->reload_menu_id_ = id;
            }
        }
    }

    if (msg == WM_COMMAND) {
        const UINT cmd = LOWORD(wp);
        const UINT src = HIWORD(wp);  // 0=menu, 1=accelerator

        // Always eat accelerator-source reload commands. The F2 hook already
        // reloaded every matching view; TC's default handler would be a no-op
        // for plugin windows anyway.
        if (src == 1 && self_->reload_menu_id_ != 0 && cmd == self_->reload_menu_id_) {
            return 0;
        }

        // Menu click on the known reload item: fan out to EVERY view whose
        // subclass_target is this hwnd (H3 fix — was previously return-after-first).
        if (src == 0 && self_->reload_menu_id_ != 0 && cmd == self_->reload_menu_id_) {
            bool any = false;
            for (auto& [pwnd, vs] : self_->views_) {
                if (vs->subclass_target == hwnd && !vs->file_path.empty()) {
                    reload_view(*vs, vs->file_path.c_str());
                    any = true;
                }
            }
            if (any) return 0;
        }
    }

    return DefSubclassProc(hwnd, msg, wp, lp);
}

template <HostView V>
HWND HostIntegration<V>::find_menu_owner_(HWND start) {
    HWND wnd = start;
    while (wnd) {
        if (GetMenu(wnd)) return wnd;
        HWND parent = GetParent(wnd);
        if (!parent || parent == wnd) break;
        wnd = parent;
    }
    return nullptr;
}

template <HostView V>
UINT HostIntegration<V>::find_menu_item_by_accel_(HMENU menu, const wchar_t* accel) {
    if (!menu) return 0;
    const int count = GetMenuItemCount(menu);
    const size_t accel_len = wcslen(accel);
    for (int i = 0; i < count; i++) {
        const UINT id = GetMenuItemID(menu, i);
        if (id == 0 || id == static_cast<UINT>(-1)) continue;
        wchar_t buf[256];
        const int len = GetMenuStringW(menu, i, buf, _countof(buf), MF_BYPOSITION);
        if (len <= 0) continue;
        const wchar_t* tab = wcschr(buf, L'\t');
        if (!tab) continue;
        if (wcsncmp(tab + 1, accel, accel_len) != 0) continue;
        const wchar_t after = tab[1 + accel_len];
        if (after == 0 || iswspace(after)) return id;
    }
    return 0;
}

// TC builds accelerator tables at runtime from .lng/.ini; this RT_ACCELERATOR
// scan returns 0 on shipping TC builds. Kept as a cheap try-first in case a
// future build or alternative file manager ships static accel resources.
template <HostView V>
UINT HostIntegration<V>::find_reload_id_via_accel_resources_() {
    HMODULE main = GetModuleHandleW(nullptr);
    if (!main) return 0;
    UINT found = 0;
    // Use the explicit wide form instead of RT_ACCELERATOR: the macro resolves
    // to LPSTR vs. LPWSTR based on the TU's UNICODE state, which we can't
    // assume from a header. MAKEINTRESOURCEW(9) is the wide-string equivalent.
    EnumResourceNamesW(main, MAKEINTRESOURCEW(9),
        [](HMODULE mod, LPCWSTR, LPWSTR name, LONG_PTR param) -> BOOL {
            auto* out = reinterpret_cast<UINT*>(param);
            HACCEL h = LoadAcceleratorsW(mod, name);
            if (!h) return TRUE;
            const int n = CopyAcceleratorTable(h, nullptr, 0);
            if (n <= 0) return TRUE;
            std::vector<ACCEL> accels(static_cast<size_t>(n));
            CopyAcceleratorTable(h, accels.data(), n);
            for (const auto& a : accels) {
                if (a.key == VK_F2
                    && (a.fVirt & FVIRTKEY)
                    && !(a.fVirt & (FALT | FCONTROL | FSHIFT))) {
                    *out = a.cmd;
                    return FALSE;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LONG_PTR>(&found));
    return found;
}
