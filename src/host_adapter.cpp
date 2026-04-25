#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <vector>

#include "listerplugin.h"
#include "file_service.h"
#include "markdown_parser.h"
#include "layout_engine.h"
#include "render_engine.h"
#include "interaction_engine.h"
#include "theme_service.h"
#include "cache_service.h"
#include "colorizer.h"
#include "search_engine.h"
#include "search_ops.h"

#define WLX_TRACE_TAG L"wlx-md"
#include "wlx_trace.h"

using Microsoft::WRL::ComPtr;

// ---------- per-window state ----------

struct ViewState {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    HWND subclass_target = nullptr;  // window we subclassed (menu owner)
    bool dark_mode = false;
    bool wrap_text = false;
    std::wstring file_path;

    std::shared_ptr<Document> document;
    std::shared_ptr<LayoutDocument> layout;
    std::unique_ptr<RenderEngine> renderer;
    std::unique_ptr<InteractionEngine> interaction;

    float scroll_y = 0;
    float max_scroll_y = 0;
    int hovered_span = -1;

    // Selection
    TextPosition sel_anchor;
    TextPosition sel_active;
    bool selecting = false;
    int hovered_code_block = -1;

    // Triple-click detection
    DWORD last_dblclk_time = 0;
    int last_dblclk_block = -1;
    int copied_code_block = -1;

    SearchIndex search_index;
    std::vector<SearchMatch> matches;
    int current_match = -1;
    SearchQuery last_query;
    bool index_dirty = true;
};

static_assert(SearchState<ViewState>);

// ---------- globals ----------

static HMODULE g_hModule = nullptr;
static ThemeService g_theme;
static bool g_theme_loaded = false;
static ComPtr<ID2D1Factory> g_d2d_factory;
static ComPtr<IDWriteFactory> g_dwrite_factory;
static CacheService g_cache;
static FileService g_file_service;
static std::unordered_map<HWND, ViewState*> g_views;
static ATOM g_window_class = 0;
static std::string g_default_ini_path;
static std::unique_ptr<Colorizer> g_colorizer;

// WH_GETMESSAGE hook: TC's message pump runs TranslateAccelerator before
// DispatchMessage, which consumes F2 (Lister's "Reload File" accelerator)
// and posts WM_COMMAND to the parent — a no-op for plugin windows, so F2
// effectively vanishes before our WndProc sees it. Hooking on the UI thread
// lets us inspect and rewrite the message *before* TranslateAccelerator runs.
// Refcounted so multiple plugin windows share one hook installation.
static HHOOK g_msg_hook = nullptr;
static int g_hook_refcount = 0;

// Parent (Lister) subclass state — used to intercept the File→Reload menu
// click. WM_INITMENUPOPUP lets us discover the reload menu ID lazily by
// scanning the popup for an item whose accelerator suffix is "F2"; the
// resulting WM_COMMAND then gets converted into our load_document call.
// Refcounted per parent HWND so multiple plugin windows under the same
// Lister parent share one subclass installation.
static constexpr UINT_PTR PARENT_SUBCLASS_ID = 0x574C5850;  // 'WLXP'
static UINT g_reload_menu_id = 0;
static bool g_pending_f2_capture = false;  // F2 just leaked through; next accel WM_COMMAND is its
// Menu-then-F2 discovery: when an unknown menu cmd arrives we hold it as a
// candidate. If F2 fires within the TTL, the user just did "click menu →
// nothing → press F2", so the candidate IS the reload command. This avoids
// guessing wrong by trusting an isolated menu click (which could be any item).
static UINT g_candidate_reload_id = 0;
static DWORD g_candidate_time = 0;
static constexpr DWORD CANDIDATE_TTL_MS = 5000;
static std::unordered_map<HWND, int> g_parent_refcount;

// ---------- helpers ----------

// DWMWA_USE_IMMERSIVE_DARK_MODE (value 20) — available since Win10 20H1.
// Older SDKs may not define it.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static void apply_dark_mode(HWND hwnd, bool dark) {
    BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
    // SetWindowTheme themes the scrollbar (DwmSetWindowAttribute only affects title bar)
    SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

static std::wstring get_module_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hModule, path, MAX_PATH);
    std::wstring dir(path);
    auto pos = dir.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? dir.substr(0, pos + 1) : dir;
}

static void ensure_factories() {
    if (!g_d2d_factory) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_d2d_factory.GetAddressOf());
    }
    if (!g_dwrite_factory) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(g_dwrite_factory.GetAddressOf()));
    }
}

static void ensure_theme() {
    if (!g_theme_loaded) {
        std::wstring cfg_path = get_module_dir() + L"wlx-listerine-md.toml";
        g_theme.load(cfg_path);
        g_theme_loaded = true;

        // Initialize colorizer
        std::wstring base = get_module_dir();
        std::wstring grammar_dir = base + g_theme.config().code_grammar_dir;
        std::wstring theme_dir = base + g_theme.config().code_theme_dir;
        g_colorizer = std::make_unique<Colorizer>(
            grammar_dir, theme_dir,
            g_theme.config().code_theme,
            g_theme.config().code_theme_light);
    }
}

static void update_scrollbar(ViewState* vs) {
    if (!vs->layout || !vs->hwnd) return;

    // Use DIP dimensions — layout and D2D coordinates are in DIPs, not physical pixels
    float viewport_h = vs->renderer ? vs->renderer->dip_height() : 1.0f;
    vs->max_scroll_y = std::max(0.0f, vs->layout->total_height - viewport_h);
    vs->scroll_y = std::clamp(vs->scroll_y, 0.0f, vs->max_scroll_y);

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = static_cast<int>(vs->layout->total_height);
    si.nPage = static_cast<UINT>(viewport_h);
    si.nPos = static_cast<int>(vs->scroll_y);
    SetScrollInfo(vs->hwnd, SB_VERT, &si, TRUE);
}

static void do_layout(ViewState* vs) {
    if (!vs->document || !g_dwrite_factory) return;

    // Use DIP width — IDWriteTextLayout measures in DIPs, not physical pixels
    float viewport_width = vs->renderer ? vs->renderer->dip_width() : 1.0f;

    // Check layout cache
    LayoutCacheKey lk;
    lk.viewport_width_bucket = CacheService::bucket_width(static_cast<int>(viewport_width));
    lk.theme_hash = g_theme.theme_hash();

    LayoutEngine engine(g_dwrite_factory.Get(), g_theme, vs->dark_mode, g_colorizer.get());
    auto layout = std::make_shared<LayoutDocument>(engine.layout(*vs->document, viewport_width, vs->wrap_text));

    vs->layout = layout;
    vs->interaction = std::make_unique<InteractionEngine>(*vs->layout);

    update_scrollbar(vs);
    vs->index_dirty = true;
}

static void load_document(ViewState* vs, const wchar_t* path) {
    vs->file_path = path;
    vs->scroll_y = 0;
    vs->sel_anchor = TextPosition{};
    vs->sel_active = TextPosition{};
    vs->selecting = false;
    vs->hovered_code_block = -1;
    vs->copied_code_block = -1;
    vs->matches.clear();
    vs->current_match = -1;
    vs->last_query = SearchQuery{};
    vs->index_dirty = true;
    // Also clear matches in the renderer: its search_matches_ still holds
    // SearchMatch objects whose block_index values point into the previous
    // file's layout. Any repaint before the next F7 would walk them against
    // the new layout and could draw spurious highlight rects.
    if (vs->renderer) vs->renderer->set_search_matches({}, -1);

    auto content = g_file_service.read(path);
    if (!content) return;

    // Check parse cache
    ParseCacheKey pk;
    pk.path = content->identity.path;
    pk.size = content->identity.size;
    pk.mtime = content->identity.mtime;
    pk.parser_version = MarkdownParser::parser_version();

    auto cached_doc = g_cache.lookup_parse(pk);
    if (cached_doc) {
        vs->document = cached_doc;
    } else {
        MarkdownParser parser;
        auto doc = std::make_shared<Document>(
            parser.parse(content->raw_utf8.c_str(), content->raw_utf8.size()));
        g_cache.store_parse(pk, doc);
        vs->document = doc;
    }

    do_layout(vs);
}

// ---------- F2 reload hook ----------
//
// Sits on the UI thread's message queue *ahead of* TranslateAccelerator so we
// can intercept VK_F2 before TC's accelerator table converts it into the
// silent-no-op WM_COMMAND.  Only acts on F2 messages targeting one of OUR
// plugin windows (looked up in g_views) — leaves every other key untouched.
static LRESULT CALLBACK GetMsgHookProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == PM_REMOVE) {
        MSG* m = reinterpret_cast<MSG*>(lp);
        if (m && m->message == WM_KEYDOWN && m->wParam == VK_F2) {
            auto it = g_views.find(m->hwnd);
            if (it != g_views.end()) {
                ViewState* vs = it->second;
                WLX_TRACE(L"F2 reload via hook hwnd=%p file=%s",
                          m->hwnd,
                          vs->file_path.empty() ? L"(empty)" : vs->file_path.c_str());
                if (!vs->file_path.empty()) {
                    load_document(vs, vs->file_path.c_str());
                    InvalidateRect(vs->hwnd, nullptr, FALSE);
                }
                // Menu-then-F2 discovery: if a recent menu click left a
                // candidate cmd, the user just did "click menu → no effect
                // → press F2". That click was the reload menu — confirm.
                if (g_reload_menu_id == 0
                    && g_candidate_reload_id != 0
                    && (GetTickCount() - g_candidate_time) < CANDIDATE_TTL_MS) {
                    g_reload_menu_id = g_candidate_reload_id;
                    WLX_TRACE(L"confirmed reload menu id=%u via menu→F2 sequence",
                              g_reload_menu_id);
                }
                g_candidate_reload_id = 0;
                if (g_reload_menu_id == 0) {
                    // Fallback discovery path (in case TC ever uses
                    // TranslateAccelerator): let it run so the resulting
                    // WM_COMMAND can be captured by our parent subclass.
                    g_pending_f2_capture = true;
                } else {
                    // ID known — eat to keep TC's no-op handler from firing.
                    m->message = WM_NULL;
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static void install_msg_hook() {
    if (g_hook_refcount++ == 0) {
        g_msg_hook = SetWindowsHookExW(WH_GETMESSAGE, GetMsgHookProc,
                                        nullptr, GetCurrentThreadId());
        WLX_TRACE(L"WH_GETMESSAGE hook installed handle=%p", g_msg_hook);
    }
}

static void uninstall_msg_hook() {
    if (g_hook_refcount > 0 && --g_hook_refcount == 0 && g_msg_hook) {
        UnhookWindowsHookEx(g_msg_hook);
        WLX_TRACE(L"WH_GETMESSAGE hook uninstalled");
        g_msg_hook = nullptr;
    }
}

// ---------- File→Reload menu interception ----------

// Scan every accelerator-table resource in TC's main executable looking for
// a binding of `vk` (no Ctrl/Alt/Shift modifier). This is the most reliable
// way to discover TC's reload command ID because TC's menu items are
// owner-drawn (no text accessible via GetMenuStringW) and TC processes F2
// itself before TranslateAccelerator ever runs (so we can't observe a
// WM_COMMAND from a real F2 keystroke either).
static BOOL CALLBACK accel_enum_proc_(HMODULE mod, LPCWSTR /*type*/, LPWSTR name, LONG_PTR param) {
    auto* found = reinterpret_cast<UINT*>(param);
    HACCEL h = LoadAcceleratorsW(mod, name);
    if (!h) return TRUE;
    int n = CopyAcceleratorTable(h, nullptr, 0);
    if (n <= 0) return TRUE;
    std::vector<ACCEL> accels(static_cast<size_t>(n));
    CopyAcceleratorTable(h, accels.data(), n);
    for (auto& a : accels) {
        // Modifier-less F2: FVIRTKEY set, no FALT/FCONTROL/FSHIFT.
        if (a.key == VK_F2
            && (a.fVirt & FVIRTKEY)
            && !(a.fVirt & (FALT | FCONTROL | FSHIFT))) {
            *found = a.cmd;
            return FALSE;  // stop enumeration
        }
    }
    return TRUE;
}

static UINT find_reload_id_via_accel_resources() {
    HMODULE main = GetModuleHandleW(nullptr);
    if (!main) return 0;
    UINT found = 0;
    EnumResourceNamesW(main, RT_ACCELERATOR, accel_enum_proc_,
                       reinterpret_cast<LONG_PTR>(&found));
    return found;
}

// Look for a menu item whose accelerator hint after the tab is exactly the
// given string (e.g. L"F2"). Returns 0 if none. We require the tail to be
// the accel literally (with optional trailing whitespace) so "F20" doesn't
// false-match for L"F2".
static UINT find_menu_item_by_accel(HMENU menu, const wchar_t* accel) {
    if (!menu) return 0;
    int count = GetMenuItemCount(menu);
    size_t accel_len = wcslen(accel);
    for (int i = 0; i < count; i++) {
        UINT id = GetMenuItemID(menu, i);
        if (id == 0 || id == static_cast<UINT>(-1)) continue;  // separator/submenu
        wchar_t buf[256];
        int len = GetMenuStringW(menu, i, buf, _countof(buf), MF_BYPOSITION);
        if (len <= 0) continue;
        const wchar_t* tab = wcschr(buf, L'\t');
        if (!tab) continue;
        if (wcsncmp(tab + 1, accel, accel_len) != 0) continue;
        wchar_t after = tab[1 + accel_len];
        if (after == 0 || iswspace(after)) return id;
    }
    return 0;
}

static LRESULT CALLBACK ParentSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR /*subclass_id*/, DWORD_PTR /*ref*/) {
    // Backup discovery path: some menus include the accel hint in their text.
    if (msg == WM_INITMENUPOPUP && g_reload_menu_id == 0) {
        HMENU menu = reinterpret_cast<HMENU>(wp);
        UINT id = find_menu_item_by_accel(menu, L"F2");
        if (id) {
            g_reload_menu_id = id;
            WLX_TRACE(L"discovered reload menu id=%u via WM_INITMENUPOPUP parent=%p",
                      id, hwnd);
        }
        // Diagnostic dump: log every item in the popup so we can see TC's menu
        // structure and find the reload entry by text if accel hints are absent.
        if (menu) {
            int count = GetMenuItemCount(menu);
            WLX_TRACE(L"  WM_INITMENUPOPUP menu=%p items=%d", menu, count);
            for (int i = 0; i < count; i++) {
                wchar_t buf[256] = {};
                GetMenuStringW(menu, i, buf, _countof(buf), MF_BYPOSITION);
                UINT iid = GetMenuItemID(menu, i);
                WLX_TRACE(L"    [%d] id=%u text=\"%s\"", i, iid, buf);
            }
        }
    }
    if (msg == WM_COMMAND) {
        UINT cmd = LOWORD(wp);
        UINT src = HIWORD(wp);  // 0=menu, 1=accelerator
        WLX_TRACE(L"  WM_COMMAND cmd=%u src=%u (%s)", cmd, src,
                  src == 0 ? L"menu" : (src == 1 ? L"accel" : L"control"));

        // Primary discovery path: catch the WM_COMMAND that our F2 hook
        // deliberately let through. Whatever LOWORD(wp) is, that's the
        // reload menu ID — TC's accelerator table told us so.
        if (src == 1 && g_pending_f2_capture) {
            if (g_reload_menu_id == 0) {
                g_reload_menu_id = cmd;
                WLX_TRACE(L"captured reload menu id=%u from F2 accelerator probe", cmd);
            }
            g_pending_f2_capture = false;
            return 0;  // eat — TC's reload handler is a no-op for plugin windows
        }

        // Eat F2-derived WM_COMMAND in steady state too (hook already reloaded).
        if (src == 1 && g_reload_menu_id != 0 && cmd == g_reload_menu_id) {
            return 0;
        }

        // Menu click on the reload item — hook didn't fire, do the reload here.
        if (src == 0 && g_reload_menu_id != 0 && cmd == g_reload_menu_id) {
            for (auto& [pwnd, vs] : g_views) {
                if (vs->subclass_target == hwnd) {
                    WLX_TRACE(L"File→Reload via subclass owner=%p plugin=%p file=%s",
                              hwnd, pwnd,
                              vs->file_path.empty() ? L"(empty)" : vs->file_path.c_str());
                    if (!vs->file_path.empty()) {
                        load_document(vs, vs->file_path.c_str());
                        InvalidateRect(vs->hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
            }
        }

        // Unknown menu-source command — discovery logic with two confirmation
        // paths:
        //   1) menu→F2: F2 hook checks for a recent candidate and confirms it
        //   2) menu→menu: same cmd seen twice within TTL → user is clicking
        //      Reload, expecting it to work; confirm and act on this click
        if (src == 0 && g_reload_menu_id == 0 && cmd != 0) {
            DWORD now = GetTickCount();
            if (g_candidate_reload_id == cmd
                && (now - g_candidate_time) < CANDIDATE_TTL_MS) {
                // Repeat-click confirmation
                g_reload_menu_id = cmd;
                WLX_TRACE(L"confirmed reload menu id=%u via repeated menu click", cmd);
                g_candidate_reload_id = 0;
                for (auto& [pwnd, vs] : g_views) {
                    if (vs->subclass_target == hwnd) {
                        if (!vs->file_path.empty()) {
                            load_document(vs, vs->file_path.c_str());
                            InvalidateRect(vs->hwnd, nullptr, FALSE);
                        }
                        return 0;
                    }
                }
                return 0;
            }
            g_candidate_reload_id = cmd;
            g_candidate_time = now;
            WLX_TRACE(L"recorded candidate reload menu id=%u "
                      L"(confirm via F2 or repeat-click)", cmd);
        }

        // Any other WM_COMMAND clears a stale pending flag (e.g. user pressed
        // F2 then immediately clicked something else).
        g_pending_f2_capture = false;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// Walk up the parent chain looking for the window that actually owns a menu.
// Lister's structure varies across TC versions: ParentWin may be a viewport
// child while the menu lives on the top-level frame. WM_COMMAND from menu
// clicks goes to the menu owner, not the immediate parent — so we must
// subclass the owner. Returns nullptr if nothing in the chain has a menu.
static HWND find_menu_owner(HWND start) {
    HWND wnd = start;
    int depth = 0;
    while (wnd) {
        HMENU m = GetMenu(wnd);
        WLX_TRACE(L"  menu-owner walk depth=%d hwnd=%p menu=%p", depth, wnd, m);
        if (m) return wnd;
        HWND parent = GetParent(wnd);
        if (!parent || parent == wnd) break;
        wnd = parent;
        ++depth;
    }
    return nullptr;
}

// Returns the hwnd actually subclassed (may differ from `parent_hint` if the
// menu owner sits higher in the chain). Returns nullptr if subclassing is
// inapplicable (no menu found anywhere). Caller must remember the returned
// hwnd and pass it to uninstall_parent_subclass on close.
static HWND install_parent_subclass(HWND parent_hint) {
    if (!parent_hint) return nullptr;
    HWND target = find_menu_owner(parent_hint);
    if (!target) target = parent_hint;  // still subclass — WM_INITMENUPOPUP may yet appear
    if (g_parent_refcount[target]++ == 0) {
        SetWindowSubclass(target, ParentSubclassProc, PARENT_SUBCLASS_ID, 0);
        WLX_TRACE(L"parent subclass installed target=%p (hint=%p)", target, parent_hint);
    }
    return target;
}

static void uninstall_parent_subclass(HWND target) {
    if (!target) return;
    auto it = g_parent_refcount.find(target);
    if (it == g_parent_refcount.end()) return;
    if (--it->second <= 0) {
        RemoveWindowSubclass(target, ParentSubclassProc, PARENT_SUBCLASS_ID);
        WLX_TRACE(L"parent subclass removed target=%p", target);
        g_parent_refcount.erase(it);
    }
}

// ---------- WndProc ----------

static void handle_scroll(ViewState* vs, float delta) {
    if (!vs->layout) return;

    float old_y = vs->scroll_y;
    vs->scroll_y = std::clamp(vs->scroll_y + delta, 0.0f, vs->max_scroll_y);

    if (vs->scroll_y != old_y) {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_POS;
        si.nPos = static_cast<int>(vs->scroll_y);
        SetScrollInfo(vs->hwnd, SB_VERT, &si, TRUE);
        InvalidateRect(vs->hwnd, nullptr, FALSE);
    }
}

static constexpr UINT_PTR TIMER_AUTOSCROLL = 1;
static constexpr UINT_PTR TIMER_COPY_FEEDBACK = 2;

static TextPosition hit_test_position(const LayoutDocument& layout, float x, float y) {
    int block_count = static_cast<int>(layout.blocks.size());

    for (int i = 0; i < block_count; i++) {
        auto& block = layout.blocks[i];
        if (block.text_runs.empty()) continue;
        if (y < block.rect.top || y > block.rect.bottom) continue;
        // For table cells (multiple blocks share the same row), also check x bounds
        if (x < block.rect.left || x > block.rect.right) continue;

        auto& run = block.text_runs[0];
        if (!run.layout) continue;

        float local_x = x - run.rect.left;
        float local_y = y - run.rect.top;
        BOOL is_trailing = FALSE;
        BOOL is_inside = FALSE;
        DWRITE_HIT_TEST_METRICS htm = {};
        run.layout->HitTestPoint(local_x, local_y, &is_trailing, &is_inside, &htm);

        int offset = static_cast<int>(htm.textPosition);
        if (is_trailing) offset++;
        return TextPosition{i, offset};
    }

    // Snap to nearest block boundary
    int closest = -1;
    float closest_dist = 1e9f;
    for (int i = 0; i < block_count; i++) {
        auto& block = layout.blocks[i];
        if (block.text_runs.empty()) continue;
        float mid = (block.rect.top + block.rect.bottom) * 0.5f;
        float dist = std::abs(y - mid);
        if (dist < closest_dist) {
            closest_dist = dist;
            closest = i;
        }
    }

    if (closest >= 0) {
        auto& block = layout.blocks[closest];
        if (y < (block.rect.top + block.rect.bottom) * 0.5f) {
            return TextPosition{closest, 0};
        } else {
            int len = 0;
            for (auto& run : block.text_runs) len += static_cast<int>(run.text.size());
            return TextPosition{closest, len};
        }
    }
    return TextPosition{};
}

static int block_text_length(const LayoutBlock& block) {
    int len = 0;
    for (auto& run : block.text_runs) len += static_cast<int>(run.text.size());
    return len;
}

static bool copy_to_clipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty()) return false;
    if (!OpenClipboard(hwnd)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hg) {
        void* p = GlobalLock(hg);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(hg);
            SetClipboardData(CF_UNICODETEXT, hg);
        }
    }
    CloseClipboard();
    return true;
}

static bool is_in_copy_button(const LayoutBlock& block, float x, float y) {
    if (block.type != BlockType::CodeFence) return false;
    float btn_size = 24.0f;
    float pad = 6.0f;
    float bx = block.rect.right - btn_size - pad;
    float by = block.rect.top + pad;
    return x >= bx && x <= bx + btn_size && y >= by && y <= by + btn_size;
}

static void clear_selection(ViewState* vs) {
    vs->sel_anchor = TextPosition{};
    vs->sel_active = TextPosition{};
    vs->selecting = false;
}

static LRESULT CALLBACK ViewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* vs = reinterpret_cast<ViewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (vs && vs->renderer && vs->layout) {
            if (vs->renderer->needs_recreate())
                vs->renderer->create_device_resources(hwnd);
            vs->renderer->set_hovered_code_block(vs->hovered_code_block);
            vs->renderer->set_copied_code_block(vs->copied_code_block);
            auto sel_lo = std::min(vs->sel_anchor, vs->sel_active);
            auto sel_hi = std::max(vs->sel_anchor, vs->sel_active);
            vs->renderer->paint(*vs->layout, vs->scroll_y, sel_lo, sel_hi);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        UINT w = LOWORD(lp);
        UINT h = HIWORD(lp);
        if (vs && vs->renderer && w > 0 && h > 0) {
            vs->renderer->resize(w, h);
            do_layout(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_VSCROLL: {
        if (!vs) break;
        float page = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        float line = g_theme.fonts().body_size * g_theme.spacing().line_height_factor;

        switch (LOWORD(wp)) {
        case SB_LINEUP:    handle_scroll(vs, -line); break;
        case SB_LINEDOWN:  handle_scroll(vs, line); break;
        case SB_PAGEUP:    handle_scroll(vs, -page); break;
        case SB_PAGEDOWN:  handle_scroll(vs, page); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            vs->scroll_y = static_cast<float>(si.nTrackPos);
            vs->scroll_y = std::clamp(vs->scroll_y, 0.0f, vs->max_scroll_y);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case SB_TOP:    vs->scroll_y = 0; InvalidateRect(hwnd, nullptr, FALSE); break;
        case SB_BOTTOM: vs->scroll_y = vs->max_scroll_y; InvalidateRect(hwnd, nullptr, FALSE); break;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (!vs) break;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        float line = g_theme.fonts().body_size * g_theme.spacing().line_height_factor;
        handle_scroll(vs, -static_cast<float>(delta) / 120.0f * line * 3.0f);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!vs) break;
        float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                : static_cast<float>(GET_X_LPARAM(lp));
        float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                : static_cast<float>(GET_Y_LPARAM(lp));
        float doc_x = px;
        float doc_y = py + vs->scroll_y;

        if (vs->selecting && vs->layout) {
            auto pos = hit_test_position(*vs->layout, doc_x, doc_y);
            if (pos.valid() && pos != vs->sel_active) {
                vs->sel_active = pos;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            float viewport_h = vs->renderer ? vs->renderer->dip_height() : 100.0f;
            if (py < 0 || py > viewport_h)
                SetTimer(hwnd, TIMER_AUTOSCROLL, 50, nullptr);
            else
                KillTimer(hwnd, TIMER_AUTOSCROLL);
        } else if (vs->layout) {
            // Link hover
            if (vs->interaction) {
                auto hit = vs->interaction->hit_test(doc_x, doc_y);
                int new_hover = hit.hit ? hit.span_index : -1;
                if (new_hover != vs->hovered_span) {
                    vs->hovered_span = new_hover;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            // Code block copy button hover
            int new_code_hover = -1;
            for (int i = 0; i < static_cast<int>(vs->layout->blocks.size()); i++) {
                if (is_in_copy_button(vs->layout->blocks[i], doc_x, doc_y)) {
                    new_code_hover = i;
                    break;
                }
            }
            if (new_code_hover != vs->hovered_code_block) {
                vs->hovered_code_block = new_code_hover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            // Cursor
            if (vs->hovered_span >= 0)
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
            else if (new_code_hover >= 0)
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            else {
                bool over_text = false;
                for (auto& block : vs->layout->blocks) {
                    if (block.text_runs.empty()) continue;
                    if (doc_y >= block.rect.top && doc_y <= block.rect.bottom &&
                        doc_x >= block.rect.left && doc_x <= block.rect.right) {
                        over_text = true;
                        break;
                    }
                }
                SetCursor(LoadCursorW(nullptr, over_text ? IDC_IBEAM : IDC_ARROW));
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (!vs || !vs->layout) break;
        SetFocus(hwnd);
        float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                : static_cast<float>(GET_X_LPARAM(lp));
        float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                : static_cast<float>(GET_Y_LPARAM(lp));
        float doc_y = py + vs->scroll_y;

        auto pos = hit_test_position(*vs->layout, px, doc_y);

        // Triple-click: select entire block (line/paragraph)
        if (pos.valid() && vs->last_dblclk_block >= 0 &&
            pos.block_index == vs->last_dblclk_block &&
            (GetTickCount() - vs->last_dblclk_time) < GetDoubleClickTime()) {
            int len = block_text_length(vs->layout->blocks[pos.block_index]);
            vs->sel_anchor = TextPosition{pos.block_index, 0};
            vs->sel_active = TextPosition{pos.block_index, len};
            vs->selecting = false;
            vs->last_dblclk_block = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (pos.valid()) {
            vs->sel_anchor = pos;
            vs->sel_active = pos;
            vs->selecting = true;
            SetCapture(hwnd);
        } else {
            clear_selection(vs);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (!vs || !vs->layout) break;
        bool was_dragging = vs->selecting;
        vs->selecting = false;
        ReleaseCapture();
        KillTimer(hwnd, TIMER_AUTOSCROLL);

        float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                : static_cast<float>(GET_X_LPARAM(lp));
        float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                : static_cast<float>(GET_Y_LPARAM(lp));
        float doc_x = px;
        float doc_y = py + vs->scroll_y;

        // Code block copy button takes priority
        for (int i = 0; i < static_cast<int>(vs->layout->blocks.size()); i++) {
            auto& block = vs->layout->blocks[i];
            if (is_in_copy_button(block, doc_x, doc_y)) {
                std::wstring code_text;
                for (auto& run : block.text_runs) code_text += run.text;
                copy_to_clipboard(hwnd, code_text);
                vs->copied_code_block = i;
                SetTimer(hwnd, TIMER_COPY_FEEDBACK, 1000, nullptr);
                clear_selection(vs);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        // Only update active position if we were dragging (not after double-click word select)
        if (was_dragging) {
            auto pos = hit_test_position(*vs->layout, doc_x, doc_y);
            if (pos.valid()) vs->sel_active = pos;
        }

        if (vs->sel_anchor == vs->sel_active) {
            // No drag — handle links
            clear_selection(vs);
            if (vs->interaction) {
                auto hit = vs->interaction->hit_test(doc_x, doc_y);
                if (hit.hit) {
                    auto action = vs->interaction->resolve(hit.target);
                    switch (action.action) {
                    case InteractionEngine::Action::ScrollToAnchor:
                        vs->scroll_y = std::clamp(action.scroll_y, 0.0f, vs->max_scroll_y);
                        update_scrollbar(vs);
                        break;
                    case InteractionEngine::Action::OpenExternal:
                        ShellExecuteW(nullptr, L"open", action.target.c_str(),
                                      nullptr, nullptr, SW_SHOW);
                        break;
                    case InteractionEngine::Action::ReloadDocument:
                        if (!vs->file_path.empty()) {
                            std::wstring dir = vs->file_path;
                            auto fpos = dir.find_last_of(L"\\/");
                            if (fpos != std::wstring::npos) dir = dir.substr(0, fpos + 1);
                            load_document(vs, (dir + action.target).c_str());
                        }
                        break;
                    default: break;
                    }
                }
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        if (!vs || !vs->layout) break;
        float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                : static_cast<float>(GET_X_LPARAM(lp));
        float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                : static_cast<float>(GET_Y_LPARAM(lp));
        float doc_y = py + vs->scroll_y;

        auto pos = hit_test_position(*vs->layout, px, doc_y);
        if (pos.valid()) {
            auto& block = vs->layout->blocks[pos.block_index];
            std::wstring full;
            for (auto& run : block.text_runs) full += run.text;
            auto [ws, we] = find_word_boundaries(full, pos.char_offset);
            vs->sel_anchor = TextPosition{pos.block_index, ws};
            vs->sel_active = TextPosition{pos.block_index, we};
            vs->selecting = false;

            // Record for triple-click detection
            vs->last_dblclk_time = GetTickCount();
            vs->last_dblclk_block = pos.block_index;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_TIMER: {
        if (wp == TIMER_AUTOSCROLL && vs && vs->selecting) {
            float line = g_theme.fonts().body_size * g_theme.spacing().line_height_factor;
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            float client_y = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(pt.y))
                                          : static_cast<float>(pt.y);
            handle_scroll(vs, client_y < 0 ? -line : line);

            if (vs->layout) {
                float doc_x = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(pt.x))
                                           : static_cast<float>(pt.x);
                float doc_y = client_y + vs->scroll_y;
                auto pos = hit_test_position(*vs->layout, doc_x, doc_y);
                if (pos.valid()) vs->sel_active = pos;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wp == TIMER_COPY_FEEDBACK && vs) {
            KillTimer(hwnd, TIMER_COPY_FEEDBACK);
            vs->copied_code_block = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        WLX_TRACE(L"WndProc WM_KEYDOWN hwnd=%p vk=0x%X", hwnd, (unsigned)wp);
        if (!vs) break;
        float page = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        float line = g_theme.fonts().body_size * g_theme.spacing().line_height_factor;

        bool handled = false;

        // Ctrl+C — copy selection
        if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (vs->layout && vs->sel_anchor.valid() && vs->sel_anchor != vs->sel_active) {
                auto lo = std::min(vs->sel_anchor, vs->sel_active);
                auto hi = std::max(vs->sel_anchor, vs->sel_active);
                auto text = extract_selected_text(*vs->layout, lo, hi);
                copy_to_clipboard(hwnd, text);
            }
            handled = true;
        }
        // Ctrl+A — select all
        else if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (vs->layout && !vs->layout->blocks.empty()) {
                vs->sel_anchor = TextPosition{0, 0};
                int last = static_cast<int>(vs->layout->blocks.size()) - 1;
                vs->sel_active = TextPosition{last, block_text_length(vs->layout->blocks[last])};
                vs->selecting = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            handled = true;
        }
        // Escape — clear selection; if no selection but active matches, clear them;
        // otherwise forward to parent (Lister uses Esc to close the window).
        else if (wp == VK_ESCAPE) {
            if (vs->sel_anchor.valid()) {
                clear_selection(vs);
                InvalidateRect(hwnd, nullptr, FALSE);
                handled = true;
            } else if (!vs->matches.empty()) {
                vs->matches.clear();
                vs->current_match = -1;
                if (vs->renderer) vs->renderer->set_search_matches({}, -1);
                InvalidateRect(hwnd, nullptr, FALSE);
                handled = true;
            }
            // else fall through -> forwarded to parent
        }
        else {
            switch (wp) {
            case VK_UP:    handle_scroll(vs, -line); handled = true; break;
            case VK_DOWN:  handle_scroll(vs,  line); handled = true; break;
            case VK_PRIOR: handle_scroll(vs, -page); handled = true; break;
            case VK_NEXT:  handle_scroll(vs,  page); handled = true; break;
            case VK_HOME:
                vs->scroll_y = 0;
                update_scrollbar(vs);
                InvalidateRect(hwnd, nullptr, FALSE);
                handled = true;
                break;
            case VK_END:
                vs->scroll_y = vs->max_scroll_y;
                update_scrollbar(vs);
                InvalidateRect(hwnd, nullptr, FALSE);
                handled = true;
                break;
            }
        }

        if (handled) return 0;
        break;
    }

    case WM_CHAR:
    case WM_SYSKEYDOWN:
        WLX_TRACE(L"WndProc %s hwnd=%p wp=0x%X",
                  wlx_trace_msg_name_(msg), hwnd, (unsigned)wp);
        break;

    case WM_COMMAND:
        // We don't normally handle WM_COMMAND — child windows don't typically
        // receive it. Logged here only to detect whether TC ever routes a menu
        // command (e.g. File > Reload) at us via the plugin window. Falls
        // through to DefWindowProcW.
        WLX_TRACE(L"WndProc WM_COMMAND hwnd=%p id=%u src=0x%X ctrl=%p",
                  hwnd, (unsigned)LOWORD(wp), (unsigned)HIWORD(wp), (HWND)lp);
        break;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT)
            return TRUE;
        break;

    case WM_ERASEBKGND:
        return TRUE;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------- window class ----------

static void ensure_window_class() {
    if (g_window_class) return;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = ViewWndProc;
    wc.hInstance = g_hModule;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = L"WlxListerineMdView";
    g_window_class = RegisterClassExW(&wc);
}

// ---------- DLL entry ----------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        break;

    case DLL_PROCESS_DETACH:
        // DLL_PROCESS_DETACH runs under the loader lock.  COM Release() calls
        // here can deadlock (terminated threads hold locks, or DWrite shared
        // factory cleanup waits on internal state).  This applies to BOTH the
        // ExitProcess path (reserved != NULL) and the FreeLibrary path.
        //
        // Safe cleanup happens in ListCloseWindow (outside the loader lock).
        // Here we just move COM-holding objects to leaked heap allocations so
        // static destructors find empty shells.  The OS reclaims on exit;
        // FreeLibrary callers accept the leak (plugin is unloading anyway).
        (void)new ComPtr<ID2D1Factory>(std::move(g_d2d_factory));
        (void)new ComPtr<IDWriteFactory>(std::move(g_dwrite_factory));
        (void)new CacheService(std::move(g_cache));
        (void)new std::unique_ptr<Colorizer>(std::move(g_colorizer));
        // ViewState* raw pointers in g_views are intentionally leaked —
        // their RenderEngine/LayoutDocument COM objects must not Release().
        g_views.clear();

        if (reserved == nullptr) {
            // Remove every parent subclass before the DLL unloads — leaving
            // a dangling subclass proc means the next message dispatched to
            // the parent calls into freed code.
            for (auto& [parent, _count] : g_parent_refcount) {
                RemoveWindowSubclass(parent, ParentSubclassProc, PARENT_SUBCLASS_ID);
            }
            g_parent_refcount.clear();
            if (g_msg_hook) {
                UnhookWindowsHookEx(g_msg_hook);
                g_msg_hook = nullptr;
            }
            if (g_window_class) {
                UnregisterClassW(L"WlxListerineMdView", g_hModule);
                g_window_class = 0;
            }
        }
        break;
    }
    return TRUE;
}

static void scroll_to_match(ViewState* vs, const SearchMatch& m) {
    if (!vs->layout) return;
    if (m.block_index < 0 ||
        m.block_index >= static_cast<int>(vs->layout->blocks.size())) return;

    const auto& block = vs->layout->blocks[m.block_index];
    const float viewport_h = vs->renderer ? vs->renderer->dip_height() : 100.0f;
    const float block_top = block.rect.top;
    const float block_bot = block.rect.bottom;

    // Already visible? leave scroll alone.
    if (block_top >= vs->scroll_y && block_bot <= vs->scroll_y + viewport_h) return;

    // Otherwise center the block vertically. Note: for blocks taller than the
    // viewport this shows the block's middle — matches near the block edges
    // may still be clipped. SearchMatch is block-level (no per-char y), so
    // this is the best we can do at the current granularity.
    const float target = block_top - (viewport_h - (block_bot - block_top)) * 0.5f;
    vs->scroll_y = std::clamp(target, 0.0f, vs->max_scroll_y);
    update_scrollbar(vs);
}

// ---------- WLX exports ----------

extern "C" {

HWND __stdcall ListLoadW(HWND ParentWin, wchar_t* FileToLoad, int ShowFlags) {
    WLX_TRACE(L"ListLoadW parent=%p file=%s flags=0x%X",
              ParentWin, FileToLoad ? FileToLoad : L"(null)", ShowFlags);
    ensure_factories();
    if (!g_d2d_factory || !g_dwrite_factory)
        return nullptr;

    ensure_theme();
    ensure_window_class();

    bool dark = (ShowFlags & lcp_darkmode) != 0;
    bool wrap = (ShowFlags & lcp_wraptext) != 0;

    RECT rc;
    GetClientRect(ParentWin, &rc);

    HWND hwnd = CreateWindowExW(
        0, L"WlxListerineMdView", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        0, 0, rc.right, rc.bottom,
        ParentWin, nullptr, g_hModule, nullptr);

    if (!hwnd) return nullptr;

    apply_dark_mode(hwnd, dark);

    auto* vs = new ViewState{};
    vs->hwnd = hwnd;
    vs->parent = ParentWin;
    vs->dark_mode = dark;
    vs->wrap_text = wrap;

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(vs));
    g_views[hwnd] = vs;

    // Create render engine
    vs->renderer = std::make_unique<RenderEngine>(
        g_d2d_factory.Get(), g_dwrite_factory.Get(), g_theme, dark);
    vs->renderer->create_device_resources(hwnd);

    // Load and render document
    load_document(vs, FileToLoad);
    InvalidateRect(hwnd, nullptr, FALSE);

    install_msg_hook();
    vs->subclass_target = install_parent_subclass(ParentWin);

    // Discover the File→Reload command ID by reading TC's accelerator-table
    // resources. This is more reliable than menu inspection (TC's menus are
    // owner-drawn so item text is empty) and gives us the same cmd ID that
    // both F2 (via accel) and the menu item post.
    if (g_reload_menu_id == 0) {
        UINT id = find_reload_id_via_accel_resources();
        if (id) {
            g_reload_menu_id = id;
            WLX_TRACE(L"discovered reload menu id=%u via accelerator resource scan", id);
        }
    }

    return hwnd;
}

int __stdcall ListLoadNextW(HWND ParentWin, HWND PluginWin, wchar_t* FileToLoad, int ShowFlags) {
    WLX_TRACE(L"ListLoadNextW parent=%p plugin=%p file=%s flags=0x%X",
              ParentWin, PluginWin, FileToLoad ? FileToLoad : L"(null)", ShowFlags);
    auto it = g_views.find(PluginWin);
    if (it == g_views.end()) return LISTPLUGIN_ERROR;

    auto* vs = it->second;
    bool new_dark = (ShowFlags & lcp_darkmode) != 0;
    bool new_wrap = (ShowFlags & lcp_wraptext) != 0;

    if (new_dark != vs->dark_mode) {
        vs->dark_mode = new_dark;
        vs->renderer->set_dark_mode(new_dark);
        apply_dark_mode(vs->hwnd, new_dark);
    }
    vs->wrap_text = new_wrap;

    load_document(vs, FileToLoad);
    InvalidateRect(vs->hwnd, nullptr, FALSE);

    return LISTPLUGIN_OK;
}

void __stdcall ListCloseWindow(HWND ListWin) {
    WLX_TRACE(L"ListCloseWindow hwnd=%p", ListWin);
    HWND subclass_target = nullptr;
    auto it = g_views.find(ListWin);
    if (it != g_views.end()) {
        subclass_target = it->second->subclass_target;
        delete it->second;
        g_views.erase(it);
    }
    DestroyWindow(ListWin);
    uninstall_parent_subclass(subclass_target);
    uninstall_msg_hook();
}

void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    ensure_theme();
    const auto& ds = g_theme.config().detect_string;

    // Convert wstring to narrow ANSI for TC
    int len = WideCharToMultiByte(CP_ACP, 0, ds.c_str(), static_cast<int>(ds.size()),
                                   DetectString, maxlen - 1, nullptr, nullptr);
    DetectString[len] = '\0';
}

int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter) {
    WLX_TRACE(L"ListSendCommand hwnd=%p cmd=%d param=%d", ListWin, Command, Parameter);
    auto it = g_views.find(ListWin);
    if (it == g_views.end()) return LISTPLUGIN_ERROR;

    auto* vs = it->second;

    switch (Command) {
    case lc_copy: {
        if (!vs->layout || !vs->sel_anchor.valid() || vs->sel_anchor == vs->sel_active)
            return LISTPLUGIN_ERROR;
        auto lo = std::min(vs->sel_anchor, vs->sel_active);
        auto hi = std::max(vs->sel_anchor, vs->sel_active);
        auto text = extract_selected_text(*vs->layout, lo, hi);
        return copy_to_clipboard(vs->hwnd, text) ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
    }

    case lc_selectall: {
        if (!vs->layout || vs->layout->blocks.empty())
            return LISTPLUGIN_ERROR;
        vs->sel_anchor = TextPosition{0, 0};
        int last = static_cast<int>(vs->layout->blocks.size()) - 1;
        vs->sel_active = TextPosition{last, block_text_length(vs->layout->blocks[last])};
        vs->selecting = false;
        InvalidateRect(vs->hwnd, nullptr, FALSE);
        return LISTPLUGIN_OK;
    }

    case lc_newparams: {
        bool new_dark = (Parameter & lcp_darkmode) != 0;
        bool new_wrap = (Parameter & lcp_wraptext) != 0;
        bool need_relayout = false;
        if (new_dark != vs->dark_mode) {
            vs->dark_mode = new_dark;
            vs->renderer->set_dark_mode(new_dark);
            need_relayout = true;
        }
        if (new_wrap != vs->wrap_text) {
            vs->wrap_text = new_wrap;
            need_relayout = true;
        }
        if (need_relayout) {
            do_layout(vs);
            InvalidateRect(vs->hwnd, nullptr, FALSE);
        }
        return LISTPLUGIN_OK;
    }

    case lc_setpercent: {
        if (vs->layout && vs->max_scroll_y > 0) {
            float pct = static_cast<float>(Parameter) / 100.0f;
            vs->scroll_y = std::clamp(pct * vs->max_scroll_y, 0.0f, vs->max_scroll_y);
            update_scrollbar(vs);
            InvalidateRect(vs->hwnd, nullptr, FALSE);
        }
        return LISTPLUGIN_OK;
    }

    default:
        return LISTPLUGIN_ERROR;
    }
}

int __stdcall ListSearchTextW(HWND ListWin, wchar_t* SearchString, int SearchParameter) {
    WLX_TRACE(L"ListSearchTextW hwnd=%p needle=%s param=0x%X",
              ListWin, SearchString ? SearchString : L"(null)", SearchParameter);
    auto it = g_views.find(ListWin);
    if (it == g_views.end()) return LISTPLUGIN_ERROR;
    if (!SearchString || !*SearchString) return LISTPLUGIN_ERROR;

    auto* vs = it->second;
    if (!vs->layout) return LISTPLUGIN_ERROR;

    SearchQuery q;
    q.needle       = SearchString;
    q.match_case   = (SearchParameter & lcs_matchcase)  != 0;
    q.whole_words  = (SearchParameter & lcs_wholewords) != 0;
    q.backwards    = (SearchParameter & lcs_backwards)  != 0;
    const bool findfirst = (SearchParameter & lcs_findfirst) != 0;

    auto r = search_step(*vs, q, findfirst);
    if (!r.has_match) {
        if (vs->renderer) vs->renderer->set_search_matches({}, -1);
        InvalidateRect(vs->hwnd, nullptr, FALSE);
        return LISTPLUGIN_ERROR;
    }
    scroll_to_match(vs, r.matches[r.cursor]);
    if (vs->renderer) vs->renderer->set_search_matches(r.matches, r.cursor);
    InvalidateRect(vs->hwnd, nullptr, FALSE);
    return LISTPLUGIN_OK;
}

void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps) {
    WLX_TRACE(L"ListSetDefaultParams dps=%p size=%d", dps, dps ? dps->size : -1);
    if (dps && dps->size >= sizeof(ListDefaultParamStruct)) {
        g_default_ini_path = dps->DefaultIniName;
    }
}

} // extern "C"
