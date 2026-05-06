#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tools/screenshot/colorizer_pipeline.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "runtime/io/file_service.h"
#include "runtime/render/render_engine.h"
#include "runtime/theme/theme_service.h"
#include "runtime/util/string_util.h"
#include "core_dll/colorizer/colorizer.h"
#include "plugin_colorizer/layout/colorizer_layout.h"
#include "plugin_colorizer/language/routing.h"

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

// Build the output PNG path: <stem>.<ext>.png next to the source. The dark
// suffix (_dark) is appended when --dark is set, mirroring the markdown
// pipeline's _dark suffix convention.
std::wstring out_path_for_paint(const std::wstring& input_path, bool dark) {
    fs::path input(input_path);
    std::wstring stem = input.filename().wstring();  // includes extension
    std::wstring out_name = stem + (dark ? L"_dark.png" : L".png");
    return input.parent_path().empty()
        ? fs::path(out_name).wstring()
        : (input.parent_path() / out_name).wstring();
}

// Default config path for colorizer mode. If the user passed --config, that's
// authoritative. If they left the default (the markdown config), swap to the
// colorizer config — otherwise the colorizer would load markdown-only TOML
// fields and miss the colorizer-specific [display]/[fonts.code] sections.
std::wstring resolve_config_path(const std::wstring& opts_config_path) {
    if (opts_config_path == L"config/wlx-listerine-md.toml") {
        return L"config/wlx-listerine-colorizer.toml";
    }
    return opts_config_path;
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
    theme.load(resolve_config_path(opts.config_path));

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

    // ----- --dump-tokens branch (real impl in Stage 3.4) -----
    if (opts.dump_tokens) {
        std::fprintf(stderr, "--dump-tokens not yet wired (Stage 3.4)\n");
        return {};
    }

    // ----- Layout (Stage 3.5 will load --display-config; for now, defaults) -----
    ColorizerDisplayConfig display;
    display.cpp_grammar = variant;

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
    std::wstring out = out_path_for_paint(opts.input_path, opts.dark);
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
