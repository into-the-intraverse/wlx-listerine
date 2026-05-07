#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/host/grammar_menu.h"
#include "runtime/interaction/interaction_engine.h"
#include "runtime/layout/layout_document.h"
#include "runtime/layout/text_position.h"
#include "runtime/parser/block_node.h"
#include "runtime/parser/link_target.h"

#include <concepts>
#include <string>
#include <vector>
#include <windows.h>

namespace wlx::runtime::host {

// ---------- Public types ----------

struct LinkMenuContext {
    bool         present = false;
    std::wstring url;
    bool         external = false;
};

struct CodeBlockMenuContext {
    bool present = false;
    int  block_index = -1;
};

struct MenuContext {
    bool                          has_selection = false;
    LinkMenuContext               link;
    CodeBlockMenuContext          code_block;
    std::vector<LanguageOption>   languages;
    std::string                   active_grammar_id;
    bool                          auto_detect_active = false;
    std::wstring                  config_path;
};

struct MenuResult {
    enum Kind {
        None             = 0,
        Copy             = 1,
        SelectAll        = 2,
        SearchGoogle     = 3,
        OpenLink         = 4,
        CopyLinkAddress  = 5,
        CopyCodeBlock    = 6,
        EditConfig       = 7,
        SetLanguage      = 8,
    };

    Kind        kind = None;
    std::string language_id;
    int         code_block_index = -1;
};

MenuResult show_context_menu(HWND owner, POINT screen_pt, const MenuContext& ctx);

// ---------- Test-only surface ----------

enum class MenuItemKind {
    Separator,
    Copy, SelectAll, SearchGoogle,
    OpenLink, CopyLinkAddress, CopyCodeBlock,
    EditConfig,
    LanguageSubmenuRoot,
};

struct MenuItem {
    MenuItemKind kind;
    bool         enabled = true;
};

std::vector<MenuItem> build_menu_items_for_test(const MenuContext& ctx);

// ---------- Per-plugin context builders ----------

// Structural concepts for the per-plugin builders. Match the codebase
// style established by Selectable / Scrollable / HostView in this same
// directory: lvalue-reference checks on selection state plus member-
// existence probes for layout and the plugin-specific bits. A view that
// doesn't satisfy the concept fails at the call site instead of deep
// inside the template body.
template <typename V>
concept MdViewLike = requires(V& v) {
    { v.sel_anchor } -> std::same_as<wlx::runtime::layout::TextPosition&>;
    { v.sel_active } -> std::same_as<wlx::runtime::layout::TextPosition&>;
    v.layout;        // any pointer-like; dereferenced for ->blocks
    v.interaction;   // any pointer-like; dereferenced for ->hit_test
};

template <typename V>
concept ColorizerViewLike = requires(V& v) {
    { v.sel_anchor } -> std::same_as<wlx::runtime::layout::TextPosition&>;
    { v.sel_active } -> std::same_as<wlx::runtime::layout::TextPosition&>;
    v.layout;
    { v.force_grammar_id } -> std::same_as<std::string&>;
};

template <MdViewLike V>
MenuContext build_md_menu_context(V& vs, float doc_x, float doc_y) {
    using namespace wlx::runtime::parser;

    MenuContext ctx;
    ctx.has_selection = vs.sel_anchor.valid()
                     && vs.sel_anchor != vs.sel_active;

    if (vs.layout && vs.interaction) {
        auto hit = vs.interaction->hit_test(doc_x, doc_y);
        if (hit.hit) {
            switch (hit.target.kind) {
                case LinkKind::ExternalUrl:
                    ctx.link.present  = true;
                    ctx.link.url      = hit.target.url;
                    ctx.link.external = true;
                    break;
                case LinkKind::RelativeDoc:
                    ctx.link.present = true;
                    ctx.link.url     = hit.target.url;
                    break;
                case LinkKind::InternalAnchor:
                    ctx.link.present = true;
                    ctx.link.url     = L"#" + hit.target.anchor_fragment;
                    break;
            }
        }
    }

    if (vs.layout) {
        for (int i = 0; i < static_cast<int>(vs.layout->blocks.size()); ++i) {
            const auto& b = vs.layout->blocks[i];
            if (b.type != BlockType::CodeFence) continue;
            if (doc_x >= b.rect.left && doc_x <= b.rect.right
             && doc_y >= b.rect.top  && doc_y <= b.rect.bottom) {
                ctx.code_block.present     = true;
                ctx.code_block.block_index = i;
                break;
            }
        }
    }

    return ctx;
}

template <ColorizerViewLike V>
MenuContext build_colorizer_menu_context(V& vs, std::vector<LanguageOption> langs) {
    MenuContext ctx;
    ctx.has_selection = vs.sel_anchor.valid()
                     && vs.sel_anchor != vs.sel_active;
    ctx.languages = std::move(langs);
    ctx.active_grammar_id  = vs.force_grammar_id;
    ctx.auto_detect_active = vs.force_grammar_id.empty();
    return ctx;
}

}  // namespace wlx::runtime::host
