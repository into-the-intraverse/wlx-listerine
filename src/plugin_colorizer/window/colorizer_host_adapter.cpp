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
#include <chrono>
#include <unordered_map>
#include <optional>
#include <string>
#include <memory>
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
#include "wlx_core/abi_spans_to_result.h"
#include "core_dll/colorizer/colorizer.h"  // ColorizeResult / ColorSpan still used by ColorViewState
#include "plugin_colorizer/layout/colorizer_layout.h"
#include "plugin_colorizer/layout/grid_window.h"
#include "plugin_colorizer/layout/grid_geometry.h"
#include "plugin_colorizer/colorize/span_table.h"
#include "plugin_colorizer/colorize/sweep_chunk.h"
#include "plugin_colorizer/language/path_to_language.h"
#include "plugin_colorizer/language/routing.h"
#include "runtime/search/search_hud.h"
#include "runtime/host/clipboard.h"
#include "runtime/host/context_menu.h"
#include "runtime/host/async_loader.h"
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
    std::string cached_raw_utf8;
    ColorizeResult cached_colors;

    // ---- viewport-scoped highlight (parse once, color the visible range) ----
    // Parsed once at open via wlx_core_parse; pins its grammar against eviction.
    // Freed automatically on ColorViewState destruction (ListCloseWindow) and on
    // reassignment in load_document/reparse (reload / relang). NOTE: ViewStates
    // are LEAKED on DLL_PROCESS_DETACH (g_views.clear() drops the map entries but
    // never deletes the ColorViewState*), so this tree never destructs under the
    // loader lock — wlx_core_free_tree (which takes the core mutex) is never
    // called at detach. Confirmed: see DllMain DLL_PROCESS_DETACH below.
    //
    // Shared with at most one in-flight sweep worker; whichever side drops the
    // last reference frees it via wlx_core_free_tree (worker-side frees take
    // the core mutex on the worker thread — never under the loader lock, since
    // ViewStates leak on DLL_PROCESS_DETACH and workers are detached).
    std::shared_ptr<WlxTree> tree;
    // Per-block (== per-source-line) UTF-8 byte start, parallel to layout->blocks.
    // From layout_source's out-param; used by ensure_search_index + selection.
    std::vector<int> line_byte_starts;
    // Language the tree was parsed with (tracing only).
    std::string tree_language;
    // Whole-file span table (sweep output). Once complete, scroll coloring
    // reads this instead of the tree, and the tree is freed.
    wlx::plugin_colorizer::colorize::SpanTable span_table;

    // Grid mode (no-wrap): build context + geometry for slide_grid_window.
    // Null/zeroed in wrap mode (classic eager whole-file blocks).
    std::shared_ptr<wlx::plugin_colorizer::layout::MaterializeCtx> grid_ctx;
    wlx::plugin_colorizer::layout::GridGeometry grid_geo;

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
    // cpp_grammar config). Read via resolve_language, which the load funnels
    // (begin_async_load / begin_async_recolor) call to pick the grammar.
    std::string force_grammar_id;

    SearchIndex search_index;
    std::vector<SearchMatch> matches;
    int current_match = -1;
    SearchQuery last_query;
    bool index_dirty = true;

    GotoPrompt goto_prompt;

    // Async load (Lever 2): the live token is shared with in-flight parse workers
    // so a late worker can safely read generation/closed after the ViewState dies.
    std::shared_ptr<wlx::runtime::host::ViewLiveToken> live;
    uint64_t current_gen = 0;
    wlx::runtime::host::LoadState state = wlx::runtime::host::LoadState::Loading;
};

static_assert(SearchState<ColorViewState>);
static_assert(ColorizerViewLike<ColorViewState>);

// Process-unique message id used to hand a completed parse result from a worker
// thread back to ColorViewWndProc on the UI thread. RegisterWindowMessageW
// guarantees a value distinct from any other plugin's (notably the md plugin's
// WlxListerineMd.ParseDone) — a foreign WndProc can never mishandle it, and it's
// a runtime value so it can't be a switch `case` label.
static UINT wm_colorizer_parse_done() {
    static const UINT m = RegisterWindowMessageW(L"WlxListerineColorizer.ParseDone");
    return m;
}

// Sweep completion (memory M1): a worker chunk-highlighted the whole file into
// a SpanTable; adoption frees the tree. Distinct registered message for the
// same reasons as ParseDone.
static UINT wm_colorizer_sweep_done() {
    static const UINT m = RegisterWindowMessageW(L"WlxListerineColorizer.SweepDone");
    return m;
}

// Phase 1 of a two-phase open (spec 2026-06-11-two-phase-open): the worker
// posts the raw bytes as soon as the read completes; adoption shows plain,
// fully interactive text while the parse continues. Distinct registered
// message for the same reasons as ParseDone.
static UINT wm_colorizer_text_ready() {
    static const UINT m = RegisterWindowMessageW(L"WlxListerineColorizer.TextReady");
    return m;
}

// Plugin-local async parse result. Carries ONLY copyable/move-only COM-free data:
// built on the worker thread, adopted on the UI thread. The TreePtr makes this
// move-only — fine for unique_ptr<ParseResultColor>. A dropped/closed/post-failed
// worker frees the TreePtr locally (wlx_core_free_tree takes the core mutex on
// the worker thread — never under the loader lock, since detach never joins
// workers).
struct ParseResultColor {
    uint64_t generation = 0;
    std::shared_ptr<wlx::runtime::host::ViewLiveToken> live;  // same control block as the spawning view
    bool failed = false;
    std::string  raw_utf8;    // -> cached_raw_utf8
    std::string  language;    // -> tree_language
    bool wrap = false;        // wrap mode the tree path was decided with (B3.4)
    // True when a TextReady post already delivered raw_utf8 (phase 1); the
    // adopt handler then takes the minimal tree + window-clear path and this
    // struct's raw_utf8 is empty. False on the single-phase paths (wrap mode,
    // read failure, or a failed phase-1 post) which still carry the source.
    bool two_phase = false;
    wlx_core::TreePtr tree;   // null => UI runs the whole-doc fallback colorize
};

// Worker -> UI sweep result. COM-free. `aborted` => keep the tree (fallback).
struct SweepResultColor {
    uint64_t generation = 0;
    std::shared_ptr<wlx::runtime::host::ViewLiveToken> live;
    bool aborted = false;
    wlx::plugin_colorizer::colorize::SpanTable table;
};

// Worker -> UI phase-1 result. COM-free. Carries a COPY of the source (the
// worker keeps its original for the parse).
struct TextResultColor {
    uint64_t generation = 0;
    std::shared_ptr<wlx::runtime::host::ViewLiveToken> live;
    std::string raw_utf8;
};

// ---------- globals ----------

static HMODULE g_hModule = nullptr;
static ThemeService g_theme;
static bool g_theme_loaded = false;
using wlx::runtime::host::d2d_factory;
using wlx::runtime::host::dwrite_factory;
using wlx::runtime::host::ensure_factories;
static WlxCore*   g_colorizer_handle = nullptr;
static ColorizerDisplayConfig g_display_cfg;
static std::unordered_map<HWND, ColorViewState*> g_views;
static ATOM g_window_class = 0;

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

// Hard cap on file size. The byte pipeline is int-typed (line_byte_starts,
// viewport byte math), so a multi-GB file would wrap negative — and long before
// that the worker's read would die with bad_alloc -> std::terminate, killing TC.
// Oversize files are refused in ListLoadW/ListLoadNextW (TC falls back to its
// default lister) and on the worker read path (covers an F2 reload of a file
// that grew past the cap).
static constexpr ULONGLONG kMaxFileBytes = 512ull * 1024 * 1024;

static bool file_too_large(const wchar_t* path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad))
        return false;   // missing/unreadable -> let the normal read path fail it
    const ULONGLONG size =
        (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    return size > kMaxFileBytes;
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

static void do_layout(ColorViewState* vs, const std::string& raw_utf8,
                      const ColorizeResult& colors) {
    if (!dwrite_factory()) return;

    float viewport_width = vs->renderer ? vs->renderer->dip_width() : 1.0f;

    ColorizerDisplayConfig cfg = g_display_cfg;
    cfg.word_wrap = vs->wrap_text;   // ShowFlags wins over TOML default
    cfg.line_height_factor = g_theme.spacing().line_height_factor;

    vs->line_byte_starts.clear();
    std::shared_ptr<LayoutDocument> layout;
    if (!cfg.word_wrap) {
        // Implicit grid: skeleton only; blocks materialize per viewport in
        // ensure_grid_window. Whole-doc colors (the no-tree fallback) feed the
        // span table so the window builder serves them like sweep output —
        // idempotent on relayout (DPI/dark) re-feeds.
        std::shared_ptr<MaterializeCtx> grid_ctx;
        GridGeometry geo;
        layout = std::make_shared<LayoutDocument>(layout_grid_skeleton(
            dwrite_factory(), raw_utf8, g_theme, vs->dark_mode, viewport_width,
            cfg, &vs->line_byte_starts, &grid_ctx, &geo));
        vs->grid_ctx = std::move(grid_ctx);
        vs->grid_geo = geo;
        if (!colors.spans.empty()) {
            vs->span_table.clear();
            vs->span_table.append_chunk(colors, 0,
                static_cast<uint32_t>(raw_utf8.size()));
            vs->span_table.seal();
        }
    } else {
        // layout_source already builds the line index + gutter, and fills
        // vs->line_byte_starts (one entry per block) for viewport recoloring.
        layout = std::make_shared<LayoutDocument>(
            layout_source(dwrite_factory(), raw_utf8, colors, g_theme,
                          vs->dark_mode, viewport_width, cfg,
                          /*timings=*/nullptr, &vs->line_byte_starts));
        vs->grid_ctx.reset();
        vs->grid_geo = GridGeometry{};
    }

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

// Pre-paint (grid mode): slide the materialized window over the viewport.
// Colors come from the span table once complete (sweep done or whole-doc
// fallback fed it), else from the live tree (mid-sweep), else plain text
// (unsupported language). Wrap docs (classic whole-file layout_source) have
// their colors baked in at layout time — nothing to do per-paint.
static void ensure_grid_window(ColorViewState* vs) {
    if (!vs || !vs->layout) return;
    if (!vs->layout->is_grid()) {
        // wrap mode: whole-doc colors applied at layout; nothing per-paint
        return;
    }
    if (!vs->grid_ctx) return;
    const float viewport_h = vs->renderer ? vs->renderer->dip_height() : 100.0f;
    auto [first, last] = wlx::plugin_colorizer::layout::grid_window_lines(
        vs->grid_geo, vs->scroll_y, viewport_h, viewport_h);
    const auto raw_size = static_cast<uint32_t>(vs->cached_raw_utf8.size());
    ColorsForRange colors_for = [vs, raw_size](uint32_t lo, uint32_t hi)
        -> wlx::core::colorizer::ColorizeResult {
        // Runs SYNCHRONOUSLY on the UI thread inside slide_grid_window — `vs` is
        // safe to capture (no thread hop).
        if (vs->span_table.complete(raw_size))
            return vs->span_table.slice(lo, hi);
        if (vs->tree) {
            WlxColorSpan* spans = nullptr;
            uint32_t count = 0;
            if (wlx_core_highlight_range(g_colorizer_handle, vs->tree.get(),
                                         vs->dark_mode ? 1 : 0, lo, hi,
                                         &spans, &count) == 0)
                return wlx_core::abi_spans_to_result(spans, count);
        }
        return {};
    };
    slide_grid_window(*vs->layout, vs->grid_geo, *vs->grid_ctx,
                      vs->cached_raw_utf8, vs->line_byte_starts,
                      first, last, colors_for);
}

// UI-thread whole-doc fallback: color the entire cached source with `language`
// into vs->cached_colors and (re)lay out. Used by the adopt handler when the
// worker produced no tree (unsupported language, word-wrap on, or a parse
// failure — byte->line mapping is unreliable with wrap, Invariant B3.4). This is
// the exact fallback the old synchronous reparse_and_colorize ran; it stays on
// the UI thread per the design's decided default. tree stays null, so
// For the no-tree grid path, do_layout feeds the fallback colors into
// span_table so ensure_grid_window serves them like sweep output.
static void apply_whole_doc_fallback(ColorViewState* vs, const std::string& language) {
    vs->cached_colors = {};
    const bool supported = !language.empty() && g_colorizer_handle &&
                           wlx_core_supports(g_colorizer_handle, language.c_str()) == 1;
    if (supported) {
        WLX_TRACE(L"viewport-colorize fallback: whole-doc colorize for '%hs' "
                  L"(wrap=%d / parse-fail)", language.c_str(), vs->wrap_text ? 1 : 0);
        WlxColorSpan* spans = nullptr;
        uint32_t count = 0;
        if (wlx_core_colorize(g_colorizer_handle,
                              vs->cached_raw_utf8.c_str(),
                              static_cast<uint32_t>(vs->cached_raw_utf8.size()),
                              language.c_str(),
                              vs->dark_mode ? 1 : 0,
                              0, 0,
                              &spans, &count) == 0) {
            vs->cached_colors = wlx_core::abi_spans_to_result(spans, count);
        }
    } else if (!language.empty()) {
        WLX_TRACE(L"viewport-colorize fallback: language '%hs' unsupported, "
                  L"plain-text render", language.c_str());
    }
    do_layout(vs, vs->cached_raw_utf8, vs->cached_colors);
}

// The shared worker body (pure; runs OFF the UI thread). Captures only copyable,
// COM-free data — NEVER a ColorViewState. wlx_core_prewarm/parse/supports take
// the core mutex (fine off-thread); no render target / window slide here.
// `raw_utf8` is the source: from disk (read here) or a cached copy passed in.
// wlx_core_prewarm folds in the old ListLoadW prewarm jthread.
static std::unique_ptr<ParseResultColor> color_parse_body(
    const wlx::runtime::host::ParseJob& j,
    std::string raw_utf8,
    std::string language, bool wrap_text, WlxCore* core, bool two_phase) {
    auto r = std::make_unique<ParseResultColor>();
    r->generation = j.generation;
    r->live = j.live;
    r->raw_utf8 = std::move(raw_utf8);
    r->language = language;
    r->wrap = wrap_text;

    if (!language.empty() && core)
        wlx_core_prewarm(core, language.c_str());   // folds in the old prewarm jthread

    // Tree path mirrors the old reparse_and_colorize: supported language, a core
    // handle, and word-wrap off (wrap stays whole-doc — Invariant B3.4). null tree
    // => the adopt handler runs the UI-thread whole-doc fallback.
    const bool tree_path = !language.empty() && core && !wrap_text &&
                           wlx_core_supports(core, language.c_str()) == 1;
    if (tree_path) {
        WlxTree* raw = wlx_core_parse(core, r->raw_utf8.c_str(),
                                      static_cast<uint32_t>(r->raw_utf8.size()),
                                      language.c_str());
        if (raw) r->tree = wlx_core::TreePtr(raw, wlx_core::TreeDeleter{core});
    }
    if (two_phase) {
        // Phase 1 already delivered the source to the UI; don't ship a second
        // copy through the message queue.
        r->raw_utf8.clear();
        r->raw_utf8.shrink_to_fit();
        r->two_phase = true;
    }
    return r;
}

// Shared per-load UI-side reset. Bumps the generation (superseding any in-flight
// worker), stamps the live token, sets Loading, and clears per-load UI state.
// `reset_force` is true for a disk load (new file -> drop the language override)
// and false for a cached recolor (the override / wrap / dark IS the trigger).
static void begin_load_reset(ColorViewState* vs, bool reset_force) {
    vs->current_gen = ++wlx::runtime::host::g_load_gen;
    vs->live->generation.store(vs->current_gen, std::memory_order_release);
    vs->state = wlx::runtime::host::LoadState::Loading;

    // Scroll resets only on a full reload (new file). A cached recolor (force-
    // language / dark / wrap) keeps the reading position; adoption's
    // update_scrollbar clamps it against the new layout height.
    if (reset_force) {
        vs->scroll_y = 0;
        vs->force_grammar_id.clear();
    }
    vs->sel_anchor = TextPosition{};
    vs->sel_active = TextPosition{};
    vs->selecting = false;
    vs->matches.clear();
    vs->current_match = -1;
    vs->last_query = SearchQuery{};
    vs->index_dirty = true;
    vs->goto_prompt = {};
    // The renderer's search_matches_ still holds SearchMatch objects whose
    // block_index values point into the previous layout; clear them so a repaint
    // before the next F7 doesn't walk them against a new/absent layout.
    if (vs->renderer) vs->renderer->set_search_matches({}, -1);
    if (vs->hud) vs->hud->clear();
}

// Disk load funnel (Lever 2). Runs on the UI thread; spawns a detached worker that
// READS the file off-thread, parses, and PostMessages the result back to
// ColorViewWndProc (wm_colorizer_parse_done) for UI-thread adoption.
// INVARIANT: every disk-sourced load (ListLoadW, ListLoadNextW, reload) MUST enter
// here so the generation bump + per-load reset always happen. Always repaints
// (loading frame now; content at adoption).
static void begin_async_load(ColorViewState* vs, const wchar_t* path) {
    vs->file_path = path;
    begin_load_reset(vs, /*reset_force=*/true);
    // NOTE: old tree/span_table/layout stay alive on purpose — they describe the OLD file still on screen during Loading; adoption swaps them atomically (and clears the table).

    const std::string language = resolve_language(vs);   // pure, UI-side
    WlxCore* core = g_colorizer_handle;
    const bool wrap = vs->wrap_text;

    auto job = std::make_unique<wlx::runtime::host::ParseJob>();
    job->path = path;
    job->generation = vs->current_gen;
    job->live = vs->live;
    job->hwnd = vs->hwnd;
    job->done_msg = wm_colorizer_parse_done();
    wlx::runtime::host::spawn_parse_worker<ParseResultColor>(std::move(job),
        [language, wrap, core](const wlx::runtime::host::ParseJob& j)
            -> std::unique_ptr<ParseResultColor> {
            using _clk = std::chrono::steady_clock;
            const auto t_read0 = _clk::now();
            wlx::runtime::io::FileService fs;             // worker-LOCAL, stateless
            std::optional<wlx::runtime::io::FileContent> content;
            if (!file_too_large(j.path.c_str()))   // re-check: F2 reload may have grown the file
                content = fs.read(j.path.c_str());
            if (!content) {
                auto r = std::make_unique<ParseResultColor>();
                r->generation = j.generation;
                r->live = j.live;
                r->failed = true;
                return r;
            }
            // Phase 1 (no-wrap only): post a COPY of the source so the UI can
            // show plain text now; keep the original for the parse below.
            // Mirrors spawn_parse_worker's posting discipline: re-check the
            // shutdown/closed gates, free locally on any failure to deliver.
            bool posted_text = false;
            if (!wrap) {
                auto t = std::make_unique<TextResultColor>();
                t->generation = j.generation;
                t->live = j.live;
                t->raw_utf8 = content->raw_utf8;          // copy
                if (!wlx::runtime::host::g_shutting_down.load(std::memory_order_acquire) &&
                    !j.live->closed.load(std::memory_order_acquire)) {
                    TextResultColor* raw = t.release();
                    if (PostMessage(j.hwnd, wm_colorizer_text_ready(),
                                    static_cast<WPARAM>(j.generation),
                                    reinterpret_cast<LPARAM>(raw)))
                        posted_text = true;
                    else
                        t.reset(raw);   // post failed -> reclaim + free locally
                }
            }
            const auto t_text = _clk::now();
            WLX_TRACE(L"two-phase: read+post %.1f ms (posted=%d)",
                      std::chrono::duration<double, std::milli>(t_text - t_read0).count(),
                      posted_text ? 1 : 0);
            auto r = color_parse_body(j, std::move(content->raw_utf8),
                                      language, wrap, core, posted_text);
            WLX_TRACE(L"two-phase: parse %.1f ms",
                      std::chrono::duration<double, std::milli>(_clk::now() - t_text).count());
            return r;
        });
    // Loading frame now; the worker triggers the real repaint at adoption.
    InvalidateRect(vs->hwnd, nullptr, FALSE);
}

// Cached recolor funnel (Lever 2). Same worker + adopt handler as the disk funnel,
// but the source is the IN-MEMORY cached copy (NO disk re-read — re-reading could
// fail if the file moved). Used by force-language relang and dark/wrap changes.
// Resets vs->tree synchronously so a paint during Loading is a no-op against the
// OLD (now-dropped) tree, closing the same-view-reparse core-mutex paint stall.
static void begin_async_recolor(ColorViewState* vs) {
    if (!vs || vs->cached_raw_utf8.empty()) return;   // nothing loaded -> no-op
    begin_load_reset(vs, /*reset_force=*/false);

    // Drop the stale tree + span table up front: a Loading paint then can't
    // touch the old tree, and the old tree is freed here on the UI thread
    // (off the loader lock).
    vs->tree.reset();
    vs->span_table.clear();

    const std::string language = resolve_language(vs);   // pure, UI-side
    WlxCore* core = g_colorizer_handle;
    const bool wrap = vs->wrap_text;
    std::string raw_copy = vs->cached_raw_utf8;           // captured COPY — no vs

    auto job = std::make_unique<wlx::runtime::host::ParseJob>();
    job->path = vs->file_path;     // unused by this closure; kept for symmetry
    job->generation = vs->current_gen;
    job->live = vs->live;
    job->hwnd = vs->hwnd;
    job->done_msg = wm_colorizer_parse_done();
    wlx::runtime::host::spawn_parse_worker<ParseResultColor>(std::move(job),
        [language, wrap, core,
         raw_copy = std::move(raw_copy)](const wlx::runtime::host::ParseJob& j) mutable
            -> std::unique_ptr<ParseResultColor> {
            // mutable + move: the worker runs this exactly once, so hand the
            // file-sized string to color_parse_body instead of copying it.
            return color_parse_body(j, std::move(raw_copy),
                                    language, wrap, core, /*two_phase=*/false);
        });
    InvalidateRect(vs->hwnd, nullptr, FALSE);
}

// Sweep funnel (memory M1). Spawned after a tree adoption; walks the file in
// adaptive chunks against the SHARED tree (core mutex serializes with viewport
// highlights), accumulating a SpanTable worker-side. Re-checks the live token
// between chunks: close / reload / re-language / dark-flip bumps the generation
// and the sweep self-cancels at the next chunk edge.
static void begin_sweep(ColorViewState* vs) {
    if (!vs->tree) return;
    if (vs->cached_raw_utf8.empty()) {
        // 0-byte file: complete(0) is trivially true and the window builder
        // never touches the tree — nothing to sweep, free the tree now.
        vs->tree.reset();
        return;
    }
    std::shared_ptr<WlxTree> tree = vs->tree;          // shared keep-alive copy
    WlxCore* core = g_colorizer_handle;
    const bool dark = vs->dark_mode;
    const auto fsize = static_cast<uint32_t>(vs->cached_raw_utf8.size());
    WLX_TRACE(L"begin_sweep: fsize=%u dark=%d", fsize, dark ? 1 : 0);

    auto job = std::make_unique<wlx::runtime::host::ParseJob>();
    job->path = vs->file_path;                          // tracing only
    job->generation = vs->current_gen;
    job->live = vs->live;
    job->hwnd = vs->hwnd;
    job->done_msg = wm_colorizer_sweep_done();
    wlx::runtime::host::spawn_parse_worker<SweepResultColor>(std::move(job),
        [tree, core, dark, fsize](const wlx::runtime::host::ParseJob& j)
            -> std::unique_ptr<SweepResultColor> {
            using namespace wlx::plugin_colorizer::colorize;
            using _clk = std::chrono::steady_clock;
            auto r = std::make_unique<SweepResultColor>();
            r->generation = j.generation;
            r->live = j.live;
            uint32_t chunk = kSweepFirstChunkBytes;
            while (!r->table.complete(fsize)) {
                // Superseded / closed / detaching: vanish between chunks. The
                // shared tree ref drops on return; a worker-side last-ref free
                // takes the core mutex on this thread (never the loader lock).
                if (sweep_superseded(*j.live, j.generation))
                    return nullptr;
                const uint32_t lo = r->table.swept_hi();
                const uint32_t hi = std::min(fsize, lo + chunk);
                WlxColorSpan* spans = nullptr;
                uint32_t count = 0;
                auto c0 = _clk::now();
                if (wlx_core_highlight_range(core, tree.get(), dark ? 1 : 0,
                                             lo, hi, &spans, &count) != 0) {
                    r->aborted = true;   // keep the tree; today's behavior is the fallback
                    return r;
                }
                r->table.append_chunk(wlx_core::abi_spans_to_result(spans, count), lo, hi);
                const double chunk_ms = std::chrono::duration<double, std::milli>(
                    _clk::now() - c0).count();
                chunk = next_chunk_bytes(hi - lo, chunk_ms);
            }
            r->table.seal();  // drop vector growth slack before the UI adopts it
            return r;
        });
}


static void relayout(ColorViewState* vs) {
    if (vs->cached_raw_utf8.empty() && vs->file_path.empty()) return;
    do_layout(vs, vs->cached_raw_utf8, vs->cached_colors);
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
    // Grid mode: the loop above stretches blocks ALREADY in the window, but
    // FUTURE materializations (ensure_grid_window after a scroll) build against
    // grid_ctx — update its code width to the new viewport. Must run regardless
    // of doc.blocks emptiness (a pre-first-paint resize leaves the window empty,
    // so the loop is skipped).
    if (vs->grid_ctx) {
        // Same margin the rect-stretch above used; falls back to the skeleton's
        // 8 DIP right margin when the window is empty (pre-first-paint resize).
        const float right_margin = !doc.blocks.empty()
            ? (doc.viewport_width - doc.blocks.front().rect.right)
            : 8.0f;
        const float code_right = new_vw - right_margin;
        vs->grid_ctx->max_code_width =
            std::max(1.0f, code_right - vs->grid_ctx->code_left);
    }
    update_scrollbar(vs);
}

static void reload_view(ColorViewState& vs, const wchar_t* path) {
    begin_async_load(&vs, path);   // funnel arranges the repaint (loading frame)
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

// Map a public (source-line) block index to the window-local blocks slot.
// Identity while first_block_line == 0 (whole-file docs); bounds-checked so a
// window slide between hit-test and use degrades to "no hit", never UB.
static int block_slot(const ColorViewState* vs, int public_index) {
    if (!vs->layout) return -1;
    const int slot = public_index - vs->layout->first_block_line;
    if (slot < 0 || slot >= static_cast<int>(vs->layout->blocks.size())) return -1;
    return slot;
}

static void clear_selection(ColorViewState* vs) {
    wlx::runtime::host::clear_selection(*vs);
}

static void scroll_to_match(ColorViewState* vs, const SearchMatch& m) {
    if (vs->layout && vs->layout->is_grid()) {
        // The match's line may not be materialized; center it arithmetically.
        // The window slides + the highlight paints on the repaint.
        if (m.block_index < 0 || m.block_index >= vs->layout->grid_line_count) return;
        const float line_top =
            (m.block_index < static_cast<int>(vs->layout->line_tops.size()))
                ? vs->layout->line_tops[static_cast<size_t>(m.block_index)]
                : 0.0f;
        const float viewport_h = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        vs->scroll_y = std::clamp(line_top - viewport_h * 0.33f, 0.0f, vs->max_scroll_y);
        InvalidateRect(vs->hwnd, nullptr, FALSE);
        return;
    }
    wlx::runtime::host::scroll_to_match(*vs, m);
}

// Grid-aware search index rebuild. Called before every search_step so the
// index is in the public (source-line) index space when in grid mode.
// Classic mode: no-op (search_step's own index_dirty branch runs build()).
static void ensure_search_index(ColorViewState* vs) {
    if (!vs->index_dirty) return;
    if (vs->layout && vs->layout->is_grid()) {
        const std::string& raw = vs->cached_raw_utf8;
        const auto& starts = vs->line_byte_starts;
        const int tab = g_display_cfg.tab_width;
        vs->search_index.build_lines(
            vs->layout->grid_line_count,
            [&raw, &starts, tab](int line) {
                const int raw_size = static_cast<int>(raw.size());
                const int line_count = static_cast<int>(starts.size());
                const int bs = starts[static_cast<size_t>(line)];
                const int be = (line + 1 < line_count)
                                   ? starts[static_cast<size_t>(line) + 1] - 1
                                   : raw_size;
                return wlx::plugin_colorizer::layout::expand_tabs(
                    wlx::plugin_colorizer::layout::decode_line(
                        raw, bs, std::max(bs, be)),
                    tab, nullptr);
            });
        vs->index_dirty = false;
    }
    // Classic mode: leave index_dirty = true; search_step handles it.
}

// Selection text honoring grid mode (decode from raw) vs classic (blocks).
static std::wstring selection_text(const ColorViewState* vs,
                                   TextPosition lo, TextPosition hi) {
    if (vs->layout && vs->layout->is_grid())
        return wlx::plugin_colorizer::layout::extract_selected_text_grid(
            vs->cached_raw_utf8, vs->line_byte_starts, g_display_cfg.tab_width, lo, hi);
    return wlx::runtime::interaction::extract_selected_text(*vs->layout, lo, hi);
}

// Copy the current selection to the clipboard, grid-aware.
static bool copy_selection_color(const ColorViewState* vs, HWND hwnd) {
    if (!vs->layout) return false;
    if (!vs->sel_anchor.valid()) return false;
    if (vs->sel_anchor == vs->sel_active) return false;
    auto lo = std::min(vs->sel_anchor, vs->sel_active);
    auto hi = std::max(vs->sel_anchor, vs->sel_active);
    auto text = selection_text(vs, lo, hi);
    return wlx::runtime::host::copy_to_clipboard(hwnd, text);
}

// Grid: whole-file selection in PUBLIC line space (last line's expanded length
// needs one decode). Classic: the shared helper.
static bool select_all_color(ColorViewState* vs) {
    if (!vs->layout) return false;
    if (!vs->layout->is_grid())
        return wlx::runtime::host::select_all(*vs);
    const int last = vs->layout->grid_line_count - 1;
    if (last < 0) return false;
    const auto& starts = vs->line_byte_starts;
    const int raw_size = static_cast<int>(vs->cached_raw_utf8.size());
    const int bs = starts[static_cast<size_t>(last)];
    const int be = raw_size;  // last line runs to EOF
    std::wstring expanded = wlx::plugin_colorizer::layout::expand_tabs(
        wlx::plugin_colorizer::layout::decode_line(vs->cached_raw_utf8, bs,
                                                   std::max(bs, be)),
        g_display_cfg.tab_width, nullptr);
    vs->sel_anchor = TextPosition{0, 0};
    vs->sel_active = TextPosition{last, static_cast<int>(expanded.size())};
    vs->selecting = false;   // mirror the shared select_all's contract
    return true;
}

// ---------- WndProc ----------

static LRESULT CALLBACK ColorViewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* vs = reinterpret_cast<ColorViewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    // Async parse adoption (Lever 2). wm_colorizer_parse_done() is a runtime-
    // registered message, so it can't be a switch `case`. Take ownership of the raw
    // Result* FIRST (every path below frees it — and its TreePtr — on scope exit),
    // then recover the live ViewState via g_views — NOT the top-of-function `vs`,
    // which can dangle for a recycled/late-dispatched message — and gate adoption on
    // identity + generation + closed via should_adopt_result.
    if (msg == wm_colorizer_parse_done()) {
        std::unique_ptr<ParseResultColor> res(reinterpret_cast<ParseResultColor*>(lp));
        auto it = g_views.find(hwnd);
        if (it == g_views.end()) return 0;            // closed/recycled -> drop (res + tree free)
        ColorViewState* v = it->second;
        if (!wlx::runtime::host::should_adopt_result(res->live.get(), res->generation,
                                                     v->live.get(), v->current_gen))
            return 0;                                 // superseded/closed -> drop (res + tree free)
        // Ready means "no longer loading" — NOT "has content". On failure below we
        // leave layout null, so a Ready view may still have no layout; every
        // interactive handler must guard `state == Ready && layout`, not state alone.
        v->state = wlx::runtime::host::LoadState::Ready;
        if (res->failed) {
            v->layout.reset();
            v->interaction.reset();                   // held a reference into the dead layout
            v->cached_raw_utf8.clear();
            v->cached_colors = {};
            v->tree.reset();                          // unpin old grammar on a failed reload
            v->span_table.clear();                    // dead file's table must not survive
            // update_scrollbar early-returns on a null layout, so collapse the
            // scrollbar explicitly — it would otherwise keep the dead file's range.
            v->scroll_y = 0;
            v->max_scroll_y = 0;
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (res->two_phase) {
            // Phase 1 already swapped the source + plain skeleton; the bytes
            // didn't change, so the skeleton, line index, search index, scroll
            // and any selection made meanwhile are all still exact. Just bring
            // the colors in.
            v->tree_language = std::move(res->language);
            if (WlxTree* raw_tree = res->tree.release())
                v->tree = std::shared_ptr<WlxTree>(raw_tree,
                                                   wlx_core::TreeDeleter{g_colorizer_handle});
            else
                v->tree.reset();
            if (v->tree && res->wrap != v->wrap_text)
                v->tree.reset();   // wrap flipped mid-gap (defensive; gen bump normally catches it)
            if (v->tree) {
                // Window blocks were built plain — drop them; the next paint's
                // ensure_grid_window rebuilds the visible window through
                // colors_for (tree highlight). Everything else stays put.
                if (v->layout) {
                    v->layout->blocks.clear();
                    v->layout->first_block_line = 0;
                }
                begin_sweep(v);
            } else {
                // Unsupported language -> apply_whole_doc_fallback traces and
                // re-lays plain (harmless); supported-but-parse-fail -> whole-doc
                // colorize fallback feeding the span table. Same semantics as
                // the single-phase fallback.
                apply_whole_doc_fallback(v, v->tree_language);
            }
            WLX_TRACE(L"parse done (two-phase): tree=%d", v->tree ? 1 : 0);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        v->cached_raw_utf8 = std::move(res->raw_utf8);
        v->tree_language = std::move(res->language);
        // res->tree stays move-only (TreePtr); only the ViewState field is shared.
        // release() may yield null (whole-doc fallback) — keep a null shared_ptr
        // empty rather than constructing a deleter-bound null one.
        if (WlxTree* raw_tree = res->tree.release())
            v->tree = std::shared_ptr<WlxTree>(raw_tree, wlx_core::TreeDeleter{g_colorizer_handle});
        else
            v->tree.reset();                          // free old tree; no new tree
        v->span_table.clear();                        // a new file's table must not survive adoption
        // Wrap toggled while this load was in flight (lc_newparams can't recolor
        // before the first adoption — no cached source yet — so the worker was
        // not superseded): the tree was built for the spawn-time wrap mode, but
        // do_layout reads the CURRENT wrap_text. Drop it and take the whole-doc
        // fallback below (Invariant B3.4: never a tree under word-wrap).
        if (v->tree && res->wrap != v->wrap_text)
            v->tree.reset();
        if (v->tree) {
            // Skeleton layout (empty colors); build the first viewport window now.
            do_layout(v, v->cached_raw_utf8, /*colors=*/{});
            ensure_grid_window(v);
            begin_sweep(v);          // memory M1: sweep -> span table -> free tree
        } else {
            // Whole-doc fallback on the UI thread (unsupported / wrap / parse fail).
            apply_whole_doc_fallback(v, v->tree_language);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    // Async sweep adoption (memory M1): sibling of the parse-done handler above.
    // A worker chunk-highlighted the whole file into a SpanTable; adopting it
    // lets scroll coloring read the table and frees the tree (unpins its
    // grammar). Same identity/generation/closed gate; reject paths free res.
    if (msg == wm_colorizer_sweep_done()) {
        std::unique_ptr<SweepResultColor> res(reinterpret_cast<SweepResultColor*>(lp));
        auto it = g_views.find(hwnd);
        if (it == g_views.end()) return 0;
        ColorViewState* v = it->second;
        if (!wlx::runtime::host::should_adopt_result(res->live.get(), res->generation,
                                                     v->live.get(), v->current_gen))
            return 0;                                  // superseded/closed -> drop
        if (res->aborted) {
            WLX_TRACE(L"sweep aborted (highlight_range failed) — keeping tree");
            return 0;                                  // memory-heavy but correct
        }
        v->span_table = std::move(res->table);
        v->tree.reset();                               // free tree + unpin grammar
        WLX_TRACE(L"sweep adopted: %zu spans, tree freed", v->span_table.size());
        return 0;
    }

    if (msg == wm_colorizer_text_ready()) {
        std::unique_ptr<TextResultColor> res(reinterpret_cast<TextResultColor*>(lp));
        auto it = g_views.find(hwnd);
        if (it == g_views.end()) return 0;            // closed/recycled -> drop
        ColorViewState* v = it->second;
        if (!wlx::runtime::host::should_adopt_result(res->live.get(), res->generation,
                                                     v->live.get(), v->current_gen))
            return 0;                                 // superseded/closed -> drop
        // Swap in the new source and show it plain. The OLD file's tree/table
        // describe the previous content — drop them now (the generation bump
        // already cancelled the old sweep; freeing here is on the UI thread,
        // off the loader lock). Colors arrive at ParseDone (two_phase path).
        v->cached_raw_utf8 = std::move(res->raw_utf8);
        v->tree.reset();
        v->span_table.clear();
        v->tree_language.clear();
        do_layout(v, v->cached_raw_utf8, /*colors=*/{});
        v->state = wlx::runtime::host::LoadState::Ready;
        WLX_TRACE(L"text ready: %zu bytes shown plain", v->cached_raw_utf8.size());
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (vs && vs->renderer && vs->layout) {
            if (vs->renderer->needs_recreate())
                vs->renderer->create_device_resources(hwnd);
            // Grid mode: slide the materialized window over the viewport and
            // color the entering lines (from span table / live tree). Wrap mode:
            // whole-doc colors are baked at layout; ensure_grid_window is a no-op.
            // Scroll/goto/anchor paths InvalidateRect -> WM_PAINT -> here, so no
            // other call site is needed.
            ensure_grid_window(vs);
            auto sel_lo = std::min(vs->sel_anchor, vs->sel_active);
            auto sel_hi = std::max(vs->sel_anchor, vs->sel_active);
            vs->renderer->paint(*vs->layout, vs->scroll_y, sel_lo, sel_hi,
                                vs->goto_prompt.active ? &vs->goto_prompt.buffer : nullptr,
                                static_cast<int>(vs->layout->line_tops.size()));
        } else if (vs && vs->renderer) {
            // Loading frame (async parse in flight): no layout yet. Paint a minimal
            // themed clear so light mode doesn't flash raw black before the first
            // result lands. Mirrors RenderEngine::paint's BeginDraw/Clear/EndDraw.
            if (vs->renderer->needs_recreate())
                vs->renderer->create_device_resources(hwnd);
            if (auto* rt = vs->renderer->render_target()) {
                rt->BeginDraw();
                rt->Clear(ThemeService::to_d2d_color(
                    g_theme.palette(vs->dark_mode).background));
                if (rt->EndDraw() == D2DERR_RECREATE_TARGET) {
                    // Device lost mid-clear: rebuild the target now, same as
                    // RenderEngine::paint's recreate handling (its needs_recreate_
                    // flag is private, so recreate eagerly instead of deferring).
                    vs->renderer->discard_device_resources();
                    vs->renderer->create_device_resources(hwnd);
                }
            }
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
            relayout(vs);   // do_layout invalidates the colored interval -> next paint recolors
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
        if (!vs || vs->state != LoadState::Ready || !vs->layout) break;
        if (vs->selecting) {
            float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                    : static_cast<float>(GET_X_LPARAM(lp));
            float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                    : static_cast<float>(GET_Y_LPARAM(lp));
            // Slide the grid window to the current scroll position before hit-testing;
            // the autoscroll timer may have moved the viewport since the last slide.
            // No-op for wrap docs.
            ensure_grid_window(vs);
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
                if (block.rect.top > doc_y) break;   // blocks are Y-ordered: nothing below can hit
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
        if (!vs || vs->state != LoadState::Ready || !vs->layout) break;
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
            int slot = block_slot(vs, pos.block_index);
            int len = slot >= 0 ? block_text_length(vs->layout->blocks[static_cast<size_t>(slot)]) : 0;
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
        if (!vs || vs->state != LoadState::Ready || !vs->layout) break;
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
        if (!vs || vs->state != LoadState::Ready || !vs->layout) break;
        float px = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(GET_X_LPARAM(lp)))
                                : static_cast<float>(GET_X_LPARAM(lp));
        float py = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(GET_Y_LPARAM(lp)))
                                : static_cast<float>(GET_Y_LPARAM(lp));
        float doc_y = py + vs->scroll_y;

        auto pos = hit_test_position(*vs->layout, px, doc_y);
        if (pos.valid()) {
            int slot = block_slot(vs, pos.block_index);
            if (slot >= 0) {
                auto& block = vs->layout->blocks[static_cast<size_t>(slot)];
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
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_CONTEXTMENU: {
        if (!vs || vs->state != LoadState::Ready || !vs->layout) return 0;

        // Commit any in-progress drag-select.
        if (vs->selecting) {
            vs->selecting = false;
            ReleaseCapture();
            KillTimer(hwnd, TIMER_AUTOSCROLL);
        }

        POINT screen_pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

        // Keyboard-invoked menu (VK_APPS / Shift+F10) carries (-1,-1): anchor at
        // the window center and skip the link hit-test — (-1,-1) doc coords miss
        // every block, and there is no meaningful point to test.
        float ctx_doc_x = -1.0f;
        float ctx_doc_y = -1.0f;
        if (screen_pt.x == -1 && screen_pt.y == -1) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            screen_pt = { rc.right / 2, rc.bottom / 2 };
            ClientToScreen(hwnd, &screen_pt);
        } else {
            // Convert the screen-space WM_CONTEXTMENU coords into doc coords.
            POINT client_pt = screen_pt;
            ScreenToClient(hwnd, &client_pt);
            ctx_doc_x = vs->renderer ? vs->renderer->pixel_to_dip_x(static_cast<float>(client_pt.x))
                                     : static_cast<float>(client_pt.x);
            float ctx_doc_y_local = vs->renderer ? vs->renderer->pixel_to_dip_y(static_cast<float>(client_pt.y))
                                                 : static_cast<float>(client_pt.y);
            ctx_doc_y = ctx_doc_y_local + vs->scroll_y;
        }

        auto langs = available_grammars(g_colorizer_handle);
        auto ctx = build_colorizer_menu_context(*vs, std::move(langs), ctx_doc_x, ctx_doc_y);
        ctx.config_path = get_module_dir() + L"wlx-listerine-colorizer.toml";

        auto result = show_context_menu(hwnd, screen_pt, ctx);

        switch (result.kind) {
        case MenuResult::Copy:
            copy_selection_color(vs, hwnd);
            break;

        case MenuResult::SelectAll:
            if (select_all_color(vs))
                InvalidateRect(hwnd, nullptr, FALSE);
            break;

        case MenuResult::SearchGoogle: {
            if (vs->sel_anchor.valid() && vs->sel_anchor != vs->sel_active) {
                auto lo = std::min(vs->sel_anchor, vs->sel_active);
                auto hi = std::max(vs->sel_anchor, vs->sel_active);
                auto text = selection_text(vs, lo, hi);
                search_with_google(text);
            }
            break;
        }

        case MenuResult::EditConfig:
            // Trusted path: plugin-computed module-dir config, never doc content.
            wlx::runtime::host::open_config_file(ctx.config_path);
            break;

        case MenuResult::SetLanguage:
            // Empty language_id = auto-detect. Re-tokenize the in-memory cached
            // source with the new override (no disk re-read) off the UI thread.
            vs->force_grammar_id = result.language_id;
            begin_async_recolor(vs);
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
            // Slide the grid window to the new scroll position before hit-testing;
            // otherwise the hit-test runs against the pre-scroll window and may
            // miss lines that just entered the viewport. No-op for wrap docs.
            ensure_grid_window(vs);

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

        // Ctrl+G — open the go-to-line prompt (no-op while a load is in flight or
        // after a failed one; Ready alone doesn't imply a layout / line index to
        // jump within — see the adoption handler's invariant).
        if (wp == 'G' && (GetKeyState(VK_CONTROL) & 0x8000) &&
            vs->state == LoadState::Ready && vs->layout) {
            vs->goto_prompt.active = true;
            vs->goto_prompt.buffer.clear();
            InvalidateRect(hwnd, nullptr, FALSE);
            handled = true;
        }
        // Ctrl+C — copy selection
        else if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            copy_selection_color(vs, hwnd);
            handled = true;
        }
        // Ctrl+A — select all
        else if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (select_all_color(vs))
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

    case WM_NCDESTROY:
        // Clear the back-pointer so a message dispatched after ListCloseWindow has
        // already deleted the ColorViewState (e.g. a late wm_colorizer_parse_done)
        // reads a null `vs` at the top instead of a dangling one. (g_views/closed
        // already guard adoption; this hardens every other branch.)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;

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
        // Signal in-flight parse workers to bail (they gate PostMessage on this)
        // BEFORE any other teardown. We never join — the worker holds no
        // ColorViewState/COM ref, and it can't return into unmapped code because
        // spawn_parse_worker pins this module for process life (at process exit
        // the OS terminates worker threads before detach runs).
        wlx::runtime::host::g_shutting_down.store(true, std::memory_order_release);
        // Leak COM objects on detach to avoid deadlocks under loader lock.
        // Same pattern as the md plugin. The ColorViewState* in g_views are
        // intentionally leaked (g_views.clear() drops map entries but never
        // deletes them): their shared tree ref would drop — and leaking the
        // ViewState also guarantees a sweep worker can never hold the LAST ref
        // at detach, so wlx_core_free_tree never runs under the loader lock.
        // The OS reclaims on exit; FreeLibrary callers accept the leak (plugin
        // is unloading anyway).
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
    // Refuse null/empty paths (begin_async_load would crash assigning file_path)
    // and oversize files (int-typed byte pipeline / worker bad_alloc — see
    // kMaxFileBytes). Returning null makes TC fall back to its default lister.
    if (!FileToLoad || !*FileToLoad || file_too_large(FileToLoad))
        return nullptr;
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

    // Prewarm (ts_query_new query-compile, ~50ms on the first file of a language)
    // is now folded into the async parse worker (color_parse_body calls
    // wlx_core_prewarm before parsing) — so there's no UI-thread prewarm jthread
    // to spawn or join here. The worker prewarms then parses, all off the UI thread.

    auto* vs = new ColorViewState{};
    vs->hwnd = hwnd;
    vs->parent = ParentWin;
    vs->dark_mode = dark;
    vs->wrap_text = wrap;
    // Liveness token: shared with every parse worker this view spawns, so a late
    // worker can read generation/closed safely after the ColorViewState is gone.
    vs->live = std::make_shared<wlx::runtime::host::ViewLiveToken>();

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
        ensure_search_index(vs);
        auto r = search_step(*vs, q, /*findfirst=*/false);
        if (!r.has_match) return;
        scroll_to_match(vs, vs->matches[r.cursor]);
        if (vs->renderer) vs->renderer->set_search_matches(vs->matches, r.cursor);
        vs->hud->update(r.cursor + 1, static_cast<int>(vs->matches.size()));
        InvalidateRect(vs->hwnd, nullptr, FALSE);
    };

    // Kick off the async load (returns immediately; the worker reads + prewarms +
    // parses off the UI thread, adopted via wm_colorizer_parse_done). The funnel
    // paints the loading frame now and arranges the content repaint at adoption.
    begin_async_load(vs, FileToLoad);

    g_integration.attach(vs, ParentWin);

    return hwnd;
}

int __stdcall ListLoadNextW(HWND ParentWin, HWND PluginWin, wchar_t* FileToLoad, int ShowFlags) {
    WLX_TRACE(L"ListLoadNextW parent=%p plugin=%p file=%s flags=0x%X",
              ParentWin, PluginWin, FileToLoad ? FileToLoad : L"(null)", ShowFlags);
    // Same null/empty + size-cap gate as ListLoadW; LISTPLUGIN_ERROR makes TC
    // close this instance and pick another handler.
    if (!FileToLoad || !*FileToLoad || file_too_large(FileToLoad))
        return LISTPLUGIN_ERROR;
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

    begin_async_load(vs, FileToLoad);   // disk funnel arranges the repaint
    return LISTPLUGIN_OK;
}

void __stdcall ListCloseWindow(HWND ListWin) {
    WLX_TRACE(L"ListCloseWindow hwnd=%p", ListWin);
    auto it = g_views.find(ListWin);
    if (it != g_views.end()) {
        ColorViewState* vs = it->second;
        g_integration.detach(vs);          // must come before delete
        g_views.erase(it);                 // erase BEFORE delete: adopt's g_views.find
                                           // can never see a being-deleted entry
        vs->live->closed.store(true, std::memory_order_release);  // in-flight worker self-cancels
        // Fail a mid-parse worker's generation gate too: bumping the global
        // g_load_gen would change nothing the worker reads — it compares its job
        // generation against THIS live token's generation.
        vs->live->generation.fetch_add(1, std::memory_order_release);
        // Drain parse results already posted to this window: DestroyWindow
        // discards queued messages undelivered, which would leak each heap
        // ParseResultColor (raw_utf8 + grammar-pinning TreePtr). Residual
        // race: a worker that passed its closed-gate just before the store above
        // can still post one more result after this drain — that leaks at most
        // one payload, and the closed flag makes the window vanishingly small.
        MSG msg;
        while (PeekMessageW(&msg, ListWin, wm_colorizer_parse_done(),
                            wm_colorizer_parse_done(), PM_REMOVE))
            delete reinterpret_cast<ParseResultColor*>(msg.lParam);
        // Same drain for queued sweep results: a SweepResultColor* carries a heap
        // SpanTable that DestroyWindow would otherwise leak. Same residual race as
        // above (one post can slip past the closed-gate); the closed flag keeps the
        // window vanishingly small.
        while (PeekMessageW(&msg, ListWin, wm_colorizer_sweep_done(),
                            wm_colorizer_sweep_done(), PM_REMOVE))
            delete reinterpret_cast<SweepResultColor*>(msg.lParam);
        // Same drain for queued phase-1 text results: a TextResultColor* carries a
        // heap raw_utf8 copy that DestroyWindow would otherwise leak. Same residual
        // race as above (one post can slip past the closed-gate); the closed flag
        // keeps the window vanishingly small.
        while (PeekMessageW(&msg, ListWin, wm_colorizer_text_ready(),
                            wm_colorizer_text_ready(), PM_REMOVE))
            delete reinterpret_cast<TextResultColor*>(msg.lParam);
        SetWindowLongPtrW(ListWin, GWLP_USERDATA, 0);  // clear back-pointer before delete
        delete vs;                         // drops the UI's shared tree ref; the tree frees here OR on the sweep worker, whichever holds the last ref — both off the loader lock
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
    case lc_copy:
        // Same helper as the Ctrl+C / context-menu paths (guards layout + selection).
        return copy_selection_color(vs, vs->hwnd) ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;

    case lc_selectall:
        // Same helper as the Ctrl+A / context-menu paths (guards null/empty layout).
        if (!select_all_color(vs))
            return LISTPLUGIN_ERROR;
        InvalidateRect(vs->hwnd, nullptr, FALSE);
        return LISTPLUGIN_OK;

    case lc_newparams: {
        bool new_dark = (Parameter & lcp_darkmode)  != 0;
        bool new_wrap = (Parameter & lcp_wraptext) != 0;
        bool dark_changed = false;
        bool wrap_changed = false;

        if (new_dark != vs->dark_mode) {
            vs->dark_mode = new_dark;
            vs->renderer->set_dark_mode(new_dark);
            if (vs->hud) vs->hud->set_dark_mode(new_dark);
            apply_dark_mode(vs->hwnd, new_dark);
            dark_changed = true;
        }
        if (new_wrap != vs->wrap_text) {
            vs->wrap_text = new_wrap;
            wrap_changed = true;
        }
        if (dark_changed || wrap_changed) {
            if (vs->cached_raw_utf8.empty()) {
                // No source yet (loading/empty) — just repaint the frame with the
                // new dark palette (vs->dark_mode was updated above). NOTE: no
                // generation bump here, so an in-flight first load still adopts;
                // the adopt handler re-checks wrap against the result (B3.4).
                InvalidateRect(vs->hwnd, nullptr, FALSE);
            } else if (vs->state != wlx::runtime::host::LoadState::Ready) {
                // Mid-load: the flags above are recorded; the pending adoption
                // applies them (adopt reads the CURRENT dark mode, and a wrap
                // change is handled by the adopt-time res->wrap mismatch guard,
                // which drops the tree and takes the whole-doc fallback).
                // Bumping the generation here would orphan the in-flight parse
                // and leave the view stuck in Loading.
            } else if (!wrap_changed && vs->tree) {
                // Dark-only flip on the tree path: the tree is theme-independent
                // (colors resolve at highlight time), so skip the worker re-parse.
                // Rebuild the grid skeleton against the new palette with empty
                // colors; ensure_grid_window on the next WM_PAINT slides a fresh
                // window from the cached tree with the new dark flag.
                // Selection/matches stay valid (same block structure).

                // Cancel any in-flight sweep (it highlights with the OLD dark flag;
                // adopting its table would bake stale colors + free the tree). The
                // generation bump makes the worker vanish at its next chunk edge,
                // and makes any already-posted stale result fail should_adopt_result.
                vs->current_gen = ++wlx::runtime::host::g_load_gen;
                vs->live->generation.store(vs->current_gen, std::memory_order_release);
                vs->span_table.clear();   // pre-settle it's empty anyway; paranoia

                do_layout(vs, vs->cached_raw_utf8, /*colors=*/{});
                // Restart the sweep with the NEW dark flag so scroll coloring still
                // converges to a whole-file table (and frees the tree). begin_sweep
                // no-ops if the tree is null.
                begin_sweep(vs);
                InvalidateRect(vs->hwnd, nullptr, FALSE);
            } else {
                // Re-parse + re-color the IN-MEMORY source (no file re-read) off
                // the UI thread so the new palette / wrap mode takes effect.
                // begin_async_recolor honors any force-language override
                // (resolve_language), rebuilds the tree with the right wrap/lang
                // (dark is applied at highlight/fallback time), and resets
                // vs->tree synchronously so a Loading paint is a no-op against
                // the stale tree.
                begin_async_recolor(vs);
            }
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
    if (vs->state != LoadState::Ready || !vs->layout) return LISTPLUGIN_ERROR;

    SearchQuery q;
    q.needle       = SearchString;
    q.match_case   = (SearchParameter & lcs_matchcase)  != 0;
    q.whole_words  = (SearchParameter & lcs_wholewords) != 0;
    q.backwards    = (SearchParameter & lcs_backwards)  != 0;
    const bool findfirst = (SearchParameter & lcs_findfirst) != 0;

    ensure_search_index(vs);
    auto r = search_step(*vs, q, findfirst);
    if (!r.has_match) {
        if (vs->renderer) vs->renderer->set_search_matches({}, -1);
        if (vs->hud) vs->hud->update(0, 0);
        InvalidateRect(vs->hwnd, nullptr, FALSE);
        return LISTPLUGIN_ERROR;
    }
    scroll_to_match(vs, vs->matches[r.cursor]);
    if (vs->renderer) vs->renderer->set_search_matches(vs->matches, r.cursor);
    if (vs->hud) vs->hud->update(r.cursor + 1, static_cast<int>(vs->matches.size()));
    InvalidateRect(vs->hwnd, nullptr, FALSE);
    return LISTPLUGIN_OK;
}

void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps) {
    WLX_TRACE(L"ListSetDefaultParams dps=%p size=%d", dps, dps ? dps->size : -1);
    // DefaultIniName is deliberately unused: this plugin reads its own TOML
    // config next to the DLL (ensure_theme), never TC's ini.
}

} // extern "C"
