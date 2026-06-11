#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tools/screenshot/colorizer_pipeline.h"

#include <toml++/toml.hpp>

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/host/module_path.h"
#include "runtime/io/file_service.h"
#include "runtime/render/render_engine.h"
#include "runtime/theme/theme_service.h"
#include "runtime/util/string_util.h"
#include "core_dll/colorizer/colorizer.h"
#include "core_dll/registry/core_config.h"
#include "plugin_colorizer/layout/colorizer_layout.h"
#include "plugin_colorizer/layout/grid_window.h"
#include "plugin_colorizer/layout/grid_geometry.h"
#include "wlx_core/abi.h"
#include "wlx_core/abi_spans_to_result.h"
#include "plugin_colorizer/colorize/span_table.h"
#include "plugin_colorizer/colorize/sweep_chunk.h"
#include "plugin_colorizer/language/path_to_language.h"
#include "plugin_colorizer/language/routing.h"
#include "tools/screenshot/token_json_writer.h"
#include "tools/screenshot/working_set_sample.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

using wlx::core::colorizer::Colorizer;
using wlx::core::colorizer::ColorizeResult;
using wlx::core::colorizer::ColorizeTimings;
using wlx::plugin_colorizer::layout::ColorizerDisplayConfig;
using wlx::plugin_colorizer::layout::LayoutTimings;
using wlx::plugin_colorizer::layout::CppGrammar;
using wlx::plugin_colorizer::layout::layout_source;
using wlx::plugin_colorizer::layout::layout_grid_skeleton;
using wlx::plugin_colorizer::layout::slide_grid_window;
using wlx::plugin_colorizer::layout::grid_window_lines;
using wlx::plugin_colorizer::layout::MaterializeCtx;
using wlx::plugin_colorizer::layout::GridGeometry;
using wlx::plugin_colorizer::language::apply_cpp_variant;
using wlx::plugin_colorizer::language::ext_to_language;
using wlx::plugin_colorizer::language::filename_to_language;
using wlx::core::registry::CoreConfig;
using wlx::runtime::host::get_module_dir;
using wlx::runtime::io::FileService;
using wlx::runtime::render::RenderEngine;
using wlx::runtime::theme::ThemeService;

namespace wlx::tools::screenshot {

namespace {

// Resolve the grammar id through the plugin's own routing (extension table →
// filename special-cases in path_to_language.h) so the tool validates the
// path users actually see (.c → cpp grammar, Dockerfile, CMakeLists.txt, …).
// --lang wins; paths neither table knows fall back to the lowercased
// extension as a grammar id (covers grammars whose id == ext that the
// table omits, e.g. sample.git_rebase). The unreal-cpp swap is handled later
// via apply_cpp_variant — routing only yields base-language IDs.
std::string infer_language(const std::wstring& path,
                           const std::wstring& override_lang) {
    if (!override_lang.empty()) return wlx::runtime::util::wstring_to_utf8(override_lang);
    std::string lang = ext_to_language(path);
    if (lang.empty()) lang = filename_to_language(path);
    if (!lang.empty()) return lang;
    std::wstring ext = fs::path(path).extension().wstring();
    if (!ext.empty() && ext.front() == L'.') ext.erase(0, 1);
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    return wlx::runtime::util::wstring_to_utf8(ext);  // fall back to the extension itself
}

CppGrammar parse_cpp_variant(const std::wstring& s) {
    if (s == L"unreal") return CppGrammar::Unreal;
    return CppGrammar::Standard;
}

// Build sibling output path: <input_dir>/<filename><suffix>.
// `filename` keeps the extension so sample.cpp.png is distinct from sample.cs.png.
std::wstring sibling_path(const std::wstring& input_path,
                          const std::wstring& suffix) {
    fs::path input(input_path);
    std::wstring out_name = input.filename().wstring() + suffix;
    return input.parent_path().empty()
        ? fs::path(out_name).wstring()
        : (input.parent_path() / out_name).wstring();
}

// FNV-1a hex over a canonical line-format serialization of the inputs that
// influence colorize+layout output. The hash isn't cryptographic — its only
// job is to flip when any of these inputs change, so the comparator can
// distinguish "config drift, regenerate goldens" from "real semantic
// regression". The schema doesn't pin the algorithm; if we later swap to
// SHA1, regenerating goldens once is the only migration cost.
std::string fnv1a_hex(const std::string& bytes) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string(buf);
}

std::string compute_config_hash(const std::string& theme_name,
                                bool dark_mode,
                                const ColorizerDisplayConfig& d) {
    // If a new ColorizerDisplayConfig field lands, this assert fires —
    // forcing an update to the canonical serialization below. Without
    // the guard, two configs differing only in the new field would
    // silently hash to the same value.
    static_assert(sizeof(ColorizerDisplayConfig) == 24,
                  "ColorizerDisplayConfig layout changed; "
                  "update compute_config_hash to include the new field(s).");

    std::ostringstream s;
    s << "theme="            << theme_name << '\n';
    s << "dark="             << (dark_mode ? "1" : "0") << '\n';
    s << "line_numbers="     << (d.line_numbers ? "1" : "0") << '\n';
    s << "word_wrap="        << (d.word_wrap ? "1" : "0") << '\n';
    s << "tab_width="        << d.tab_width << '\n';
    s << "line_height="      << d.line_height_factor << '\n';
    s << "show_whitespace="  << static_cast<int>(d.show_whitespace) << '\n';
    s << "indent_guides="    << (d.show_indent_guides ? "1" : "0") << '\n';
    s << "highlight_trail="  << (d.highlight_trailing ? "1" : "0") << '\n';
    s << "cpp_grammar="      << static_cast<int>(d.cpp_grammar) << '\n';
    return fnv1a_hex(s.str());
}

// If the user didn't pass --config, default to the colorizer TOML rather than
// the markdown one (which is Options' built-in default for legacy reasons).
// Otherwise honor whatever --config was specified, even if it equals the
// markdown default by coincidence.
std::wstring resolve_config_path(const Options& opts) {
    if (!opts.config_path_explicit) {
        return L"config/wlx-listerine-colorizer.toml";
    }
    return opts.config_path;
}

ColorizerDisplayConfig load_display_config(const std::wstring& path,
                                           const ThemeService& theme) {
    ColorizerDisplayConfig d;  // defaults
    // Mirror the plugin's do_layout: line height comes from the theme's
    // spacing config, not ColorizerDisplayConfig's built-in default. The
    // flat line_height key below can still override it.
    d.line_height_factor = theme.spacing().line_height_factor;
    if (path.empty()) return d;
    std::string narrow_path = wlx::runtime::util::wstring_to_utf8(path);
    try {
        auto tbl = toml::parse_file(narrow_path);
        if (auto v = tbl["line_numbers"].value<bool>())        d.line_numbers = *v;
        if (auto v = tbl["word_wrap"].value<bool>())           d.word_wrap = *v;
        if (auto v = tbl["tab_width"].value<int64_t>())        d.tab_width = static_cast<int>(*v);
        if (auto v = tbl["line_height"].value<double>())       d.line_height_factor = static_cast<float>(*v);
        if (auto v = tbl["show_indent_guides"].value<bool>())  d.show_indent_guides = *v;
        if (auto v = tbl["highlight_trailing"].value<bool>())  d.highlight_trailing = *v;
        if (auto v = tbl["show_whitespace"].value<std::string>()) {
            using wlx::plugin_colorizer::layout::ShowWhitespace;
            if      (*v == "all")      d.show_whitespace = ShowWhitespace::All;
            else if (*v == "boundary") d.show_whitespace = ShowWhitespace::Boundary;
            else if (*v == "none")     d.show_whitespace = ShowWhitespace::None;
            else {
                std::fprintf(stderr,
                    "Unknown show_whitespace value \"%s\" in %ls — using default\n",
                    v->c_str(), path.c_str());
            }
        }
    } catch (const toml::parse_error& e) {
        std::fprintf(stderr, "Failed to parse --display-config %ls: %s\n",
                     path.c_str(), e.description().data());
        // Return defaults rather than failing the whole pipeline — matches the
        // idea that --display-config is an "override hint", not a hard config.
    }
    return d;
}

// ---------- memory stats ----------

// Print the memory footprint of opening this file: peak working set (the
// process high-water mark, path-independent) and the working-set delta vs the
// pre-work baseline. Shared by the cached-tree and eager bench blocks. Takes a
// pre-taken sample so callers can measure before any bench-only re-runs that
// would inflate the numbers.
void print_bench_memory(size_t source_bytes, size_t mem_before, WorkingSetSample ws) {
    double peak_mb = static_cast<double>(ws.peak) / (1024.0 * 1024.0);
    double delta_mb = static_cast<double>(
        static_cast<ptrdiff_t>(ws.current) -
        static_cast<ptrdiff_t>(mem_before)) / (1024.0 * 1024.0);
    std::fprintf(stderr, "  source size    %8zu bytes UTF-8\n", source_bytes);
    std::fprintf(stderr, "  peak workingset%8.1f MB\n", peak_mb);
    std::fprintf(stderr, "  process delta  %+8.1f MB  (working set vs baseline)\n", delta_mb);
}

// Shared PNG save epilogue for the cached-tree and eager render paths:
// ensure the output directory exists and write the bitmap. Returns `out`
// on success, empty on failure.
std::wstring save_png(RenderEngine& renderer, IWICImagingFactory* wic,
                      const std::wstring& out) {
    std::error_code ec;
    fs::create_directories(fs::path(out).parent_path(), ec);
    if (ec) {
        std::fprintf(stderr, "Cannot create output directory: %s\n",
                     ec.message().c_str());
        return {};
    }
    HRESULT hr = renderer.save_to_png(wic, out.c_str());
    if (FAILED(hr)) {
        std::fprintf(stderr, "Failed to save PNG: 0x%08lx\n", hr);
        return {};
    }
    return out;
}

}  // namespace

std::wstring run_colorizer_pipeline(const Options& opts) {
    // Baseline working set before any work, so --bench can report the delta
    // attributable to opening this file (factories + grammar + tree + layout).
    size_t mem_before = opts.bench ? sample_working_set().current : 0;

    // ----- Factories -----
    ComPtr<ID2D1Factory> d2d_factory;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   d2d_factory.GetAddressOf());
    if (FAILED(hr)) {
        std::fprintf(stderr, "D2D factory failed: 0x%08lx\n", hr);
        return {};
    }

    ComPtr<IDWriteFactory> dwrite_factory;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(dwrite_factory.GetAddressOf()));
    if (FAILED(hr)) {
        std::fprintf(stderr, "DWrite factory failed: 0x%08lx\n", hr);
        return {};
    }

    ComPtr<IWICImagingFactory> wic_factory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(wic_factory.GetAddressOf()));
    if (FAILED(hr)) {
        std::fprintf(stderr, "WIC factory failed: 0x%08lx\n", hr);
        return {};
    }

    // ----- Theme -----
    ThemeService theme;
    theme.load(resolve_config_path(opts));

    // ----- Colorizer -----
    // Mirror CoreRegistry's setup: grammars/ and themes/ are resolved relative
    // to the exe (POST_BUILD steps mirror them next to the binary), and the
    // theme names come from wlx-listerine-core.toml in the same directory —
    // so the eager path and the --cached-tree/core path resolve the same
    // colors regardless of CWD.
    const std::wstring exe_dir = get_module_dir(nullptr);
    const CoreConfig core_cfg = CoreConfig::load(exe_dir);
    Colorizer colorizer(exe_dir + L"grammars", exe_dir + L"themes",
                        core_cfg.theme, core_cfg.theme_light,
                        core_cfg.cap, core_cfg.ttl_minutes);

    // ----- Read source -----
    auto _t0 = std::chrono::steady_clock::now();
    FileService file_service;
    auto content = file_service.read(opts.input_path.c_str());
    if (!content) {
        std::fprintf(stderr, "Failed to read: %ls\n", opts.input_path.c_str());
        return {};
    }
    auto _tread = std::chrono::steady_clock::now();

    // ----- Resolve language + cpp variant -----
    std::string base_lang = infer_language(opts.input_path, opts.lang);
    if (base_lang.empty()) {
        std::fprintf(stderr,
            "Could not infer language for %ls (use --lang)\n",
            opts.input_path.c_str());
        return {};
    }
    CppGrammar variant = parse_cpp_variant(opts.cpp_grammar);
    std::string lang = apply_cpp_variant(base_lang, variant, &colorizer);
    if (!colorizer.supports(lang)) {
        std::fprintf(stderr,
            "No grammar registered for \"%s\" — check grammars/%s/\n",
            lang.c_str(), lang.c_str());
        return {};
    }

    // ----- Build display config (used by both paint and dump-tokens paths) -----
    // Load TOML overrides first, then force cpp_grammar from --cpp-grammar
    // (the flag wins over any value the TOML might set).
    ColorizerDisplayConfig display = load_display_config(opts.display_config, theme);
    display.cpp_grammar = variant;

    // ----- Colorize (eager whole-doc) -----
    // Deferred in --cached-tree mode: that path parses + highlights via the
    // core singleton and never reads this result, so an up-front whole-doc
    // colorize would only contaminate the bench's parse/memory numbers and
    // waste a full parse. (--dump-tokens needs it and wins over --cached-tree,
    // matching the branch order below.) If the cached-tree path falls back,
    // the eager layout section below runs the deferred colorize first.
    ColorizeTimings ctimings;
    ColorizeResult colors;
    auto _tcolor0 = _tread;
    auto _tcolor  = _tread;
    bool colorized = false;
    if (!opts.cached_tree || opts.dump_tokens) {
        _tcolor0 = std::chrono::steady_clock::now();
        colors = colorizer.colorize(content->raw_utf8, lang, opts.dark, &ctimings);
        _tcolor = std::chrono::steady_clock::now();
        colorized = true;
    }

    // ----- --dump-tokens branch -----
    if (opts.dump_tokens) {
        // Placeholder: HelixTheme has no name() accessor today, and ThemeService
        // doesn't expose the loaded theme's name either. A user setting
        // `theme = "monokai"` in wlx-listerine-colorizer.toml would currently
        // get "default_dark"/"default_light" in the JSON header. Note this
        // doesn't break golden detection — a theme swap changes resolved
        // colors, so per-token diffs fire even though config_hash is stable.
        // TODO: when HelixTheme::name() lands, replace this literal.
        const std::string theme_name_str = opts.dark ? "default_dark" : "default_light";
        TokenJsonOptions json_opts;
        // wstring_to_utf8, not path::string(): the latter narrows through the
        // ANSI codepage and corrupts non-ASCII filenames in the JSON header.
        json_opts.source_name = wlx::runtime::util::wstring_to_utf8(
            fs::path(opts.input_path).filename().wstring());
        json_opts.language    = lang;
        json_opts.theme_name  = theme_name_str;
        json_opts.config_hash = compute_config_hash(theme_name_str, opts.dark, display);

        std::string json = TokenJsonWriter::write(colors, content->raw_utf8, json_opts);

        const std::wstring suffix = opts.dark
            ? L"_tokens.dark.json"
            : L"_tokens.light.json";
        std::wstring out = sibling_path(opts.input_path, suffix);

        std::error_code ec;
        fs::create_directories(fs::path(out).parent_path(), ec);
        if (ec) {
            std::fprintf(stderr, "Cannot create output directory: %s\n",
                         ec.message().c_str());
            return {};
        }

        // std::ios::binary is load-bearing for determinism: without it, MSVC's
        // text-mode ofstream translates '\n' → "\r\n" on write, which would
        // produce non-deterministic byte sequences across platforms and break
        // golden-file comparison via SHA256 / structural diff.
        std::ofstream f(out, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "Failed to open %ls for writing\n", out.c_str());
            return {};
        }
        f.write(json.data(), static_cast<std::streamsize>(json.size()));
        if (!f) {
            std::fprintf(stderr, "Failed to write %ls\n", out.c_str());
            return {};
        }
        return out;
    }

    // ----- --cached-tree path (parse once + viewport highlight_range) -----
    // Mirrors the host's grid flow (layout_grid_skeleton + slide_grid_window).
    // Wrap geometry is handled by the grid (GridGeometry) — word_wrap no longer
    // causes a fallback here. Falls back to the eager whole-doc path only when:
    //   - core handle unavailable (ABI mismatch)
    //   - language unsupported in the core
    //   - wlx_core_parse returns null
    if (opts.cached_tree) {
        WlxCore* core = wlx_core::acquire_compatible();
        bool use_cached = core != nullptr &&
                          wlx_core_supports(core, lang.c_str()) == 1;
        if (!use_cached) {
            std::fprintf(stderr,
                "[--cached-tree] fallback: %s — using whole-doc colorize\n",
                !core ? "core unavailable" : "language unsupported in core");
        } else {
            // Parse
            auto _tparse0 = std::chrono::steady_clock::now();
            WlxTree* raw_tree = wlx_core_parse(
                core, content->raw_utf8.c_str(),
                static_cast<uint32_t>(content->raw_utf8.size()),
                lang.c_str());
            auto _tparse1 = std::chrono::steady_clock::now();

            if (!raw_tree) {
                std::fprintf(stderr,
                    "[--cached-tree] fallback: wlx_core_parse failed — using whole-doc colorize\n");
                // fall through to eager path below
            } else {
                wlx_core::TreePtr tree(raw_tree, wlx_core::TreeDeleter{core});

                // Grid skeleton: NO per-line blocks, just line_byte_starts +
                // geometry + the build context slide_grid_window needs. This is
                // the host's no-wrap path (do_layout's grid branch) — the old
                // layout_source skeleton built every block up front; the grid
                // skeleton builds none, so the 250k-block sqlite layout never
                // materializes during parse/sweep (the M2 memory win).
                std::vector<int> line_byte_starts;
                std::shared_ptr<MaterializeCtx> grid_ctx;
                GridGeometry grid_geo;
                auto layout_ct = layout_grid_skeleton(dwrite_factory.Get(),
                                                      content->raw_utf8,
                                                      theme,
                                                      opts.dark,
                                                      static_cast<float>(opts.width),
                                                      display,
                                                      &line_byte_starts,
                                                      &grid_ctx,
                                                      &grid_geo);
                auto _tlayout_ct = std::chrono::steady_clock::now();

                // Bitmap height: --full sizes to the whole grid, else the fixed
                // viewport. viewport_h_ct is also the post-scroll step + overscan.
                const int bmp_height_ct = opts.full
                    ? std::max(1, static_cast<int>(std::ceil(layout_ct.total_height)))
                    : opts.height;
                // Mutable scroll so the post-scroll loop can slide the viewport.
                float scroll_y_ct_mut = opts.full ? 0.0f : opts.scroll;
                const float viewport_h_ct = static_cast<float>(bmp_height_ct);

                // First-paint coloring through the grid (host's WM_PAINT flow):
                // pick the visible line window, then slide_grid_window builds +
                // colors those lines via the live tree. colors_for runs
                // synchronously inside the slide, so `tree` is safe to capture.
                auto colors_for_tree = [&](uint32_t lo, uint32_t hi) -> ColorizeResult {
                    WlxColorSpan* cs = nullptr; uint32_t cc = 0;
                    if (wlx_core_highlight_range(core, tree.get(), opts.dark ? 1 : 0,
                                                 lo, hi, &cs, &cc) == 0)
                        return wlx_core::abi_spans_to_result(cs, cc);
                    return {};
                };
                auto _thigh0 = std::chrono::steady_clock::now();
                auto [first, last] = grid_window_lines(grid_geo, scroll_y_ct_mut,
                                                       viewport_h_ct,
                                                       /*overscan=*/viewport_h_ct);
                slide_grid_window(layout_ct, grid_geo, *grid_ctx, content->raw_utf8,
                                  line_byte_starts, first, last, colors_for_tree);
                auto _thigh1 = std::chrono::steady_clock::now();

                // Byte span of the first window, for the bench annotation only.
                const uint32_t vlo = (last < first)
                    ? 0u
                    : static_cast<uint32_t>(line_byte_starts[static_cast<size_t>(first)]);
                const uint32_t vhi = (last < first)
                    ? 0u
                    : ((last + 1 < static_cast<int>(line_byte_starts.size()))
                           ? static_cast<uint32_t>(line_byte_starts[static_cast<size_t>(last + 1)])
                           : static_cast<uint32_t>(content->raw_utf8.size()));

                // ----- Render (cached-tree path) -----
                RenderEngine renderer_ct(d2d_factory.Get(), dwrite_factory.Get(), theme, opts.dark);
                hr = renderer_ct.create_bitmap_resources(wic_factory.Get(),
                                                         opts.width, bmp_height_ct);
                if (FAILED(hr)) {
                    std::fprintf(stderr, "Bitmap target failed: 0x%08lx\n", hr);
                    return {};
                }
                hr = renderer_ct.paint(layout_ct, scroll_y_ct_mut);
                if (FAILED(hr)) {
                    std::fprintf(stderr, "Paint failed (EndDraw): 0x%08lx\n", hr);
                    return {};
                }
                auto _tpaint_ct = std::chrono::steady_clock::now();

                // Timing helper shared by bench print and sweep timing.
                auto ms = [](auto a, auto b) {
                    return std::chrono::duration<double, std::milli>(b - a).count();
                };

                // ---- sweep: chunk-highlight the whole file into the span table,
                // then free the tree (memory M1). The "process delta" bench sample
                // is taken AFTER this settle point so the freed tree shows up.
                // Gated: run only when --bench (settle sampling) or --scroll-screens
                // (which needs the table to recolor post-scroll viewports).
                wlx::plugin_colorizer::colorize::SpanTable sweep_table;
                auto _tsweep0 = std::chrono::steady_clock::now();
                if (opts.bench || opts.scroll_screens > 0) {
                    const auto fsize = static_cast<uint32_t>(content->raw_utf8.size());
                    uint32_t chunk = wlx::plugin_colorizer::colorize::kSweepFirstChunkBytes;
                    while (!sweep_table.complete(fsize)) {
                        const uint32_t lo = sweep_table.swept_hi();
                        const uint32_t hi = std::min(fsize, lo + chunk);
                        WlxColorSpan* cs = nullptr;
                        uint32_t cc = 0;
                        auto c0 = std::chrono::steady_clock::now();
                        if (wlx_core_highlight_range(core, tree.get(), opts.dark ? 1 : 0,
                                                     lo, hi, &cs, &cc) != 0)
                            break;   // sweep failure: keep the tree (spec fallback)
                        sweep_table.append_chunk(wlx_core::abi_spans_to_result(cs, cc), lo, hi);
                        chunk = wlx::plugin_colorizer::colorize::next_chunk_bytes(
                            hi - lo, ms(c0, std::chrono::steady_clock::now()));
                    }
                    if (sweep_table.complete(fsize)) {
                        sweep_table.seal();  // drop vector growth slack (~MBs on big files)
                        tree.reset();   // free tree + unpin grammar — the settle point
                    }
                }
                auto _tsweep1 = std::chrono::steady_clock::now();

                // ---- post-scroll pass (--scroll-screens N): slide the viewport
                // down N screens, recoloring + repainting per step, so the final
                // working-set sample measures post-scroll retention. Gated on a
                // COMPLETE sweep table (host's table-ready discipline): an aborted
                // sweep must not silently render uncolored post-scroll viewports.
                if (opts.scroll_screens > 0 && !opts.full &&
                    sweep_table.complete(
                        static_cast<uint32_t>(content->raw_utf8.size()))) {
                    for (int step = 0; step < opts.scroll_screens; ++step) {
                        scroll_y_ct_mut += viewport_h_ct;
                        auto [f2, l2] = grid_window_lines(grid_geo, scroll_y_ct_mut,
                                                          viewport_h_ct, viewport_h_ct);
                        slide_grid_window(layout_ct, grid_geo, *grid_ctx,
                                          content->raw_utf8, line_byte_starts,
                                          f2, l2,
                                          [&](uint32_t lo, uint32_t hi) {
                                              return sweep_table.slice(lo, hi);
                                          });
                        renderer_ct.paint(layout_ct, scroll_y_ct_mut);
                    }
                }

                if (opts.bench) {
                    int lines = 1;
                    for (char c : content->raw_utf8) if (c == '\n') ++lines;

                    std::fprintf(stderr,
                        "Colorizer timing --cached-tree (%d lines, lang=%s):\n",
                        lines, lang.c_str());
                    std::fprintf(stderr, "  file read        %8.2f ms\n",
                        ms(_t0, _tread));
                    std::fprintf(stderr, "  parse (cold)     %8.2f ms  (wlx_core_parse)\n",
                        ms(_tparse0, _tparse1));
                    // Grid skeleton: byte-scan + arithmetic line_tops only, NO
                    // per-line blocks (those build lazily in the slide below), so
                    // the old line-split / span-index / build-blocks / line-index
                    // sub-rows are gone — layout_grid_skeleton takes no timings.
                    std::fprintf(stderr, "  layout (skel)    %8.2f ms  (grid skeleton, no blocks)\n",
                        ms(_tparse1, _tlayout_ct));
                    std::fprintf(stderr,
                        "  highlight range  %8.2f ms  (slide_grid_window first window, "
                        "bytes %u..%u)\n",
                        ms(_thigh0, _thigh1), vlo, vhi);
                    std::fprintf(stderr, "  paint            %8.2f ms\n",
                        ms(_thigh1, _tpaint_ct));   // from end of highlight, not layout
                    const char* sweep_aborted = sweep_table.complete(
                        static_cast<uint32_t>(content->raw_utf8.size()))
                        ? "" : "  (ABORTED)";
                    std::fprintf(stderr, "  sweep      %6.2f ms  (%zu spans, %.1f MB table)%s\n",
                        ms(_tsweep0, _tsweep1), sweep_table.size(),
                        sweep_table.approx_bytes() / (1024.0 * 1024.0), sweep_aborted);
                    // Sum the measured phases. NOT ms(_t0, _tpaint_ct): that wall-
                    // clock also includes the one-time core-singleton init / grammar
                    // scan triggered by acquire_compatible() in this path (a tool
                    // artifact — the real plugin shares one already-warm core).
                    // Sweep is excluded from hot total: it is a bench settle step,
                    // not part of the open-file critical path.
                    double hot_ct = ms(_t0, _tread) + ms(_tparse0, _tparse1)
                                  + ms(_tparse1, _tlayout_ct) + ms(_thigh0, _thigh1)
                                  + ms(_thigh1, _tpaint_ct);
                    std::fprintf(stderr,
                        "  hot total        %8.2f ms  (read+parse+layout+highlight+paint)\n",
                        hot_ct);
                    print_bench_memory(content->raw_utf8.size(), mem_before,
                                       sample_working_set());
                }

                // ----- Save (cached-tree path) -----
                return save_png(renderer_ct, wic_factory.Get(),
                                sibling_path(opts.input_path,
                                             opts.dark ? L"_dark.png" : L".png"));
                // tree freed here via TreePtr destructor (if sweep didn't free it)
            }
        }
        // Fallback: parse failed — fall through to eager whole-doc below.
    }

    // ----- Layout (eager whole-doc) -----

    // Reached in --cached-tree mode only via fallback (core unavailable or parse
    // failure) — run the whole-doc colorize deferred above.
    if (!colorized) {
        _tcolor0 = std::chrono::steady_clock::now();
        colors = colorizer.colorize(content->raw_utf8, lang, opts.dark, &ctimings);
        _tcolor = std::chrono::steady_clock::now();
    }

    LayoutTimings ltimings;
    auto layout = layout_source(dwrite_factory.Get(),
                                content->raw_utf8,    // utf-8 source
                                colors,
                                theme,
                                opts.dark,
                                static_cast<float>(opts.width),
                                display,
                                &ltimings);
    auto _tlayout = std::chrono::steady_clock::now();

    // ----- Bitmap dimensions -----
    int bmp_width = opts.width;
    int bmp_height = opts.full
        ? std::max(1, static_cast<int>(std::ceil(layout.total_height)))
        : opts.height;
    float scroll_y = opts.full ? 0.0f : opts.scroll;

    // ----- Render -----
    RenderEngine renderer(d2d_factory.Get(), dwrite_factory.Get(), theme, opts.dark);
    hr = renderer.create_bitmap_resources(wic_factory.Get(), bmp_width, bmp_height);
    if (FAILED(hr)) {
        std::fprintf(stderr, "Bitmap target failed: 0x%08lx\n", hr);
        return {};
    }
    hr = renderer.paint(layout, scroll_y);
    if (FAILED(hr)) {
        std::fprintf(stderr, "Paint failed (EndDraw): 0x%08lx\n", hr);
        return {};
    }
    auto _tpaint = std::chrono::steady_clock::now();

    if (opts.bench) {
        auto ms = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        int lines = 1;
        for (char c : content->raw_utf8) if (c == '\n') ++lines;

        // Sample memory BEFORE the warm re-run below: the bench-only second
        // whole-doc colorize would otherwise inflate the peak/delta numbers.
        WorkingSetSample ws = sample_working_set();

        // Warm colorize: the grammar DLL + compiled query are now cached
        // process-wide, so this re-run measures the steady-state cost every
        // 2nd+ file of this language pays (no LoadLibrary, no ts_query_new).
        // Result discarded — only the timings matter.
        ColorizeTimings cwarm;
        auto _w0 = std::chrono::steady_clock::now();
        (void) colorizer.colorize(content->raw_utf8, lang, opts.dark, &cwarm);
        auto _w1 = std::chrono::steady_clock::now();

        std::fprintf(stderr, "Colorizer timing (%d lines, lang=%s):\n", lines, lang.c_str());
        std::fprintf(stderr, "  file read        %8.2f ms\n", ms(_t0, _tread));
        std::fprintf(stderr, "  colorize (cold)  %8.2f ms\n", ms(_tcolor0, _tcolor));
        std::fprintf(stderr, "    grammar load   %8.2f ms  (LoadLibrary, cold only)\n", ctimings.grammar_load_ms);
        std::fprintf(stderr, "    query compile  %8.2f ms  (ts_query_new, cold only)\n", ctimings.query_compile_ms);
        std::fprintf(stderr, "    parse          %8.2f ms  (tree-sitter)\n", ctimings.parse_ms);
        std::fprintf(stderr, "    highlight      %8.2f ms  (query exec)\n", ctimings.highlight_ms);
        std::fprintf(stderr, "  colorize (warm)  %8.2f ms  (grammar+query cached)\n", ms(_w0, _w1));
        std::fprintf(stderr, "    parse          %8.2f ms\n", cwarm.parse_ms);
        std::fprintf(stderr, "    highlight      %8.2f ms\n", cwarm.highlight_ms);
        std::fprintf(stderr, "  layout           %8.2f ms  (per-line IDWriteTextLayout)\n", ms(_tcolor, _tlayout));
        std::fprintf(stderr, "    line split     %8.2f ms\n", ltimings.line_split_ms);
        std::fprintf(stderr, "    span index     %8.2f ms\n", ltimings.span_index_ms);
        std::fprintf(stderr, "    build blocks   %8.2f ms  (CreateTextLayout + decorations)\n", ltimings.build_blocks_ms);
        std::fprintf(stderr, "    line index     %8.2f ms\n", ltimings.line_index_ms);
        std::fprintf(stderr, "  paint            %8.2f ms\n", ms(_tlayout, _tpaint));
        std::fprintf(stderr, "  hot total        %8.2f ms  (read+colorize+layout+paint)\n", ms(_t0, _tpaint));
        std::fprintf(stderr, "  per line         %8.3f ms\n", ms(_tcolor, _tlayout) / std::max(1, lines));
        print_bench_memory(content->raw_utf8.size(), mem_before, ws);
    }

    // ----- Save -----
    return save_png(renderer, wic_factory.Get(),
                    sibling_path(opts.input_path, opts.dark ? L"_dark.png" : L".png"));
}

}  // namespace wlx::tools::screenshot
