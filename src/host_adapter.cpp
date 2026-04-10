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
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>

#include "listerplugin.h"
#include "file_service.h"
#include "markdown_parser.h"
#include "layout_engine.h"
#include "render_engine.h"
#include "interaction_engine.h"
#include "theme_service.h"
#include "cache_service.h"

using Microsoft::WRL::ComPtr;

// ---------- per-window state ----------

struct ViewState {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    bool dark_mode = false;
    std::wstring file_path;

    std::shared_ptr<Document> document;
    std::shared_ptr<LayoutDocument> layout;
    std::unique_ptr<RenderEngine> renderer;
    std::unique_ptr<InteractionEngine> interaction;

    float scroll_y = 0;
    float max_scroll_y = 0;
    int hovered_span = -1;
};

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

// ---------- helpers ----------

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
        std::wstring cfg_path = get_module_dir() + L"wlx-mini-markdown.toml";
        g_theme.load(cfg_path);
        g_theme_loaded = true;
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

    LayoutEngine engine(g_dwrite_factory.Get(), g_theme, vs->dark_mode);
    auto layout = std::make_shared<LayoutDocument>(engine.layout(*vs->document, viewport_width));

    vs->layout = layout;
    vs->interaction = std::make_unique<InteractionEngine>(*vs->layout);

    update_scrollbar(vs);
}

static void load_document(ViewState* vs, const wchar_t* path) {
    vs->file_path = path;
    vs->scroll_y = 0;

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

static LRESULT CALLBACK ViewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* vs = reinterpret_cast<ViewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (vs && vs->renderer && vs->layout) {
            if (vs->renderer->needs_recreate())
                vs->renderer->create_device_resources(hwnd);
            vs->renderer->paint(*vs->layout, vs->scroll_y);
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
        if (!vs || !vs->interaction) break;
        float mx = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                : static_cast<float>(GET_X_LPARAM(lp));
        float my = (vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                 : static_cast<float>(GET_Y_LPARAM(lp))) + vs->scroll_y;
        auto hit = vs->interaction->hit_test(mx, my);

        int new_hover = hit.hit ? hit.span_index : -1;
        if (new_hover != vs->hovered_span) {
            vs->hovered_span = new_hover;
            SetCursor(LoadCursorW(nullptr, hit.hit ? IDC_HAND : IDC_ARROW));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (!vs || !vs->interaction) break;
        float mx = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                : static_cast<float>(GET_X_LPARAM(lp));
        float my = (vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                 : static_cast<float>(GET_Y_LPARAM(lp))) + vs->scroll_y;
        auto hit = vs->interaction->hit_test(mx, my);

        if (hit.hit) {
            auto action = vs->interaction->resolve(hit.target);
            switch (action.action) {
            case InteractionEngine::Action::ScrollToAnchor:
                vs->scroll_y = std::clamp(action.scroll_y, 0.0f, vs->max_scroll_y);
                update_scrollbar(vs);
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            case InteractionEngine::Action::OpenExternal:
                ShellExecuteW(nullptr, L"open", action.target.c_str(),
                              nullptr, nullptr, SW_SHOW);
                break;
            case InteractionEngine::Action::ReloadDocument:
                // Resolve relative path
                if (!vs->file_path.empty()) {
                    std::wstring dir = vs->file_path;
                    auto pos = dir.find_last_of(L"\\/");
                    if (pos != std::wstring::npos)
                        dir = dir.substr(0, pos + 1);
                    std::wstring full = dir + action.target;
                    load_document(vs, full.c_str());
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            default:
                break;
            }
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (!vs) break;
        float page = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        float line = g_theme.fonts().body_size * g_theme.spacing().line_height_factor;

        switch (wp) {
        case VK_UP:    handle_scroll(vs, -line); break;
        case VK_DOWN:  handle_scroll(vs, line); break;
        case VK_PRIOR: handle_scroll(vs, -page); break;
        case VK_NEXT:  handle_scroll(vs, page); break;
        case VK_HOME:
            vs->scroll_y = 0;
            update_scrollbar(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case VK_END:
            vs->scroll_y = vs->max_scroll_y;
            update_scrollbar(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        return 0;
    }

    case WM_SETCURSOR:
        if (vs && vs->hovered_span >= 0) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
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
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ViewWndProc;
    wc.hInstance = g_hModule;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"WlxMiniMarkdownView";
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
        // reserved != NULL means process is terminating — other DLLs may already
        // be unloaded, so calling COM Release() could deadlock or crash.  Let the
        // OS reclaim everything.
        if (reserved != nullptr)
            break;

        // FreeLibrary path: TC is unloading the plugin while the process continues.
        // Release child COM objects before factories to avoid dangling internal refs.
        for (auto& [hwnd, vs] : g_views)
            delete vs;
        g_views.clear();

        g_cache.clear();

        g_d2d_factory.Reset();
        g_dwrite_factory.Reset();

        if (g_window_class) {
            UnregisterClassW(L"WlxMiniMarkdownView", g_hModule);
            g_window_class = 0;
        }
        break;
    }
    return TRUE;
}

// ---------- WLX exports ----------

extern "C" {

HWND __stdcall ListLoadW(HWND ParentWin, wchar_t* FileToLoad, int ShowFlags) {
    ensure_factories();
    if (!g_d2d_factory || !g_dwrite_factory)
        return nullptr;

    ensure_theme();
    ensure_window_class();

    bool dark = (ShowFlags & lcp_darkmode) != 0;

    RECT rc;
    GetClientRect(ParentWin, &rc);

    HWND hwnd = CreateWindowExW(
        0, L"WlxMiniMarkdownView", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        0, 0, rc.right, rc.bottom,
        ParentWin, nullptr, g_hModule, nullptr);

    if (!hwnd) return nullptr;

    auto* vs = new ViewState{};
    vs->hwnd = hwnd;
    vs->parent = ParentWin;
    vs->dark_mode = dark;

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(vs));
    g_views[hwnd] = vs;

    // Create render engine
    vs->renderer = std::make_unique<RenderEngine>(
        g_d2d_factory.Get(), g_dwrite_factory.Get(), g_theme, dark);
    vs->renderer->create_device_resources(hwnd);

    // Load and render document
    load_document(vs, FileToLoad);
    InvalidateRect(hwnd, nullptr, FALSE);

    return hwnd;
}

int __stdcall ListLoadNextW(HWND ParentWin, HWND PluginWin, wchar_t* FileToLoad, int ShowFlags) {
    auto it = g_views.find(PluginWin);
    if (it == g_views.end()) return LISTPLUGIN_ERROR;

    auto* vs = it->second;
    bool new_dark = (ShowFlags & lcp_darkmode) != 0;

    if (new_dark != vs->dark_mode) {
        vs->dark_mode = new_dark;
        vs->renderer->set_dark_mode(new_dark);
    }

    load_document(vs, FileToLoad);
    InvalidateRect(vs->hwnd, nullptr, FALSE);

    return LISTPLUGIN_OK;
}

void __stdcall ListCloseWindow(HWND ListWin) {
    auto it = g_views.find(ListWin);
    if (it != g_views.end()) {
        delete it->second;
        g_views.erase(it);
    }
    DestroyWindow(ListWin);
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
    auto it = g_views.find(ListWin);
    if (it == g_views.end()) return LISTPLUGIN_ERROR;

    auto* vs = it->second;

    switch (Command) {
    case lc_copy:
        // No text selection in this version
        return LISTPLUGIN_ERROR;

    case lc_selectall:
        // No text selection in this version
        return LISTPLUGIN_ERROR;

    case lc_newparams: {
        bool new_dark = (Parameter & lcp_darkmode) != 0;
        if (new_dark != vs->dark_mode) {
            vs->dark_mode = new_dark;
            vs->renderer->set_dark_mode(new_dark);
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

void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps) {
    if (dps && dps->size >= sizeof(ListDefaultParamStruct)) {
        g_default_ini_path = dps->DefaultIniName;
    }
}

} // extern "C"
