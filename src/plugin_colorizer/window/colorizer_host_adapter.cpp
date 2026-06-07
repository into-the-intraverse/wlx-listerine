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
#include <shellapi.h>
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
#include <thread>
#include <vector>

// WLX_TRACE_TAG must be defined before any project header includes
// runtime/diagnostics/wlx_trace.h — link_actions.h transitively pulls
// it in, and the macro fallback would otherwise lock to L"wlx".
#define WLX_TRACE_TAG L"wlx-clr"
#include "runtime/diagnostics/wlx_trace.h"

#include "listerplugin.h"
#include "runtime/interaction/text_selection.h"
#include "runtime/interaction/interaction_engine.h"
#include "runtime/host/link_actions.h"
#include "runtime/io/file_service.h"
#include "runtime/layout/layout_engine.h"
#include "runtime/render/render_engine.h"
#include "runtime/search/search_index.h"
#include "runtime/search/search_ops.h"
#include "runtime/theme/theme_service.h"
#include "runtime/util/string_util.h"
#include "wlx_core/abi.h"
#include "core_dll/colorizer/colorizer.h"  // ColorizeResult / ColorSpan still used by ColorViewState
#include "plugin_colorizer/layout/colorizer_layout.h"
#include "plugin_colorizer/language/path_to_language.h"
#include "plugin_colorizer/language/routing.h"
#include "runtime/search/search_hud.h"
#include "runtime/host/clipboard.h"
#include "runtime/host/context_menu.h"
#include "runtime/host/dark_mode.h"
#include "runtime/host/factories.h"
#include "runtime/host/grammar_menu.h"
#include "runtime/host/hit_test.h"
#include "runtime/host/host_integration.h"
#include "runtime/host/module_path.h"
#include "runtime/host/scroll_handler.h"
#include "runtime/host/selection_helpers.h"
#include "runtime/host/goto_line.h"
#include "runtime/host/view_actions.h"
#include "runtime/host/web_search.h"
#include "runtime/host/window_class.h"

#include <toml++/toml.hpp>

using namespace wlx::core::colorizer;

using namespace wlx::runtime::diagnostics;
using namespace wlx::runtime::host;
using namespace wlx::runtime::interaction;
using namespace wlx::runtime::io;
using namespace wlx::runtime::layout;
using namespace wlx::runtime::render;
using namespace wlx::runtime::search;
using namespace wlx::runtime::theme;
using namespace wlx::runtime::util;

using Microsoft::WRL::ComPtr;

namespace wlx::plugin_colorizer::window {

using namespace wlx::core::colorizer;
using namespace wlx::plugin_colorizer::language;
using namespace wlx::plugin_colorizer::layout;

// File-local: convert ABI WlxColorSpan array into the C++ ColorizeResult type.
// Frees the spans via wlx_core_free_spans. Returns an empty ColorizeResult if
// spans is null or count is zero.
static ColorizeResult abi_spans_to_result(WlxColorSpan* spans, uint32_t count) {
    ColorizeResult out;
    if (!spans || count == 0) {
        if (spans) wlx_core_free_spans(spans);
        return out;
    }
    out.spans.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& s = spans[i];
        ColorSpan cs;
        cs.start = s.start; cs.length = s.length;
        cs.color = s.color; cs.bg_color = s.bg_color;
        cs.has_bg = s.has_bg != 0; cs.modifiers = s.modifiers;
        out.spans.push_back(cs);
    }
    wlx_core_free_spans(spans);
    return out;
}

// ---------- per-window state ----------

struct ColorViewState {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    HWND subclass_target = nullptr;  // window we subclassed (menu owner)
    bool dark_mode = false;
    std::wstring file_path;

    std::shared_ptr<LayoutDocument> layout;
    std::unique_ptr<RenderEngine> renderer;
    std::unique_ptr<SearchHud> hud;
    std::unique_ptr<wlx::runtime::interaction::InteractionEngine> interaction;

    float scroll_y = 0;
    float max_scroll_y = 0;

    // Cached file content (avoids re-reading on resize)
    std::wstring cached_text;
    std::string cached_raw_utf8;
    ColorizeResult cached_colors;

    // ---- viewport-scoped highlight (parse once, color the visible range) ----
    // Parsed once at open via wlx_core_parse; pins its grammar against eviction.
    // Freed automatically on ColorViewState destruction (ListCloseWindow) and on
    // reassignment in load_document/reparse (reload / relang). NOTE: ViewStates
    // are LEAKED on DLL_PROCESS_DETACH (g_views.clear() drops the map entries but
    // never deletes the ColorViewState*), so this TreePtr never destructs under
    // the loader lock — wlx_core_free_tree (which takes the core mutex) is never
    // called at detach. Confirmed: see DllMain DLL_PROCESS_DETACH below.
    wlx_core::TreePtr tree;
    // Per-block (== per-source-line) UTF-8 byte start, parallel to layout->blocks.
    // From layout_source's out-param; maps a viewport byte range -> the blocks it
    // touches for apply_spans_to_range.
    std::vector<int> line_byte_starts;
    // Language the tree was parsed with (tracing only).
    std::string tree_language;
    // Contiguous already-colored byte interval [colored_lo, colored_hi). 0,0 =
    // nothing colored yet. colorize_viewport unions newly-colored windows into
    // this (or resets it on a disjoint jump) to avoid per-paint re-highlight churn.
    uint32_t colored_lo = 0;
    uint32_t colored_hi = 0;

    // Selection
    TextPosition sel_anchor;
    TextPosition sel_active;
    bool selecting = false;

    // Triple-click detection
    DWORD last_dblclk_time = 0;
    int last_dblclk_block = -1;

    // Cursor
    HCURSOR cursor = nullptr;

    bool wrap_text = false;

    // Force-language override (session-only). Empty = auto-detect from extension.
    // Set by the right-click "Force Language" submenu; reset on file reload.
    // Note: when non-empty, the value is taken as-is — apply_cpp_variant is
    // bypassed (an explicit user pick should not be re-routed through the
    // cpp_grammar config). Three call sites read this field:
    // load_document, recolorize_with_force, and the lc_newparams dark-mode
    // branch — keep them in sync if you add another colorize call.
    std::string force_grammar_id;

    SearchIndex search_index;
    std::vector<SearchMatch> matches;
    int current_match = -1;
    SearchQuery last_query;
    bool index_dirty = true;

    GotoPrompt goto_prompt;
};

static_assert(SearchState<ColorViewState>);
static_assert(ColorizerViewLike<ColorViewState>);

// ---------- globals ----------

static HMODULE g_hModule = nullptr;
static ThemeService g_theme;
static bool g_theme_loaded = false;
using wlx::runtime::host::d2d_factory;
using wlx::runtime::host::dwrite_factory;
using wlx::runtime::host::ensure_factories;
static FileService g_file_service;
static WlxCore*   g_colorizer_handle = nullptr;
static ColorizerDisplayConfig g_display_cfg;
static std::unordered_map<HWND, ColorViewState*> g_views;
static ATOM g_window_class = 0;
static std::string g_default_ini_path;

// Forward decl so the HostView<ColorViewState> concept is satisfied at the
// point where HostIntegration<ColorViewState> is instantiated (below).
static void reload_view(ColorViewState& vs, const wchar_t* path);

static HostIntegration<ColorViewState> g_integration;

// ---------- extension / filename → language ----------
//
// The lookup tables and the two pure functions live in
// plugin_colorizer/language/path_to_language.h so they can be unit-tested
// in isolation. We just import them here.
using wlx::plugin_colorizer::language::ext_to_language;
using wlx::plugin_colorizer::language::filename_to_language;

// ---------- helpers ----------

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

using wlx::runtime::host::apply_dark_mode;

static std::wstring get_module_dir() {
    return wlx::runtime::host::get_module_dir(g_hModule);
}


// TC's detect_string syntax supports EXT="…" (matched against whatever
// follows the LAST dot in the filename) plus content tests. It does NOT
// support filename-pattern matchers, so dotless files like Pipfile must
// rely on TC treating the whole filename as the "extension" — that's why
// EXT="DOCKERFILE" / EXT="MAKEFILE" / EXT="PIPFILE" work for files with
// no dot at all.
//
// Dotfile entries (e.g., .bashrc → ext "bashrc") are listed without the
// leading dot per TC convention.
//
// Adding a row here is half the wiring; the other half is updating
// kExtTable in plugin_colorizer/language/path_to_language.h so the
// language lookup actually returns a grammar id.
static constexpr wchar_t kDefaultDetectString[] =
    // C / C++ / Python / JS / TS
    L"EXT=\"C\" | EXT=\"H\" | EXT=\"CPP\" | EXT=\"CC\" | EXT=\"CXX\" | EXT=\"HPP\" | "
    L"EXT=\"HXX\" | EXT=\"PY\" | EXT=\"PYI\" | EXT=\"JS\" | EXT=\"MJS\" | EXT=\"CJS\" | "
    L"EXT=\"JSX\" | EXT=\"TS\" | EXT=\"TSX\" | EXT=\"MTS\" | EXT=\"RS\" | EXT=\"GO\" | "
    L"EXT=\"JAVA\" | EXT=\"CS\" | EXT=\"PHP\" | EXT=\"LUA\" | "
    // Shell + dotfile rc files (TC treats `.bashrc` as ext=BASHRC, etc.)
    L"EXT=\"SH\" | EXT=\"BASH\" | EXT=\"ZSH\" | "
    L"EXT=\"BASHRC\" | EXT=\"BASH_PROFILE\" | EXT=\"BASH_ALIASES\" | EXT=\"BASH_LOGOUT\" | "
    L"EXT=\"ZSHRC\" | EXT=\"ZSHENV\" | EXT=\"ZPROFILE\" | EXT=\"ZLOGIN\" | EXT=\"ZLOGOUT\" | "
    L"EXT=\"PROFILE\" | EXT=\"ENVRC\" | "
    // PowerShell / Vim
    L"EXT=\"PS1\" | EXT=\"PSM1\" | EXT=\"PSD1\" | "
    L"EXT=\"VIM\" | EXT=\"VIMRC\" | EXT=\"NVIMRC\" | "
    // Data / config
    L"EXT=\"JSON\" | EXT=\"JSONC\" | EXT=\"TOML\" | EXT=\"YAML\" | EXT=\"YML\" | "
    // Markup
    L"EXT=\"HTML\" | EXT=\"HTM\" | EXT=\"XML\" | EXT=\"SVG\" | EXT=\"CSS\" | "
    // Visual Studio / MSBuild XML
    L"EXT=\"VCXPROJ\" | EXT=\"CSPROJ\" | EXT=\"FSPROJ\" | EXT=\"VBPROJ\" | EXT=\"PROJ\" | "
    L"EXT=\"PROPS\" | EXT=\"TARGETS\" | EXT=\"FILTERS\" | EXT=\"SLNX\" | "
    L"EXT=\"XAML\" | EXT=\"RESX\" | "
    // Build / DevOps
    L"EXT=\"CMAKE\" | EXT=\"SQL\" | EXT=\"DOCKERFILE\" | EXT=\"DOCKERIGNORE\" | "
    // Git
    L"EXT=\"GITCONFIG\" | EXT=\"GITMODULES\" | EXT=\"GITIGNORE\" | EXT=\"GITATTRIBUTES\" | "
    // npm / general ignore files
    L"EXT=\"NPMIGNORE\" | "
    // Filename-routed catch-alls — these claim broader extensions and rely
    // on filename_to_language() to specialize the few names we actually
    // want (CMakeLists.txt / CMakeCache.txt / uv.lock / Pipfile.lock /
    // bun.lock / poetry.lock). Files we don't recognize fall through to
    // plain-text rendering inside our viewer.
    L"EXT=\"TXT\" | EXT=\"LOCK\" | EXT=\"PIPFILE\"";

// Phase 4 (Task 4.9 audit): NOT lifted to runtime/host. This plugin's
// ensure_theme overrides the default detect_string and parses a colorizer-
// specific [display] section (line_numbers, word_wrap, tab_width, show_*,
// cpp_grammar, …). The md plugin has none of that. The shared portion
// (load TOML + set loaded flag) is small enough that lifting it would
// require either a callback parameter or a virtual hook — not worth it.
static void ensure_theme() {
    if (!g_theme_loaded) {
        std::wstring cfg_path = get_module_dir() + L"wlx-listerine-colorizer.toml";
        g_theme.load(cfg_path);
        g_theme_loaded = true;

        // Override md-only default detect_string with colorizer's full set
        if (g_theme.config().detect_string == L"EXT=\"MD\" | EXT=\"MARKDOWN\"")
            g_theme.mutable_config().detect_string = kDefaultDetectString;

        // Parse [display] section — ThemeService doesn't know about it
        try {
            auto tbl = toml::parse_file(wstring_to_utf8(cfg_path));
            if (auto v = tbl["display"]["line_numbers"].value<bool>())
                g_display_cfg.line_numbers = *v;
            if (auto v = tbl["display"]["word_wrap"].value<bool>())
                g_display_cfg.word_wrap = *v;
            if (auto v = tbl["display"]["tab_width"].value<int64_t>())
                g_display_cfg.tab_width = static_cast<int>(*v);
            if (auto v = tbl["display"]["show_whitespace"].value<std::string>()) {
                if (*v == "all") g_display_cfg.show_whitespace = ShowWhitespace::All;
                else if (*v == "boundary") g_display_cfg.show_whitespace = ShowWhitespace::Boundary;
                else g_display_cfg.show_whitespace = ShowWhitespace::None;
            }
            if (auto v = tbl["display"]["show_indent_guides"].value<bool>())
                g_display_cfg.show_indent_guides = *v;
            if (auto v = tbl["display"]["highlight_trailing"].value<bool>())
                g_display_cfg.highlight_trailing = *v;

            // [colorizer].cpp_grammar — "standard" (default) | "unreal"
            if (auto v = tbl["colorizer"]["cpp_grammar"].value<std::string>()) {
                if (*v == "unreal") {
                    g_display_cfg.cpp_grammar = CppGrammar::Unreal;
                } else if (*v == "standard") {
                    g_display_cfg.cpp_grammar = CppGrammar::Standard;
                } else {
                    WLX_TRACE(L"unknown cpp_grammar value '%hs', defaulting to standard",
                              v->c_str());
                    g_display_cfg.cpp_grammar = CppGrammar::Standard;
                }
            }
        } catch (...) {
            // Parse failure — use defaults
        }

        // The core DLL owns the colorizer singleton and discovers its install
        // dir via GetModuleFileNameW. Plugins just acquire a handle.
        g_colorizer_handle = wlx_core::acquire_compatible();
    }
}

static void update_scrollbar(ColorViewState* vs) {
    wlx::runtime::host::update_scrollbar(*vs);
}

static void do_layout(ColorViewState* vs, const std::wstring& text, const std::string& raw_utf8,
                      const ColorizeResult& colors) {
    if (!dwrite_factory()) return;

    float viewport_width = vs->renderer ? vs->renderer->dip_width() : 1.0f;

    ColorizerDisplayConfig cfg = g_display_cfg;
    cfg.word_wrap = vs->wrap_text;   // ShowFlags wins over TOML default
    cfg.line_height_factor = g_theme.spacing().line_height_factor;

    vs->line_byte_starts.clear();
    auto layout = std::make_shared<LayoutDocument>(
        layout_source(dwrite_factory(), text, raw_utf8,
                      colors, g_theme, vs->dark_mode, viewport_width, cfg,
                      /*timings=*/nullptr, &vs->line_byte_starts));
    // layout_source already builds the line index + gutter, and fills
    // vs->line_byte_starts (one entry per block) for viewport recoloring.

    vs->layout = layout;
    vs->interaction = std::make_unique<InteractionEngine>(*vs->layout);
    update_scrollbar(vs);
    vs->index_dirty = true;
}

// Resolve the grammar id for this view: an explicit force-language override wins
// (taken as-is, NOT re-routed through apply_cpp_variant — see force_grammar_id
// doc), else extension -> filename -> cpp_variant. Empty = unsupported/plain.
static std::string resolve_language(ColorViewState* vs) {
    std::string language = vs->force_grammar_id;
    if (language.empty()) {
        language = ext_to_language(vs->file_path);
        if (language.empty())
            language = filename_to_language(vs->file_path);
        language = apply_cpp_variant(language, g_display_cfg.cpp_grammar, g_colorizer_handle);
    }
    return language;
}

// Highlight the visible byte range against the cached tree and merge the spans
// into the layout's per-line blocks. Called from WM_PAINT (before paint) and once
// after load. No-op when there is no cached tree (the whole-doc fallback already
// colored everything) or no layout. Cheap on scroll: re-highlights only the
// newly-exposed range against the cached tree (no re-parse), and skips entirely
// when the viewport is already within the colored interval.
static void colorize_viewport(ColorViewState* vs) {
    if (!vs || !vs->tree || !vs->layout) return;
    auto& doc = *vs->layout;
    if (doc.blocks.empty() || vs->line_byte_starts.empty()) return;

    const float viewport_h = vs->renderer ? vs->renderer->dip_height() : 100.0f;
    const float top = vs->scroll_y;
    const float bottom = vs->scroll_y + viewport_h;
    // One screenful of overscan on each side so a small scroll doesn't re-trigger.
    const float over_top = top - viewport_h;
    const float over_bottom = bottom + viewport_h;

    const int block_count = static_cast<int>(doc.blocks.size());
    const int n = std::min(block_count, static_cast<int>(vs->line_byte_starts.size()));
    if (n == 0) return;

    // First/last visible (with overscan) block via block rects (Y-sorted).
    int first = -1, last = -1;
    for (int i = 0; i < n; ++i) {
        const auto& r = doc.blocks[static_cast<size_t>(i)].rect;
        if (r.bottom < over_top) continue;
        if (r.top > over_bottom) break;
        if (first < 0) first = i;
        last = i;
    }
    if (first < 0) return;

    const int raw_size = static_cast<int>(vs->cached_raw_utf8.size());
    uint32_t vlo = static_cast<uint32_t>(vs->line_byte_starts[first]);
    uint32_t vhi = (last + 1 < static_cast<int>(vs->line_byte_starts.size()))
                       ? static_cast<uint32_t>(vs->line_byte_starts[last + 1])
                       : static_cast<uint32_t>(raw_size);

    // Already colored? (window inside the contiguous colored interval). Skip.
    if (vs->colored_hi > vs->colored_lo &&
        vlo >= vs->colored_lo && vhi <= vs->colored_hi)
        return;

    WlxColorSpan* spans = nullptr;
    uint32_t count = 0;
    if (wlx_core_highlight_range(g_colorizer_handle, vs->tree.get(),
                                 vs->dark_mode ? 1 : 0, vlo, vhi,
                                 &spans, &count) != 0)
        return;  // highlight failed — leave existing colors as-is
    ColorizeResult result = abi_spans_to_result(spans, count);

    apply_spans_to_range(doc, vs->cached_raw_utf8, vs->line_byte_starts,
                         result, vlo, vhi, g_display_cfg.tab_width);

    // Update the colored interval: union when contiguous/overlapping with the
    // existing one, else (a disjoint jump) reset to the new window. Previously
    // colored blocks keep their color_ranges, which is harmless.
    if (vs->colored_hi <= vs->colored_lo) {
        vs->colored_lo = vlo;
        vs->colored_hi = vhi;
    } else if (vlo <= vs->colored_hi && vhi >= vs->colored_lo) {
        vs->colored_lo = std::min(vs->colored_lo, vlo);
        vs->colored_hi = std::max(vs->colored_hi, vhi);
    } else {
        vs->colored_lo = vlo;
        vs->colored_hi = vhi;
    }
}

// Parse the cached source once with `language` and color the first viewport, OR
// fall back to a whole-doc colorize (cached_colors + do_layout) when the language
// is unsupported, parsing fails, or word-wrap is on (viewport byte->line mapping
// is unreliable with wrap). The fallback is WLX_TRACE'd (no silent cap). Resets
// any prior tree (freeing it, unpinning the old grammar) and the colored interval.
// Caller must have populated vs->cached_text / vs->cached_raw_utf8.
static void reparse_and_colorize(ColorViewState* vs, const std::string& language) {
    // Reset prior state first: a new TreePtr frees the old tree (unpins grammar).
    vs->tree.reset();
    vs->colored_lo = vs->colored_hi = 0;
    vs->cached_colors = {};

    const bool supported = !language.empty() && g_colorizer_handle &&
                           wlx_core_supports(g_colorizer_handle, language.c_str()) == 1;

    // Word-wrap stays eager whole-doc (Invariant B3.4): real measured heights,
    // and byte->line mapping is unreliable with wrap.
    if (supported && !vs->wrap_text) {
        WlxTree* raw = wlx_core_parse(
            g_colorizer_handle, vs->cached_raw_utf8.c_str(),
            static_cast<uint32_t>(vs->cached_raw_utf8.size()), language.c_str());
        if (raw) {
            vs->tree = wlx_core::TreePtr(raw, wlx_core::TreeDeleter{g_colorizer_handle});
            vs->tree_language = language;
            // Skeleton layout (empty colors); colorize the first viewport below.
            do_layout(vs, vs->cached_text, vs->cached_raw_utf8, /*colors=*/{});
            colorize_viewport(vs);
            return;
        }
        WLX_TRACE(L"viewport-colorize fallback: wlx_core_parse failed for '%hs', "
                  L"using whole-doc colorize", language.c_str());
    } else if (supported && vs->wrap_text) {
        WLX_TRACE(L"viewport-colorize fallback: word-wrap on, using whole-doc "
                  L"colorize for '%hs'", language.c_str());
    } else if (!language.empty()) {
        WLX_TRACE(L"viewport-colorize fallback: language '%hs' unsupported, "
                  L"using whole-doc colorize", language.c_str());
    }

    // Whole-doc fallback (renders exactly as before this change). tree stays null,
    // so colorize_viewport is a no-op in WM_PAINT.
    if (supported) {
        WlxColorSpan* spans = nullptr;
        uint32_t count = 0;
        if (wlx_core_colorize(g_colorizer_handle,
                              vs->cached_raw_utf8.c_str(),
                              static_cast<uint32_t>(vs->cached_raw_utf8.size()),
                              language.c_str(),
                              vs->dark_mode ? 1 : 0,
                              0, 0,
                              &spans, &count) == 0) {
            vs->cached_colors = abi_spans_to_result(spans, count);
        }
    }
    do_layout(vs, vs->cached_text, vs->cached_raw_utf8, vs->cached_colors);
}

static void load_document(ColorViewState* vs, const wchar_t* path) {
    vs->file_path = path;
    vs->scroll_y = 0;
    vs->force_grammar_id.clear();
    vs->matches.clear();
    vs->current_match = -1;
    vs->last_query = SearchQuery{};
    vs->index_dirty = true;
    vs->goto_prompt = {};
    // Also clear matches in the renderer: its search_matches_ still holds
    // SearchMatch objects whose block_index values point into the previous
    // file's layout. Any repaint before the next F7 would walk them against
    // the new layout and could draw spurious highlight rects.
    if (vs->renderer) vs->renderer->set_search_matches({}, -1);
    if (vs->hud) vs->hud->clear();

    auto content = g_file_service.read(path);
    if (!content) {
        vs->layout.reset();
        vs->cached_text.clear();
        vs->cached_raw_utf8.clear();
        vs->cached_colors = {};
        vs->tree.reset();           // unpin old grammar on a failed reload
        vs->colored_lo = vs->colored_hi = 0;
        update_scrollbar(vs);
        return;
    }

    // `content` is a function-local optional not read past here (the colorize +
    // do_layout calls below read the destination members), so move the two
    // full-file payloads in instead of deep-copying them.
    vs->cached_text = std::move(content->text);
    vs->cached_raw_utf8 = std::move(content->raw_utf8);

    // Parse once + color the first viewport (or whole-doc fallback). On a reload
    // (ListLoadNextW) this resets vs->tree, freeing the prior file's tree and
    // unpinning its grammar.
    reparse_and_colorize(vs, resolve_language(vs));
    InvalidateRect(vs->hwnd, nullptr, FALSE);
}

static void relayout(ColorViewState* vs) {
    if (vs->cached_text.empty() && vs->file_path.empty()) return;
    do_layout(vs, vs->cached_text, vs->cached_raw_utf8, vs->cached_colors);
}

// No-wrap line breaking is width-independent, so a horizontal resize doesn't
// change line layout — only the right edge each block's selection highlight
// fills to. Skip the full relayout (which would rebuild every block + the
// interaction engine + re-defer materialization) and just stretch the block
// rects to the new width; materialized per-line layouts stay valid.
static void resize_widths_nowrap(ColorViewState* vs) {
    if (!vs->layout || !vs->renderer) return;
    auto& doc = *vs->layout;
    const float new_vw = vs->renderer->dip_width();
    if (!doc.blocks.empty()) {
        // Derive the right margin from existing geometry (rect.right was
        // viewport_width - right_margin) rather than hardcoding it here.
        const float right_margin = doc.viewport_width - doc.blocks.front().rect.right;
        const float new_right = new_vw - right_margin;
        for (auto& b : doc.blocks) {
            b.rect.right = new_right;
            for (auto& r : b.text_runs) r.rect.right = new_right;
        }
    }
    doc.viewport_width = new_vw;
    update_scrollbar(vs);
}

// Re-tokenize the in-memory cached source using the current force_grammar_id
// (or the extension fallback when the override is empty), then re-layout
// and invalidate. Does NOT read from disk — caller must have populated
// vs->cached_text / vs->cached_raw_utf8 via a prior load_document.
// (cached_raw_utf8 is always populated alongside cached_text; the empty
// guard treats "no file loaded" as a no-op.)
static void recolorize_with_force(ColorViewState* vs) {
    if (!vs || vs->cached_raw_utf8.empty()) return;
    // Re-parse the cached source under the (possibly changed) language and color
    // the current viewport. reparse_and_colorize resets vs->tree (freeing the old
    // tree, unpinning the old grammar) and the colored interval.
    reparse_and_colorize(vs, resolve_language(vs));
    InvalidateRect(vs->hwnd, nullptr, FALSE);
}

static void reload_view(ColorViewState& vs, const wchar_t* path) {
    load_document(&vs, path);
    // load_document() invalidates internally for the colorizer
}

static_assert(HostView<ColorViewState>);

// ---------- scroll helper ----------

static void handle_scroll(ColorViewState* vs, float delta) {
    wlx::runtime::host::handle_scroll(*vs, delta);
}

// ---------- selection helpers ----------

static constexpr UINT_PTR TIMER_AUTOSCROLL = 1;

using wlx::runtime::host::block_text_length;
using wlx::runtime::host::hit_test_position;

using wlx::runtime::host::copy_to_clipboard;

static void clear_selection(ColorViewState* vs) {
    wlx::runtime::host::clear_selection(*vs);
}

static void scroll_to_match(ColorViewState* vs, const SearchMatch& m) {
    wlx::runtime::host::scroll_to_match(*vs, m);
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
            // Viewport-scoped highlight: color any newly-visible byte range
            // against the cached tree before painting (no-op without a tree).
            // Scroll/goto/anchor paths InvalidateRect -> WM_PAINT -> here, so no
            // other call site is needed.
            colorize_viewport(vs);
            auto sel_lo = std::min(vs->sel_anchor, vs->sel_active);
            auto sel_hi = std::max(vs->sel_anchor, vs->sel_active);
            vs->renderer->paint(*vs->layout, vs->scroll_y, sel_lo, sel_hi,
                                vs->goto_prompt.active ? &vs->goto_prompt.buffer : nullptr,
                                static_cast<int>(vs->layout->line_tops.size()));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        UINT w = LOWORD(lp);
        UINT h = HIWORD(lp);
        if (vs && vs->renderer && w > 0 && h > 0) {
            vs->renderer->resize(w, h);
            // No-wrap: line layout is width-independent — skip the full relayout
            // and just restretch block widths. Wrap mode must relayout to rewrap.
            if (vs->wrap_text)
                relayout(vs);
            else
                resize_widths_nowrap(vs);
            if (vs->hud) vs->hud->on_parent_resize();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_DPICHANGED: {
        if (vs && vs->renderer) {
            vs->renderer->discard_device_resources();
            vs->renderer->create_device_resources(hwnd);
            relayout(vs);
            // relayout rebuilds a colorless skeleton (tree path uses empty
            // cached_colors); the stale colored interval would otherwise make
            // colorize_viewport skip re-highlighting. Reset it so the next paint
            // recolors the viewport against the (still-valid) cached tree.
            vs->colored_lo = vs->colored_hi = 0;
            if (vs->hud) vs->hud->on_parent_resize();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_VSCROLL: {
        if (!vs) break;
        float page = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        float line = g_theme.fonts().code_size * g_theme.spacing().line_height_factor;

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
        float line = g_theme.fonts().code_size * g_theme.spacing().line_height_factor;
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
            // Track cursor type for WM_SETCURSOR
            float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                    : static_cast<float>(GET_X_LPARAM(lp));
            float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                    : static_cast<float>(GET_Y_LPARAM(lp));
            float doc_y = py + vs->scroll_y;

            // URL hit: switch to hand cursor and bail early.
            if (vs->interaction) {
                auto hit = vs->interaction->hit_test(px, doc_y);
                if (hit.hit) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return 0;
                }
            }

            bool over_text = false;
            for (auto& block : vs->layout->blocks) {
                if (block.text_runs.empty()) continue;
                if (doc_y >= block.rect.top && doc_y <= block.rect.bottom &&
                    px >= block.rect.left && px <= block.rect.right) {
                    over_text = true;
                    break;
                }
            }
            vs->cursor = LoadCursorW(nullptr, over_text ? IDC_IBEAM : IDC_ARROW);
            SetCursor(vs->cursor);
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

        // URL click: open in browser and bail before starting a selection drag.
        if (vs->interaction) {
            auto hit = vs->interaction->hit_test(px, doc_y);
            if (hit.hit && hit.target.kind == wlx::runtime::parser::LinkKind::ExternalUrl) {
                wlx::runtime::host::open_external_url(hit.target.url);
                return 0;
            }
        }

        auto pos = hit_test_position(*vs->layout, px, doc_y);

        // Triple-click detection: click on same block within double-click interval after a dblclk
        if (pos.valid() && vs->last_dblclk_block >= 0 &&
            pos.block_index == vs->last_dblclk_block &&
            (GetTickCount() - vs->last_dblclk_time) < GetDoubleClickTime()) {
            // Select entire line (block)
            int len = block_text_length(vs->layout->blocks[pos.block_index]);
            vs->sel_anchor = TextPosition{pos.block_index, 0};
            vs->sel_active = TextPosition{pos.block_index, len};
            vs->selecting = false;
            vs->last_dblclk_block = -1;  // reset so 4th click doesn't re-trigger
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

            // Record for triple-click detection
            vs->last_dblclk_time = GetTickCount();
            vs->last_dblclk_block = pos.block_index;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_CONTEXTMENU: {
        if (!vs || !vs->layout) return 0;

        // Commit any in-progress drag-select.
        if (vs->selecting) {
            vs->selecting = false;
            ReleaseCapture();
            KillTimer(hwnd, TIMER_AUTOSCROLL);
        }

        POINT screen_pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

        // Convert the screen-space WM_CONTEXTMENU coords into doc coords.
        POINT client_pt = screen_pt;
        ScreenToClient(hwnd, &client_pt);
        float ctx_doc_x = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(client_pt.x))
                                       : static_cast<float>(client_pt.x);
        float ctx_doc_y_local = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(client_pt.y))
                                             : static_cast<float>(client_pt.y);
        float ctx_doc_y = ctx_doc_y_local + vs->scroll_y;

        auto langs = available_grammars(g_colorizer_handle);
        auto ctx = build_colorizer_menu_context(*vs, std::move(langs), ctx_doc_x, ctx_doc_y);
        ctx.config_path = get_module_dir() + L"wlx-listerine-colorizer.toml";

        auto result = show_context_menu(hwnd, screen_pt, ctx);

        switch (result.kind) {
        case MenuResult::Copy:
            copy_selection(*vs, hwnd);
            break;

        case MenuResult::SelectAll:
            if (select_all(*vs))
                InvalidateRect(hwnd, nullptr, FALSE);
            break;

        case MenuResult::SearchGoogle: {
            if (vs->sel_anchor.valid() && vs->sel_anchor != vs->sel_active) {
                auto lo = std::min(vs->sel_anchor, vs->sel_active);
                auto hi = std::max(vs->sel_anchor, vs->sel_active);
                auto text = extract_selected_text(*vs->layout, lo, hi);
                search_with_google(text);
            }
            break;
        }

        case MenuResult::EditConfig:
            wlx::runtime::host::open_external_url(ctx.config_path);
            break;

        case MenuResult::SetLanguage:
            // Empty language_id = auto-detect.
            vs->force_grammar_id = result.language_id;
            recolorize_with_force(vs);
            break;

        case MenuResult::OpenLink:
            if (ctx.link.present)
                wlx::runtime::host::open_external_url(ctx.link.url);
            break;

        case MenuResult::CopyLinkAddress:
            if (ctx.link.present) {
                // Reuse the existing clipboard helper. Signature:
                //   bool copy_to_clipboard(HWND owner, const std::wstring& text)
                // declared in runtime/host/clipboard.h.
                wlx::runtime::host::copy_to_clipboard(hwnd, ctx.link.url);
            }
            break;

        case MenuResult::CopyCodeBlock:
        case MenuResult::None:
        default:
            break;  // colorizer has no code-block boundaries
        }

        return 0;
    }

    case WM_TIMER: {
        if (wp == TIMER_AUTOSCROLL && vs && vs->selecting) {
            float line = g_theme.fonts().code_size * g_theme.spacing().line_height_factor;
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
        WLX_TRACE(L"WndProc WM_KEYDOWN hwnd=%p vk=0x%X", hwnd, (unsigned)wp);
        if (!vs) break;

        // Go-to-line prompt swallows all keystrokes while active.
        if (vs->goto_prompt.active) {
            auto step = wlx::runtime::host::goto_handle_key(vs->goto_prompt, (unsigned)wp);
            if (step.action == wlx::runtime::host::GotoAction::Jump)
                wlx::runtime::host::scroll_to_line(*vs, step.line);
            else if (step.action != wlx::runtime::host::GotoAction::Ignore)
                InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        float page = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        float line = g_theme.fonts().code_size * g_theme.spacing().line_height_factor;

        bool handled = false;

        // Ctrl+G — open the go-to-line prompt
        if (wp == 'G' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            vs->goto_prompt.active = true;
            vs->goto_prompt.buffer.clear();
            InvalidateRect(hwnd, nullptr, FALSE);
            handled = true;
        }
        // Ctrl+C — copy selection
        else if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
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
            // else fall through → forwarded to parent
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
        g_hModule, L"WlxListerineColorView", ColorViewWndProc);
}

}  // namespace wlx::plugin_colorizer::window

using namespace wlx::plugin_colorizer::window;

// ---------- DLL entry ----------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        SearchHud::register_class(hModule);
        break;

    case DLL_PROCESS_DETACH:
        // Leak COM objects on detach to avoid deadlocks under loader lock.
        // Same pattern as the md plugin.
        wlx::runtime::host::leak_factories_on_detach();
        g_views.clear();

        if (reserved == nullptr) {
            g_integration.emergency_cleanup();
            if (g_window_class) {
                UnregisterClassW(L"WlxListerineColorView", g_hModule);
                g_window_class = 0;
            }
            SearchHud::unregister_class(g_hModule);
        }
        break;
    }
    return TRUE;
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
        0, L"WlxListerineColorView", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        0, 0, rc.right, rc.bottom,
        ParentWin, nullptr, g_hModule, nullptr);

    if (!hwnd) return nullptr;

    apply_dark_mode(hwnd, dark);

    // Prewarm the likely grammar's cold work (ts_query_new query-compile, ~50ms
    // on the first file of a language) on a background thread so it overlaps the
    // renderer + device-resource + HUD setup below. We join before
    // load_document so its colorize() finds the grammar+query warm; the prewarm
    // serializes with colorize via the core mutex (never races the cache). A
    // jthread auto-joins if anything below throws before the explicit join.
    std::jthread prewarm_thread;
    if (FileToLoad && g_colorizer_handle) {
        std::wstring path = FileToLoad;
        std::string lang = ext_to_language(path);
        if (lang.empty()) lang = filename_to_language(path);
        lang = apply_cpp_variant(lang, g_display_cfg.cpp_grammar, g_colorizer_handle);
        if (!lang.empty()) {
            WlxCore* handle = g_colorizer_handle;
            prewarm_thread = std::jthread([handle, lang]() {
                wlx_core_prewarm(handle, lang.c_str());
            });
        }
    }

    auto* vs = new ColorViewState{};
    vs->hwnd = hwnd;
    vs->parent = ParentWin;
    vs->dark_mode = dark;
    vs->wrap_text = wrap;

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(vs));
    g_views[hwnd] = vs;

    vs->renderer = std::make_unique<RenderEngine>(
        d2d_factory(), dwrite_factory(), g_theme, dark);
    vs->renderer->create_device_resources(hwnd);

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

    // Ensure the prewarm finished (grammar+query now warm) before colorizing.
    if (prewarm_thread.joinable())
        prewarm_thread.join();

    load_document(vs, FileToLoad);

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
        bool new_dark = (Parameter & lcp_darkmode)  != 0;
        bool new_wrap = (Parameter & lcp_wraptext) != 0;
        bool changed = false;

        if (new_dark != vs->dark_mode) {
            vs->dark_mode = new_dark;
            vs->renderer->set_dark_mode(new_dark);
            if (vs->hud) vs->hud->set_dark_mode(new_dark);
            apply_dark_mode(vs->hwnd, new_dark);
            changed = true;
        }
        if (new_wrap != vs->wrap_text) {
            vs->wrap_text = new_wrap;
            changed = true;
        }
        if (changed) {
            // Re-parse + re-color (no file re-read) so the new palette / wrap mode
            // takes effect. reparse_and_colorize honors any force-language override
            // (resolve_language) and re-builds the layout; on a wrap toggle it
            // switches between the incremental (no-wrap) and whole-doc (wrap) path.
            // Guards on empty source (treated as no-op) to match the prior code's
            // "no file loaded" behaviour.
            if (!vs->cached_raw_utf8.empty())
                reparse_and_colorize(vs, resolve_language(vs));
            else
                do_layout(vs, vs->cached_text, vs->cached_raw_utf8, vs->cached_colors);
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
