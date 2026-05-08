#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/host/context_menu.h"

#include <shellapi.h>
#include <windows.h>

#include <algorithm>

#define WLX_TRACE_TAG L"wlx-host"
#include "runtime/diagnostics/wlx_trace.h"

namespace wlx::runtime::host {

namespace {

constexpr UINT kIdCopy            = 0x9100;
constexpr UINT kIdSelectAll       = 0x9101;
constexpr UINT kIdSearchGoogle    = 0x9102;
constexpr UINT kIdOpenLink        = 0x9103;
constexpr UINT kIdCopyLinkAddress = 0x9104;
constexpr UINT kIdCopyCodeBlock   = 0x9105;
constexpr UINT kIdEditConfig      = 0x9106;
constexpr UINT kIdLangAuto        = 0x9107;
constexpr UINT kIdLangBase        = 0x9200;

void append_separator_if_nonempty(std::vector<MenuItem>& items) {
    if (items.empty()) return;
    if (items.back().kind == MenuItemKind::Separator) return;
    items.push_back({MenuItemKind::Separator, true});
}

void trim_trailing_separator(std::vector<MenuItem>& items) {
    while (!items.empty() && items.back().kind == MenuItemKind::Separator)
        items.pop_back();
}

}  // namespace

std::vector<MenuItem> build_menu_items(const MenuContext& ctx) {
    std::vector<MenuItem> items;

    items.push_back({MenuItemKind::Copy,      ctx.has_selection});
    items.push_back({MenuItemKind::SelectAll, true});

    append_separator_if_nonempty(items);
    items.push_back({MenuItemKind::SearchGoogle, ctx.has_selection});

    if (ctx.link.present) {
        append_separator_if_nonempty(items);
        items.push_back({MenuItemKind::OpenLink,        true});
        items.push_back({MenuItemKind::CopyLinkAddress, true});
    }

    if (ctx.code_block.present) {
        append_separator_if_nonempty(items);
        items.push_back({MenuItemKind::CopyCodeBlock, true});
    }

    if (!ctx.config_path.empty()) {
        append_separator_if_nonempty(items);
        items.push_back({MenuItemKind::EditConfig, true});
    }

    if (!ctx.languages.empty()) {
        append_separator_if_nonempty(items);
        items.push_back({MenuItemKind::LanguageSubmenuRoot, true});
    }

    trim_trailing_separator(items);
    return items;
}

namespace {

HMENU build_language_submenu(const MenuContext& ctx) {
    HMENU sub = CreatePopupMenu();
    if (!sub) return nullptr;

    UINT auto_flags = MF_STRING;
    if (ctx.auto_detect_active) auto_flags |= MF_CHECKED;
    AppendMenuW(sub, auto_flags, kIdLangAuto, L"Auto-detect");
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);

    for (size_t i = 0; i < ctx.languages.size(); ++i) {
        const auto& lo = ctx.languages[i];
        UINT flags = MF_STRING;
        if (lo.grammar_id == ctx.active_grammar_id && !ctx.auto_detect_active)
            flags |= MF_CHECKED;
        AppendMenuW(sub, flags, kIdLangBase + static_cast<UINT>(i),
                    lo.display_name.c_str());
    }
    return sub;
}

// Build the menu item for `kind` and append it to `menu`. The language
// submenu is constructed lazily inside the LanguageSubmenuRoot case so
// its ownership is unambiguous: if the case fires, AppendMenuW transfers
// the submenu HMENU to `menu` (DestroyMenu(menu) destroys it
// recursively); if the case never fires, the submenu is never built and
// nothing leaks.
void append_label(HMENU menu, MenuItemKind kind, bool enabled,
                  const MenuContext& ctx) {
    UINT flags = enabled ? MF_STRING : (MF_STRING | MF_GRAYED);
    switch (kind) {
        case MenuItemKind::Separator:
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            return;
        case MenuItemKind::Copy:
            AppendMenuW(menu, flags, kIdCopy, L"&Copy\tCtrl+C");           return;
        case MenuItemKind::SelectAll:
            AppendMenuW(menu, flags, kIdSelectAll, L"Select &All\tCtrl+A");  return;
        case MenuItemKind::SearchGoogle:
            AppendMenuW(menu, flags, kIdSearchGoogle, L"&Search with Google"); return;
        case MenuItemKind::OpenLink:
            AppendMenuW(menu, flags, kIdOpenLink, L"&Open Link");          return;
        case MenuItemKind::CopyLinkAddress:
            AppendMenuW(menu, flags, kIdCopyLinkAddress, L"Copy Link &Address"); return;
        case MenuItemKind::CopyCodeBlock:
            AppendMenuW(menu, flags, kIdCopyCodeBlock, L"Copy Code &Block");     return;
        case MenuItemKind::EditConfig:
            AppendMenuW(menu, flags, kIdEditConfig, L"&Edit Plugin Config");    return;
        case MenuItemKind::LanguageSubmenuRoot: {
            HMENU sub = build_language_submenu(ctx);
            if (sub) {
                AppendMenuW(menu, MF_STRING | MF_POPUP,
                            reinterpret_cast<UINT_PTR>(sub),
                            L"&Force Language");
            }
            return;
        }
    }
}

POINT resolve_anchor(HWND owner, POINT screen_pt) {
    if (screen_pt.x != -1 || screen_pt.y != -1) return screen_pt;
    // TODO: spec says keyboard-invoked menus should anchor at the top-
    // left of the current selection rect on screen, falling back to
    // window-center only when there is no selection. Implementing that
    // requires plumbing a screen-space selection rect through
    // MenuContext (DWrite HitTestTextRange + DIP→client→screen
    // conversion). Window-center is the documented fallback per spec
    // when there is no selection; using it unconditionally here is a
    // deliberate first cut. Right-click is the dominant invocation
    // path; Shift+F10 still opens the menu, just at window center.
    RECT r{};
    GetWindowRect(owner, &r);
    return { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
}

// Maximum number of language entries the cmd-id range can encode:
// kIdLangBase = 0x9200, the next allocated id block starts at 0x9300.
// Keep the language list under this cap or extend the id namespace.
constexpr UINT kMaxLanguages = 0x100;

}  // namespace

MenuResult show_context_menu(HWND owner, POINT screen_pt, const MenuContext& ctx) {
    MenuResult result;

    auto items = build_menu_items(ctx);
    if (items.empty()) return result;

    HMENU menu = CreatePopupMenu();
    if (!menu) {
        WLX_TRACE(L"show_context_menu: CreatePopupMenu failed");
        return result;
    }

    for (const auto& item : items)
        append_label(menu, item.kind, item.enabled, ctx);

    POINT anchor = resolve_anchor(owner, screen_pt);

    SetForegroundWindow(owner);
    UINT cmd = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        anchor.x, anchor.y, 0, owner, nullptr);

    DestroyMenu(menu);

    if (cmd == 0) return result;

    if (cmd == kIdCopy)            result.kind = MenuResult::Copy;
    else if (cmd == kIdSelectAll)  result.kind = MenuResult::SelectAll;
    else if (cmd == kIdSearchGoogle) result.kind = MenuResult::SearchGoogle;
    else if (cmd == kIdOpenLink)   result.kind = MenuResult::OpenLink;
    else if (cmd == kIdCopyLinkAddress) result.kind = MenuResult::CopyLinkAddress;
    else if (cmd == kIdCopyCodeBlock) {
        result.kind = MenuResult::CopyCodeBlock;
        result.code_block_index = ctx.code_block.block_index;
    }
    else if (cmd == kIdEditConfig) result.kind = MenuResult::EditConfig;
    else if (cmd == kIdLangAuto) {
        result.kind = MenuResult::SetLanguage;
        result.language_id.clear();
    }
    else if (cmd >= kIdLangBase && cmd < kIdLangBase + kMaxLanguages) {
        size_t idx = cmd - kIdLangBase;
        if (idx < ctx.languages.size()) {
            result.kind = MenuResult::SetLanguage;
            result.language_id = ctx.languages[idx].grammar_id;
        }
    }
    else {
        WLX_TRACE(L"show_context_menu: unknown command id %u", cmd);
    }

    return result;
}

}  // namespace wlx::runtime::host
