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

    // Selection
    TextPosition sel_anchor;
    TextPosition sel_active;
    bool selecting = false;
    int hovered_code_block = -1;
    int copied_code_block = -1;
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
    vs->sel_anchor = TextPosition{};
    vs->sel_active = TextPosition{};
    vs->selecting = false;
    vs->hovered_code_block = -1;
    vs->copied_code_block = -1;

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
        if (!vs) break;
        float page = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        float line = g_theme.fonts().body_size * g_theme.spacing().line_height_factor;

        // Ctrl+C — copy selection
        if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (vs->layout && vs->sel_anchor.valid() && vs->sel_anchor != vs->sel_active) {
                auto lo = std::min(vs->sel_anchor, vs->sel_active);
                auto hi = std::max(vs->sel_anchor, vs->sel_active);
                auto text = extract_selected_text(*vs->layout, lo, hi);
                copy_to_clipboard(hwnd, text);
            }
            return 0;
        }

        // Ctrl+A — select all
        if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (vs->layout && !vs->layout->blocks.empty()) {
                vs->sel_anchor = TextPosition{0, 0};
                int last = static_cast<int>(vs->layout->blocks.size()) - 1;
                vs->sel_active = TextPosition{last, block_text_length(vs->layout->blocks[last])};
                vs->selecting = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        // Escape — clear selection
        if (wp == VK_ESCAPE) {
            if (vs->sel_anchor.valid()) {
                clear_selection(vs);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

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
        // ViewState* raw pointers in g_views are intentionally leaked —
        // their RenderEngine/LayoutDocument COM objects must not Release().
        g_views.clear();

        if (reserved == nullptr && g_window_class) {
            UnregisterClassW(L"WlxListerineMdView", g_hModule);
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
        apply_dark_mode(vs->hwnd, new_dark);
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
