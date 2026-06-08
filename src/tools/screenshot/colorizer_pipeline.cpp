#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tools/screenshot/colorizer_pipeline.h"

#include <toml++/toml.hpp>

#include <windows.h>
#include <psapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/io/file_service.h"
#include "runtime/render/render_engine.h"
#include "runtime/theme/theme_service.h"
#include "runtime/util/string_util.h"
#include "core_dll/colorizer/colorizer.h"
#include "plugin_colorizer/layout/colorizer_layout.h"
#include "wlx_core/abi.h"
#include "plugin_colorizer/language/routing.h"
#include "tools/screenshot/token_json_writer.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

using wlx::core::colorizer::Colorizer;
using wlx::core::colorizer::ColorizeResult;
using wlx::core::colorizer::ColorizeTimings;
using wlx::plugin_colorizer::layout::ColorizerDisplayConfig;
using wlx::plugin_colorizer::layout::LayoutTimings;
using wlx::plugin_colorizer::layout::CppGrammar;
using wlx::plugin_colorizer::layout::layout_source;
using wlx::plugin_colorizer::layout::apply_spans_to_range;
using wlx::plugin_colorizer::language::apply_cpp_variant;
using wlx::runtime::io::FileService;
using wlx::runtime::render::RenderEngine;
using wlx::runtime::theme::ThemeService;

namespace wlx::tools::screenshot {

namespace {

// Map common file extensions to tree-sitter grammar IDs. Extensions not in
// the table fall back to the extension string itself (covers grammars whose
// id == ext like "lua"). The unreal-cpp swap is handled later via
// apply_cpp_variant — this map only covers base-language IDs.
//
// Extension → tree-sitter grammar id. Entries where grammar_id == extension
// (e.g. lua, cmake, toml, json, yaml, html, css, php, java, go, rust...)
// do not need a row here — the fallback at the bottom returns the extension
// string itself. Only entries where the grammar id differs from the
// extension (e.g. py → python, js → javascript, sh → bash) belong here.
std::string infer_language(const std::wstring& path,
                           const std::wstring& override_lang) {
    if (!override_lang.empty()) return wlx::runtime::util::wstring_to_utf8(override_lang);
    fs::path p(path);
    std::wstring ext = p.extension().wstring();
    if (!ext.empty() && ext.front() == L'.') ext.erase(0, 1);
    static const std::pair<std::wstring, std::string> kMap[] = {
        {L"c",   "c"},   {L"cpp", "cpp"},  {L"cc",  "cpp"},     {L"cxx", "cpp"},
        {L"h",   "cpp"}, {L"hpp", "cpp"},  {L"hxx", "cpp"},
        {L"cs",  "c-sharp"},
        {L"go",  "go"},
        {L"py",  "python"}, {L"pyi", "python"},
        {L"rs",  "rust"},
        {L"js",  "javascript"}, {L"mjs", "javascript"}, {L"cjs", "javascript"}, {L"jsx", "javascript"},
        {L"ts",  "typescript"}, {L"tsx", "typescript"}, {L"mts", "typescript"},
        {L"json","json"}, {L"jsonc","json"},
        {L"toml","toml"},
        {L"yaml","yaml"}, {L"yml", "yaml"},
        {L"sh",  "bash"}, {L"bash","bash"}, {L"zsh", "bash"},
        {L"ps1", "powershell"}, {L"psm1","powershell"}, {L"psd1","powershell"},
        {L"lua", "lua"},
        {L"html","html"}, {L"htm", "html"},
        {L"css", "css"},
        {L"php", "php"},
        {L"java","java"},
        {L"vim", "vim"}, {L"vimrc","vim"},
        {L"cmake","cmake"},
        {L"dockerfile","dockerfile"},
        {L"gitconfig","git-config"},
        {L"gitignore","gitignore"},
        {L"gitattributes","gitattributes"},
    };
    for (const auto& [k, v] : kMap) if (k == ext) return v;
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

// Convert an ABI WlxColorSpan array into a ColorizeResult. Frees the spans via
// wlx_core_free_spans. Returns an empty result if spans is null or count is zero.
static ColorizeResult abi_spans_to_result(WlxColorSpan* spans, uint32_t count) {
    using wlx::core::colorizer::ColorSpan;
    ColorizeResult out;
    if (!spans || count == 0) {
        if (spans) wlx_core_free_spans(spans);
        return out;
    }
    out.spans.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& s = spans[i];
        ColorSpan cs;
        cs.start    = s.start;  cs.length   = s.length;
        cs.color    = s.color;  cs.bg_color = s.bg_color;
        cs.has_bg   = s.has_bg != 0; cs.modifiers = s.modifiers;
        out.spans.push_back(cs);
    }
    wlx_core_free_spans(spans);
    return out;
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

ColorizerDisplayConfig load_display_config(const std::wstring& path) {
    ColorizerDisplayConfig d;  // defaults
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

size_t process_working_set() {
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return pmc.WorkingSetSize;
}

size_t process_peak_working_set() {
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return pmc.PeakWorkingSetSize;
}

// Print the memory footprint of opening this file: peak working set (the
// process high-water mark, path-independent) and the working-set delta vs the
// pre-work baseline. Shared by the cached-tree and eager bench blocks.
void print_bench_memory(size_t source_bytes, size_t mem_before) {
    double peak_mb = static_cast<double>(process_peak_working_set()) / (1024.0 * 1024.0);
    double delta_mb = static_cast<double>(
        static_cast<ptrdiff_t>(process_working_set()) -
        static_cast<ptrdiff_t>(mem_before)) / (1024.0 * 1024.0);
    std::fprintf(stderr, "  source size    %8zu bytes UTF-8\n", source_bytes);
    std::fprintf(stderr, "  peak workingset%8.1f MB\n", peak_mb);
    std::fprintf(stderr, "  process delta  %+8.1f MB  (working set vs baseline)\n", delta_mb);
}

}  // namespace

std::wstring run_colorizer_pipeline(const Options& opts) {
    // Baseline working set before any work, so --bench can report the delta
    // attributable to opening this file (factories + grammar + tree + layout).
    size_t mem_before = opts.bench ? process_working_set() : 0;

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
    Colorizer colorizer(L"grammars", L"config/themes");

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
    ColorizerDisplayConfig display = load_display_config(opts.display_config);
    display.cpp_grammar = variant;

    // ----- Colorize (eager whole-doc) -----
    ColorizeTimings ctimings;
    ColorizeResult colors = colorizer.colorize(content->raw_utf8, lang, opts.dark, &ctimings);
    auto _tcolor = std::chrono::steady_clock::now();

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
        json_opts.source_name = fs::path(opts.input_path).filename().string();
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
    // Mirrors the host's reparse_and_colorize + colorize_viewport flow.
    // Falls back to the eager whole-doc path when:
    //   - core handle unavailable (ABI mismatch)
    //   - language unsupported in the core
    //   - word_wrap is on (byte->line mapping unreliable)
    //   - wlx_core_parse returns null
    if (opts.cached_tree && !display.word_wrap) {
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

                // Skeleton layout: empty colors, capture line_byte_starts
                std::vector<int> line_byte_starts;
                LayoutTimings ltimings_ct;
                auto layout_ct = layout_source(dwrite_factory.Get(),
                                               content->text,
                                               content->raw_utf8,
                                               /*colors=*/{},
                                               theme,
                                               opts.dark,
                                               static_cast<float>(opts.width),
                                               display,
                                               &ltimings_ct,
                                               &line_byte_starts);
                auto _tlayout_ct = std::chrono::steady_clock::now();

                // Compute visible byte range for the rendered viewport.
                // For --full: highlight the entire document.
                // For viewport mode: use the same overscan math as colorize_viewport.
                const int bmp_height_ct = opts.full
                    ? std::max(1, static_cast<int>(std::ceil(layout_ct.total_height)))
                    : opts.height;
                const float scroll_y_ct = opts.full ? 0.0f : opts.scroll;

                uint32_t vlo = 0;
                uint32_t vhi = static_cast<uint32_t>(content->raw_utf8.size());

                if (!opts.full) {
                    // Same visible->byte math as the host's colorize_viewport.
                    const float viewport_h = static_cast<float>(bmp_height_ct);
                    auto vr = wlx::plugin_colorizer::layout::viewport_byte_range(
                        layout_ct.blocks, line_byte_starts,
                        static_cast<int>(content->raw_utf8.size()),
                        scroll_y_ct, viewport_h, /*overscan=*/viewport_h);
                    // vr.empty means empty/out-of-range layout; fall back to whole-doc coloring.
                    if (!vr.empty) {
                        vlo = vr.lo;
                        vhi = vr.hi;
                    }
                }

                // Highlight the visible byte range against the cached tree
                WlxColorSpan* ct_spans = nullptr;
                uint32_t ct_count = 0;
                auto _thigh0 = std::chrono::steady_clock::now();
                int hres = wlx_core_highlight_range(
                    core, tree.get(), opts.dark ? 1 : 0,
                    vlo, vhi, &ct_spans, &ct_count);
                auto _thigh1 = std::chrono::steady_clock::now();

                if (hres != 0) {
                    std::fprintf(stderr,
                        "[--cached-tree] wlx_core_highlight_range failed (%d) — "
                        "rendering without colors\n", hres);
                } else {
                    ColorizeResult ct_result = abi_spans_to_result(ct_spans, ct_count);
                    apply_spans_to_range(layout_ct, content->raw_utf8, line_byte_starts,
                                         ct_result, vlo, vhi, display.tab_width);
                }

                // ----- Render (cached-tree path) -----
                RenderEngine renderer_ct(d2d_factory.Get(), dwrite_factory.Get(), theme, opts.dark);
                hr = renderer_ct.create_bitmap_resources(wic_factory.Get(),
                                                         opts.width, bmp_height_ct);
                if (FAILED(hr)) {
                    std::fprintf(stderr, "Bitmap target failed: 0x%08lx\n", hr);
                    return {};
                }
                renderer_ct.paint(layout_ct, scroll_y_ct);
                if (renderer_ct.needs_recreate()) {
                    std::fprintf(stderr, "Render target lost during paint\n");
                    return {};
                }
                auto _tpaint_ct = std::chrono::steady_clock::now();

                if (opts.bench) {
                    auto ms = [](auto a, auto b) {
                        return std::chrono::duration<double, std::milli>(b - a).count();
                    };
                    int lines = 1;
                    for (char c : content->raw_utf8) if (c == '\n') ++lines;

                    std::fprintf(stderr,
                        "Colorizer timing --cached-tree (%d lines, lang=%s):\n",
                        lines, lang.c_str());
                    std::fprintf(stderr, "  file read        %8.2f ms\n",
                        ms(_t0, _tread));
                    std::fprintf(stderr, "  parse (cold)     %8.2f ms  (wlx_core_parse)\n",
                        ms(_tparse0, _tparse1));
                    std::fprintf(stderr, "  layout (skel)    %8.2f ms  (skeleton, no colors)\n",
                        ms(_tparse1, _tlayout_ct));
                    std::fprintf(stderr, "    line split     %8.2f ms\n",
                        ltimings_ct.line_split_ms);
                    std::fprintf(stderr, "    span index     %8.2f ms  (empty, skipped)\n",
                        ltimings_ct.span_index_ms);
                    std::fprintf(stderr, "    build blocks   %8.2f ms\n",
                        ltimings_ct.build_blocks_ms);
                    std::fprintf(stderr, "    line index     %8.2f ms\n",
                        ltimings_ct.line_index_ms);
                    std::fprintf(stderr,
                        "  highlight range  %8.2f ms  (wlx_core_highlight_range, "
                        "bytes %u..%u)\n",
                        ms(_thigh0, _thigh1), vlo, vhi);
                    std::fprintf(stderr, "  paint            %8.2f ms\n",
                        ms(_thigh1, _tpaint_ct));   // from end of highlight, not layout
                    // Sum the measured phases. NOT ms(_t0, _tpaint_ct): that wall-
                    // clock also includes the one-time core-singleton init / grammar
                    // scan triggered by acquire_compatible() in this path (a tool
                    // artifact — the real plugin shares one already-warm core).
                    double hot_ct = ms(_t0, _tread) + ms(_tparse0, _tparse1)
                                  + ms(_tparse1, _tlayout_ct) + ms(_thigh0, _thigh1)
                                  + ms(_thigh1, _tpaint_ct);
                    std::fprintf(stderr,
                        "  hot total        %8.2f ms  (read+parse+layout+highlight+paint)\n",
                        hot_ct);
                    print_bench_memory(content->raw_utf8.size(), mem_before);
                }

                // ----- Save (cached-tree path) -----
                std::wstring out_ct = sibling_path(opts.input_path,
                                                    opts.dark ? L"_dark.png" : L".png");
                std::error_code ec_ct;
                fs::create_directories(fs::path(out_ct).parent_path(), ec_ct);
                if (ec_ct) {
                    std::fprintf(stderr, "Cannot create output directory: %s\n",
                                 ec_ct.message().c_str());
                    return {};
                }
                hr = renderer_ct.save_to_png(wic_factory.Get(), out_ct.c_str());
                if (FAILED(hr)) {
                    std::fprintf(stderr, "Failed to save PNG: 0x%08lx\n", hr);
                    return {};
                }
                return out_ct;
                // tree freed here via TreePtr destructor
            }
        }
        // Fallback: word_wrap or parse failed — fall through to eager whole-doc below.
    } else if (opts.cached_tree && display.word_wrap) {
        std::fprintf(stderr,
            "[--cached-tree] fallback: word_wrap on — using whole-doc colorize\n");
    }

    // ----- Layout (eager whole-doc) -----

    LayoutTimings ltimings;
    auto layout = layout_source(dwrite_factory.Get(),
                                content->text,        // wstring source
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
    renderer.paint(layout, scroll_y);
    if (renderer.needs_recreate()) {
        std::fprintf(stderr, "Render target lost during paint\n");
        return {};
    }
    auto _tpaint = std::chrono::steady_clock::now();

    if (opts.bench) {
        auto ms = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        int lines = 1;
        for (char c : content->raw_utf8) if (c == '\n') ++lines;

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
        std::fprintf(stderr, "  colorize (cold)  %8.2f ms\n", ms(_tread, _tcolor));
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
        print_bench_memory(content->raw_utf8.size(), mem_before);
    }

    // ----- Save -----
    std::wstring out = sibling_path(opts.input_path, opts.dark ? L"_dark.png" : L".png");
    std::error_code ec;
    fs::create_directories(fs::path(out).parent_path(), ec);
    if (ec) {
        std::fprintf(stderr, "Cannot create output directory: %s\n", ec.message().c_str());
        return {};
    }
    hr = renderer.save_to_png(wic_factory.Get(), out.c_str());
    if (FAILED(hr)) {
        std::fprintf(stderr, "Failed to save PNG: 0x%08lx\n", hr);
        return {};
    }
    return out;
}

}  // namespace wlx::tools::screenshot
