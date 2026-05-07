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
#include <dwmapi.h>
#include <uxtheme.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <vector>

#include "listerplugin.h"
#include "runtime/io/file_service.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/interaction/text_selection.h"
#include "runtime/layout/layout_engine.h"
#include "runtime/render/render_engine.h"
#include "runtime/interaction/interaction_engine.h"
#include "runtime/theme/theme_service.h"
#include "runtime/cache/cache_service.h"
#include "wlx_core/abi.h"
#include "runtime/search/search_index.h"
#include "runtime/search/search_ops.h"
#include "runtime/search/search_hud.h"
#include "runtime/host/clipboard.h"
#include "runtime/host/dark_mode.h"
#include "runtime/host/factories.h"
#include "runtime/host/hit_test.h"
#include "runtime/host/host_integration.h"
#include "runtime/host/module_path.h"
#include "runtime/host/scroll_handler.h"
#include "runtime/host/selection_helpers.h"
#include "runtime/host/view_actions.h"
#include "runtime/host/window_class.h"

#define WLX_TRACE_TAG L"wlx-md"
#include "runtime/diagnostics/wlx_trace.h"

using namespace wlx::runtime::cache;
using namespace wlx::runtime::diagnostics;
using namespace wlx::runtime::host;
using namespace wlx::runtime::interaction;
using namespace wlx::runtime::io;
using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;
using namespace wlx::runtime::render;
using namespace wlx::runtime::search;
using namespace wlx::runtime::theme;

using Microsoft::WRL::ComPtr;

namespace wlx::plugin_md::window {

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
    std::unique_ptr<SearchHud> hud;

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
using wlx::runtime::host::d2d_factory;
using wlx::runtime::host::dwrite_factory;
using wlx::runtime::host::ensure_factories;
static CacheService g_cache;
static FileService g_file_service;
static std::unordered_map<HWND, ViewState*> g_views;
static ATOM g_window_class = 0;
static std::string g_default_ini_path;
static WlxCore*   g_colorizer_handle = nullptr;

// Forward decl so the HostView<ViewState> concept is satisfied at the point
// where HostIntegration<ViewState> is instantiated (below).
static void reload_view(ViewState& vs, const wchar_t* path);

static HostIntegration<ViewState> g_integration;

// ---------- helpers ----------

using wlx::runtime::host::apply_dark_mode;

static std::wstring get_module_dir() {
    return wlx::runtime::host::get_module_dir(g_hModule);
}


// Phase 4 (Task 4.9 audit): NOT lifted to runtime/host. The md and colorizer
// versions diverge — colorizer overrides the default detect_string and parses
// a [display] section that this plugin doesn't have. The shared portion
// (load TOML + set loaded flag) isn't large enough to justify the parameter
// surface a lifted version would need (filename + post-load callback).
static void ensure_theme() {
    if (!g_theme_loaded) {
        std::wstring cfg_path = get_module_dir() + L"wlx-listerine-md.toml";
        g_theme.load(cfg_path);
        g_theme_loaded = true;

        // The core DLL owns the colorizer singleton and discovers its install
        // dir via GetModuleFileNameW. Plugins just acquire a handle.
        g_colorizer_handle = wlx_core::acquire_compatible();
    }
}

static void update_scrollbar(ViewState* vs) {
    wlx::runtime::host::update_scrollbar(*vs);
}

static void do_layout(ViewState* vs) {
    if (!vs->document || !dwrite_factory()) return;

    // Use DIP width — IDWriteTextLayout measures in DIPs, not physical pixels
    float viewport_width = vs->renderer ? vs->renderer->dip_width() : 1.0f;

    // Check layout cache
    LayoutCacheKey lk;
    lk.viewport_width_bucket = CacheService::bucket_width(static_cast<int>(viewport_width));
    lk.theme_hash = g_theme.theme_hash();

    LayoutEngine engine(dwrite_factory(), g_theme, vs->dark_mode, g_colorizer_handle);
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
    if (vs->hud) vs->hud->clear();

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

static void reload_view(ViewState& vs, const wchar_t* path) {
    load_document(&vs, path);
    InvalidateRect(vs.hwnd, nullptr, FALSE);
}

static_assert(HostView<ViewState>);

// ---------- WndProc ----------

static void handle_scroll(ViewState* vs, float delta) {
    wlx::runtime::host::handle_scroll(*vs, delta);
}

static constexpr UINT_PTR TIMER_AUTOSCROLL = 1;
static constexpr UINT_PTR TIMER_COPY_FEEDBACK = 2;

using wlx::runtime::host::block_text_length;
using wlx::runtime::host::hit_test_position;

using wlx::runtime::host::copy_to_clipboard;

static bool is_in_copy_button(const LayoutBlock& block, float x, float y) {
    if (block.type != BlockType::CodeFence) return false;
    float btn_size = 24.0f;
    float pad = 6.0f;
    float bx = block.rect.right - btn_size - pad;
    float by = block.rect.top + pad;
    return x >= bx && x <= bx + btn_size && y >= by && y <= by + btn_size;
}

static void clear_selection(ViewState* vs) {
    wlx::runtime::host::clear_selection(*vs);
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
            if (vs->hud) vs->hud->on_parent_resize();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_DPICHANGED: {
        if (vs && vs->renderer) {
            vs->renderer->discard_device_resources();
            vs->renderer->create_device_resources(hwnd);
            do_layout(vs);
            if (vs->hud) vs->hud->on_parent_resize();
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
            wlx::runtime::host::copy_selection(*vs, hwnd);
            handled = true;
        }
        // Ctrl+A — select all
        else if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (wlx::runtime::host::select_all(*vs))
                InvalidateRect(hwnd, nullptr, FALSE);
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
                if (vs->hud) vs->hud->clear();
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

        // Unhandled keys fall to DefWindowProcW. TC's TranslateAccelerator runs on
        // the message loop ahead of DispatchMessage, so F5/F7/N/P/W are claimed
        // upstream and never reach this point. F2 is the exception — TC consumes
        // it before TranslateAccelerator, which is why we intercept it via the
        // WH_GETMESSAGE hook (see wlx_host_common.h). If a new accel key ever
        // reaches this branch, the trace below will surface it.
        WLX_TRACE(L"WM_KEYDOWN unhandled, falling through vk=0x%X", (unsigned)wp);
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
    g_window_class = wlx::runtime::host::ensure_window_class(
        g_hModule, L"WlxListerineMdView", ViewWndProc);
}

}  // namespace wlx::plugin_md::window

using namespace wlx::plugin_md::window;

// ---------- DLL entry ----------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        SearchHud::register_class(hModule);
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
        wlx::runtime::host::leak_factories_on_detach();
        (void)new CacheService(std::move(g_cache));
        // ViewState* raw pointers in g_views are intentionally leaked —
        // their RenderEngine/LayoutDocument COM objects must not Release().
        g_views.clear();

        if (reserved == nullptr) {
            g_integration.emergency_cleanup();
            if (g_window_class) {
                UnregisterClassW(L"WlxListerineMdView", g_hModule);
                g_window_class = 0;
            }
            SearchHud::unregister_class(g_hModule);
        }
        break;
    }
    return TRUE;
}

static void scroll_to_match(ViewState* vs, const SearchMatch& m) {
    wlx::runtime::host::scroll_to_match(*vs, m);
}

// ---------- WLX exports ----------

extern "C" {

HWND __stdcall ListLoadW(HWND ParentWin, wchar_t* FileToLoad, int ShowFlags) {
    WLX_TRACE(L"ListLoadW parent=%p file=%s flags=0x%X",
              ParentWin, FileToLoad ? FileToLoad : L"(null)", ShowFlags);
    ensure_factories();
    if (!d2d_factory() || !dwrite_factory())
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
        d2d_factory(), dwrite_factory(), g_theme, dark);
    vs->renderer->create_device_resources(hwnd);

    // Create search HUD (initially hidden)
    vs->hud = std::make_unique<SearchHud>(
        hwnd, d2d_factory(), dwrite_factory(), g_theme, dark);
    vs->hud->on_navigate = [vs](bool backwards) {
        if (vs->matches.empty() || !vs->layout) return;
        SearchQuery q = vs->last_query;
        q.backwards = backwards;
        auto r = search_step(*vs, q, /*findfirst=*/false);
        if (!r.has_match) return;
        scroll_to_match(vs, r.matches[r.cursor]);
        if (vs->renderer) vs->renderer->set_search_matches(r.matches, r.cursor);
        vs->hud->update(r.cursor + 1, static_cast<int>(r.matches.size()));
        InvalidateRect(vs->hwnd, nullptr, FALSE);
    };

    // Load and render document
    load_document(vs, FileToLoad);
    InvalidateRect(hwnd, nullptr, FALSE);

    g_integration.attach(vs, ParentWin);

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
        if (vs->hud) vs->hud->set_dark_mode(new_dark);
        apply_dark_mode(vs->hwnd, new_dark);
    }
    vs->wrap_text = new_wrap;

    load_document(vs, FileToLoad);
    InvalidateRect(vs->hwnd, nullptr, FALSE);

    return LISTPLUGIN_OK;
}

void __stdcall ListCloseWindow(HWND ListWin) {
    WLX_TRACE(L"ListCloseWindow hwnd=%p", ListWin);
    auto it = g_views.find(ListWin);
    if (it != g_views.end()) {
        g_integration.detach(it->second);  // must come before delete
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
            if (vs->hud) vs->hud->set_dark_mode(new_dark);
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
        if (vs->hud) vs->hud->update(0, 0);
        InvalidateRect(vs->hwnd, nullptr, FALSE);
        return LISTPLUGIN_ERROR;
    }
    scroll_to_match(vs, r.matches[r.cursor]);
    if (vs->renderer) vs->renderer->set_search_matches(r.matches, r.cursor);
    if (vs->hud) vs->hud->update(r.cursor + 1, static_cast<int>(r.matches.size()));
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
