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
#include <dwmapi.h>
#include <uxtheme.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <memory>

#include "listerplugin.h"
#include "file_service.h"
#include "render_engine.h"
#include "theme_service.h"
#include "colorizer.h"
#include "colorizer_layout.h"

using Microsoft::WRL::ComPtr;

// ---------- per-window state ----------

struct ColorViewState {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    bool dark_mode = false;
    std::wstring file_path;

    std::shared_ptr<LayoutDocument> layout;
    std::unique_ptr<RenderEngine> renderer;

    float scroll_y = 0;
    float max_scroll_y = 0;

    // Selection
    TextPosition sel_anchor;
    TextPosition sel_active;
    bool selecting = false;
};

// ---------- globals ----------

static HMODULE g_hModule = nullptr;
static ThemeService g_theme;
static bool g_theme_loaded = false;
static ComPtr<ID2D1Factory> g_d2d_factory;
static ComPtr<IDWriteFactory> g_dwrite_factory;
static FileService g_file_service;
static std::unique_ptr<Colorizer> g_colorizer;
static ColorizerDisplayConfig g_display_cfg;
static std::unordered_map<HWND, ColorViewState*> g_views;
static ATOM g_window_class = 0;
static std::string g_default_ini_path;

// ---------- extension -> language map ----------

static const struct { const wchar_t* ext; const char* lang; } kExtLangMap[] = {
    { L"c",           "c"          },
    { L"h",           "c"          },
    { L"cpp",         "cpp"        },
    { L"cc",          "cpp"        },
    { L"cxx",         "cpp"        },
    { L"hpp",         "cpp"        },
    { L"hxx",         "cpp"        },
    { L"py",          "python"     },
    { L"js",          "javascript" },
    { L"ts",          "typescript" },
    { L"rs",          "rust"       },
    { L"go",          "go"         },
    { L"java",        "java"       },
    { L"cs",          "c_sharp"    },
    { L"rb",          "ruby"       },
    { L"php",         "php"        },
    { L"lua",         "lua"        },
    { L"sh",          "bash"       },
    { L"bash",        "bash"       },
    { L"ps1",         "powershell" },
    { L"vim",         "vim"        },
    { L"json",        "json"       },
    { L"toml",        "toml"       },
    { L"yaml",        "yaml"       },
    { L"yml",         "yaml"       },
    { L"xml",         "xml"        },
    { L"html",        "html"       },
    { L"css",         "css"        },
    { L"md",          "markdown"   },
    { L"cmake",       "cmake"      },
    { L"sql",         "sql"        },
};

static std::string ext_to_language(const std::wstring& path) {
    auto dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return {};
    std::wstring ext = path.substr(dot + 1);
    // lowercase
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    for (auto& e : kExtLangMap) {
        if (ext == e.ext) return e.lang;
    }
    return {};
}

// ---------- helpers ----------

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static void apply_dark_mode(HWND hwnd, bool dark) {
    BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
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
        std::wstring cfg_path = get_module_dir() + L"wlx-listerine-colorizer.toml";
        g_theme.load(cfg_path);
        g_theme_loaded = true;

        // Parse [display] section manually via TOML — ThemeService doesn't know about it.
        // We re-read the file for the display block.
        // Simpler: hardcode defaults then try to parse with toml++.
        // Since we can't easily include toml here, we read the toml config via ThemeService
        // for colors/fonts/spacing, and parse [display] separately.
        // For now use the defaults from ColorizerDisplayConfig (already set at declaration).

        // Initialize colorizer
        std::wstring base = get_module_dir();
        std::wstring grammar_dir = base + g_theme.config().code_grammar_dir;
        std::wstring theme_dir   = base + g_theme.config().code_theme_dir;
        g_colorizer = std::make_unique<Colorizer>(grammar_dir, theme_dir);

        // line_height_factor from spacing config
        g_display_cfg.line_height_factor = g_theme.spacing().line_height_factor;
    }
}

static void update_scrollbar(ColorViewState* vs) {
    if (!vs->layout || !vs->hwnd) return;

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

static void do_layout(ColorViewState* vs, const std::wstring& text, const std::string& raw_utf8,
                      const ColorizeResult& colors) {
    if (!g_dwrite_factory) return;

    float viewport_width = vs->renderer ? vs->renderer->dip_width() : 1.0f;

    auto layout = std::make_shared<LayoutDocument>(
        layout_source(g_dwrite_factory.Get(), text, raw_utf8,
                      colors, g_theme, vs->dark_mode, viewport_width, g_display_cfg));

    vs->layout = layout;
    update_scrollbar(vs);
}

static void load_document(ColorViewState* vs, const wchar_t* path) {
    vs->file_path = path;
    vs->scroll_y = 0;

    auto content = g_file_service.read(path);
    if (!content) {
        vs->layout.reset();
        update_scrollbar(vs);
        return;
    }

    std::string language = ext_to_language(vs->file_path);
    ColorizeResult colors;
    if (!language.empty() && g_colorizer && g_colorizer->supports(language)) {
        colors = g_colorizer->colorize(content->raw_utf8, language, vs->dark_mode);
    }

    do_layout(vs, content->text, content->raw_utf8, colors);
    InvalidateRect(vs->hwnd, nullptr, FALSE);
}

static void relayout(ColorViewState* vs) {
    if (vs->file_path.empty()) return;
    // Re-read to get colors (we don't cache; acceptable for a simple plugin)
    auto content = g_file_service.read(vs->file_path.c_str());
    if (!content) return;

    std::string language = ext_to_language(vs->file_path);
    ColorizeResult colors;
    if (!language.empty() && g_colorizer && g_colorizer->supports(language)) {
        colors = g_colorizer->colorize(content->raw_utf8, language, vs->dark_mode);
    }

    do_layout(vs, content->text, content->raw_utf8, colors);
}

// ---------- scroll helper ----------

static void handle_scroll(ColorViewState* vs, float delta) {
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

// ---------- selection helpers ----------

static constexpr UINT_PTR TIMER_AUTOSCROLL = 1;

static TextPosition hit_test_position(const LayoutDocument& layout, float x, float y) {
    int block_count = static_cast<int>(layout.blocks.size());

    for (int i = 0; i < block_count; i++) {
        auto& block = layout.blocks[i];
        if (block.text_runs.empty()) continue;
        if (y < block.rect.top || y > block.rect.bottom) continue;
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

    // Snap to nearest block
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
        if (y < (block.rect.top + block.rect.bottom) * 0.5f)
            return TextPosition{closest, 0};
        int len = 0;
        for (auto& run : block.text_runs) len += static_cast<int>(run.text.size());
        return TextPosition{closest, len};
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

static void clear_selection(ColorViewState* vs) {
    vs->sel_anchor = TextPosition{};
    vs->sel_active = TextPosition{};
    vs->selecting = false;
}

// ---------- WndProc ----------

static LRESULT CALLBACK ColorViewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* vs = reinterpret_cast<ColorViewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (vs && vs->renderer && vs->layout) {
            if (vs->renderer->needs_recreate())
                vs->renderer->create_device_resources(hwnd);
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
            relayout(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_VSCROLL: {
        if (!vs) break;
        float page = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        float line = g_theme.fonts().code_size * g_display_cfg.line_height_factor;

        switch (LOWORD(wp)) {
        case SB_LINEUP:   handle_scroll(vs, -line); break;
        case SB_LINEDOWN: handle_scroll(vs,  line); break;
        case SB_PAGEUP:   handle_scroll(vs, -page); break;
        case SB_PAGEDOWN: handle_scroll(vs,  page); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            vs->scroll_y = std::clamp(static_cast<float>(si.nTrackPos),
                                      0.0f, vs->max_scroll_y);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case SB_TOP:
            vs->scroll_y = 0;
            update_scrollbar(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case SB_BOTTOM:
            vs->scroll_y = vs->max_scroll_y;
            update_scrollbar(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (!vs) break;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        float line = g_theme.fonts().code_size * g_display_cfg.line_height_factor;
        handle_scroll(vs, -static_cast<float>(delta) / 120.0f * line * 3.0f);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!vs || !vs->layout) break;
        if (vs->selecting) {
            float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                    : static_cast<float>(GET_X_LPARAM(lp));
            float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                    : static_cast<float>(GET_Y_LPARAM(lp));
            float doc_x = px;
            float doc_y = py + vs->scroll_y;

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
        } else {
            // I-beam cursor over text
            float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                    : static_cast<float>(GET_X_LPARAM(lp));
            float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                    : static_cast<float>(GET_Y_LPARAM(lp));
            float doc_y = py + vs->scroll_y;
            bool over_text = false;
            for (auto& block : vs->layout->blocks) {
                if (block.text_runs.empty()) continue;
                if (doc_y >= block.rect.top && doc_y <= block.rect.bottom &&
                    px >= block.rect.left && px <= block.rect.right) {
                    over_text = true;
                    break;
                }
            }
            SetCursor(LoadCursorW(nullptr, over_text ? IDC_IBEAM : IDC_ARROW));
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

        if (was_dragging) {
            float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                    : static_cast<float>(GET_X_LPARAM(lp));
            float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                    : static_cast<float>(GET_Y_LPARAM(lp));
            float doc_y = py + vs->scroll_y;
            auto pos = hit_test_position(*vs->layout, px, doc_y);
            if (pos.valid()) vs->sel_active = pos;
        }

        if (vs->sel_anchor == vs->sel_active)
            clear_selection(vs);

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
            float line = g_theme.fonts().code_size * g_display_cfg.line_height_factor;
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
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (!vs) break;
        float page = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        float line = g_theme.fonts().code_size * g_display_cfg.line_height_factor;

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
        case VK_DOWN:  handle_scroll(vs,  line); break;
        case VK_PRIOR: handle_scroll(vs, -page); break;
        case VK_NEXT:  handle_scroll(vs,  page); break;
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
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
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
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = ColorViewWndProc;
    wc.hInstance = g_hModule;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = L"WlxListerineColorView";
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
        // Leak COM objects on detach to avoid deadlocks under loader lock.
        // Same pattern as the md plugin.
        (void)new ComPtr<ID2D1Factory>(std::move(g_d2d_factory));
        (void)new ComPtr<IDWriteFactory>(std::move(g_dwrite_factory));
        (void)new std::unique_ptr<Colorizer>(std::move(g_colorizer));
        g_views.clear();

        if (reserved == nullptr && g_window_class) {
            UnregisterClassW(L"WlxListerineColorView", g_hModule);
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
        0, L"WlxListerineColorView", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        0, 0, rc.right, rc.bottom,
        ParentWin, nullptr, g_hModule, nullptr);

    if (!hwnd) return nullptr;

    apply_dark_mode(hwnd, dark);

    auto* vs = new ColorViewState{};
    vs->hwnd = hwnd;
    vs->parent = ParentWin;
    vs->dark_mode = dark;

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(vs));
    g_views[hwnd] = vs;

    vs->renderer = std::make_unique<RenderEngine>(
        g_d2d_factory.Get(), g_dwrite_factory.Get(), g_theme, dark);
    vs->renderer->create_device_resources(hwnd);

    load_document(vs, FileToLoad);

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
            apply_dark_mode(vs->hwnd, new_dark);
            relayout(vs);
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
