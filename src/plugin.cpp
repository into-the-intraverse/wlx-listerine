#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

#include "listerplugin.h"
#include "config.h"
#include "rtf_builder.h"

// ---------- globals ----------

static HMODULE g_hModule = nullptr;
static Config g_config;
static bool g_config_loaded = false;
static HMODULE g_richedit = nullptr;

// Per-window data
struct WndData {
    HWND richedit;
    std::vector<LinkInfo> links;
    bool dark_mode;
    std::string file_path;
};

static std::unordered_map<HWND, WndData*> g_windows;

// ---------- helpers ----------

static std::string get_module_dir() {
    char path[MAX_PATH];
    GetModuleFileNameA(g_hModule, path, MAX_PATH);
    std::string dir(path);
    auto pos = dir.find_last_of("\\/");
    return (pos != std::string::npos) ? dir.substr(0, pos + 1) : dir;
}

static void ensure_config() {
    if (!g_config_loaded) {
        std::string cfg_path = get_module_dir() + "wlx-mini-markdown.toml";
        g_config = load_config(cfg_path);
        g_config_loaded = true;
    }
}

static void ensure_richedit() {
    if (!g_richedit) {
        g_richedit = LoadLibraryW(L"msftedit.dll");
    }
}

static std::string read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// EM_STREAMIN callback
struct StreamData {
    const char* data;
    size_t size;
    size_t pos;
};

static DWORD CALLBACK stream_in_callback(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb) {
    auto* sd = reinterpret_cast<StreamData*>(cookie);
    LONG remaining = static_cast<LONG>(sd->size - sd->pos);
    LONG toRead = (cb < remaining) ? cb : remaining;
    memcpy(buf, sd->data + sd->pos, toRead);
    sd->pos += toRead;
    *pcb = toRead;
    return 0;
}

static void load_rtf_into_richedit(HWND hwndRE, const std::string& rtf) {
    StreamData sd = {rtf.c_str(), rtf.size(), 0};
    EDITSTREAM es = {};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&sd);
    es.pfnCallback = stream_in_callback;
    SendMessageW(hwndRE, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
}

static COLORREF to_colorref(uint32_t rgb) {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static void render_file(WndData* wd, const char* path) {
    std::string content = read_file(path);
    if (content.empty()) {
        SendMessageW(wd->richedit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"(empty file)"));
        return;
    }

    RtfBuilder builder(g_config, wd->dark_mode);
    std::string rtf = builder.build(content.c_str(), content.size());
    wd->links = builder.links();

    load_rtf_into_richedit(wd->richedit, rtf);

    // Apply CFE_LINK to tracked link ranges so EN_LINK fires on click
    for (auto& link : wd->links) {
        CHARRANGE cr = {static_cast<LONG>(link.char_start), static_cast<LONG>(link.char_end)};
        SendMessageW(wd->richedit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_LINK;
        cf.dwEffects = CFE_LINK;
        SendMessageW(wd->richedit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    }

    // Deselect and scroll to top
    CHARRANGE crTop = {0, 0};
    SendMessageW(wd->richedit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crTop));
    SendMessageW(wd->richedit, WM_VSCROLL, SB_TOP, 0);
}

// Parent subclass to intercept WM_NOTIFY (EN_LINK)
static LRESULT CALLBACK parent_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR subclassId, DWORD_PTR refData) {
    if (msg == WM_NOTIFY) {
        auto* nmhdr = reinterpret_cast<NMHDR*>(lp);
        if (nmhdr->code == EN_LINK) {
            auto* enlink = reinterpret_cast<ENLINK*>(lp);
            if (enlink->msg == WM_LBUTTONUP) {
                auto* wd = reinterpret_cast<WndData*>(refData);
                if (wd) {
                    LONG start = enlink->chrg.cpMin;
                    LONG end = enlink->chrg.cpMax;
                    for (auto& link : wd->links) {
                        if (static_cast<LONG>(link.char_start) <= start &&
                            end <= static_cast<LONG>(link.char_end)) {
                            int wlen = MultiByteToWideChar(CP_UTF8, 0, link.url.c_str(), -1, nullptr, 0);
                            std::wstring wurl(wlen - 1, L'\0');
                            MultiByteToWideChar(CP_UTF8, 0, link.url.c_str(), -1, wurl.data(), wlen);
                            ShellExecuteW(nullptr, L"open", wurl.c_str(),
                                          nullptr, nullptr, SW_SHOW);
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ---------- DLL entry ----------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

// ---------- WLX exports ----------

// Forward declaration needed because ListSearchText calls ListSearchTextW
extern "C" int __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter);

extern "C" {

HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    ensure_config();
    ensure_richedit();
    if (!g_richedit) return nullptr;

    bool dark = (ShowFlags & lcp_darkmode) != 0;

    RECT rc;
    GetClientRect(ParentWin, &rc);

    HWND hwndRE = CreateWindowExW(
        0, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_READONLY | ES_NOHIDESEL,
        0, 0, rc.right, rc.bottom,
        ParentWin, nullptr, g_hModule, nullptr);

    if (!hwndRE) return nullptr;

    auto* wd = new WndData{hwndRE, {}, dark, std::string(FileToLoad)};
    SetWindowLongPtrW(hwndRE, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(wd));
    g_windows[hwndRE] = wd;

    // Enable EN_LINK notifications
    DWORD mask = static_cast<DWORD>(SendMessageW(hwndRE, EM_GETEVENTMASK, 0, 0));
    SendMessageW(hwndRE, EM_SETEVENTMASK, 0, mask | ENM_LINK);

    // Set background color
    const auto& colors = dark ? g_config.dark : g_config.light;
    SendMessageW(hwndRE, EM_SETBKGNDCOLOR, 0, to_colorref(colors.background));

    // Install parent subclass for EN_LINK handling
    SetWindowSubclass(ParentWin, parent_subclass, reinterpret_cast<UINT_PTR>(hwndRE),
                      reinterpret_cast<DWORD_PTR>(wd));

    render_file(wd, FileToLoad);

    return hwndRE;
}

int __stdcall ListLoadNext(HWND ParentWin, HWND PluginWin, char* FileToLoad, int ShowFlags) {
    auto it = g_windows.find(PluginWin);
    if (it == g_windows.end()) return LISTPLUGIN_ERROR;

    auto* wd = it->second;
    wd->dark_mode = (ShowFlags & lcp_darkmode) != 0;
    wd->file_path = FileToLoad;

    const auto& colors = wd->dark_mode ? g_config.dark : g_config.light;
    SendMessageW(wd->richedit, EM_SETBKGNDCOLOR, 0, to_colorref(colors.background));

    render_file(wd, FileToLoad);
    return LISTPLUGIN_OK;
}

void __stdcall ListCloseWindow(HWND ListWin) {
    auto it = g_windows.find(ListWin);
    if (it != g_windows.end()) {
        HWND parent = GetParent(ListWin);
        RemoveWindowSubclass(parent, parent_subclass, reinterpret_cast<UINT_PTR>(ListWin));
        delete it->second;
        g_windows.erase(it);
    }
    DestroyWindow(ListWin);
}

void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    ensure_config();
    strncpy(DetectString, g_config.detect_string.c_str(), maxlen - 1);
    DetectString[maxlen - 1] = '\0';
}

int __stdcall ListSearchText(HWND ListWin, char* SearchString, int SearchParameter) {
    int len = MultiByteToWideChar(CP_ACP, 0, SearchString, -1, nullptr, 0);
    std::vector<wchar_t> wide(len);
    MultiByteToWideChar(CP_ACP, 0, SearchString, -1, wide.data(), len);
    return ListSearchTextW(ListWin, wide.data(), SearchParameter);
}

int __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter) {
    DWORD flags = 0;
    if (SearchParameter & lcs_matchcase) flags |= FR_MATCHCASE;
    if (SearchParameter & lcs_wholewords) flags |= FR_WHOLEWORD;

    CHARRANGE sel = {};
    SendMessageW(ListWin, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));

    FINDTEXTEXW ft = {};
    ft.lpstrText = SearchString;

    if (SearchParameter & lcs_backwards) {
        LONG textLen = static_cast<LONG>(SendMessageW(ListWin, WM_GETTEXTLENGTH, 0, 0));
        ft.chrg.cpMin = (SearchParameter & lcs_findfirst) ? textLen : sel.cpMin;
        ft.chrg.cpMax = 0;
    } else {
        flags |= FR_DOWN;
        ft.chrg.cpMin = (SearchParameter & lcs_findfirst) ? 0 : sel.cpMax;
        ft.chrg.cpMax = -1;
    }

    LRESULT found = SendMessageW(ListWin, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
    if (found == -1) return LISTPLUGIN_ERROR;

    SendMessageW(ListWin, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&ft.chrgText));
    SendMessageW(ListWin, EM_SCROLLCARET, 0, 0);

    return LISTPLUGIN_OK;
}

int __stdcall ListPrint(HWND ListWin, char* FileToPrint, char* DefPrinter,
                        int PrintFlags, RECT* Margins) {
    return LISTPLUGIN_ERROR;  // v1: no print support
}

int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter) {
    switch (Command) {
    case lc_copy:
        SendMessageW(ListWin, WM_COPY, 0, 0);
        return LISTPLUGIN_OK;

    case lc_selectall:
        SendMessageW(ListWin, EM_SETSEL, 0, -1);
        return LISTPLUGIN_OK;

    case lc_newparams: {
        auto it = g_windows.find(ListWin);
        if (it == g_windows.end()) return LISTPLUGIN_ERROR;
        auto* wd = it->second;
        bool new_dark = (Parameter & lcp_darkmode) != 0;
        if (new_dark != wd->dark_mode) {
            wd->dark_mode = new_dark;
            const auto& colors = new_dark ? g_config.dark : g_config.light;
            SendMessageW(ListWin, EM_SETBKGNDCOLOR, 0, to_colorref(colors.background));
            if (!wd->file_path.empty()) {
                render_file(wd, wd->file_path.c_str());
            }
        }
        return LISTPLUGIN_OK;
    }

    default:
        return LISTPLUGIN_ERROR;
    }
}

} // extern "C"

