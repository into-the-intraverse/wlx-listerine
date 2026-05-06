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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "runtime/io/file_service.h"
#include "runtime/render/render_engine.h"
#include "runtime/theme/theme_service.h"
#include "runtime/util/string_util.h"
#include "core_dll/colorizer/colorizer.h"
#include "plugin_colorizer/layout/colorizer_layout.h"
#include "plugin_colorizer/language/routing.h"
#include "tools/screenshot/token_json_writer.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

using wlx::core::colorizer::Colorizer;
using wlx::core::colorizer::ColorizeResult;
using wlx::plugin_colorizer::layout::ColorizerDisplayConfig;
using wlx::plugin_colorizer::layout::CppGrammar;
using wlx::plugin_colorizer::layout::layout_source;
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

}  // namespace

std::wstring run_colorizer_pipeline(const Options& opts) {
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
    FileService file_service;
    auto content = file_service.read(opts.input_path.c_str());
    if (!content) {
        std::fprintf(stderr, "Failed to read: %ls\n", opts.input_path.c_str());
        return {};
    }

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

    // ----- Colorize -----
    ColorizeResult colors = colorizer.colorize(content->raw_utf8, lang, opts.dark);

    // ----- Build display config (used by both paint and dump-tokens paths) -----
    // Load TOML overrides first, then force cpp_grammar from --cpp-grammar
    // (the flag wins over any value the TOML might set).
    ColorizerDisplayConfig display = load_display_config(opts.display_config);
    display.cpp_grammar = variant;

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

    // ----- Layout -----

    auto layout = layout_source(dwrite_factory.Get(),
                                content->text,        // wstring source
                                content->raw_utf8,    // utf-8 source
                                colors,
                                theme,
                                opts.dark,
                                static_cast<float>(opts.width),
                                display);

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
