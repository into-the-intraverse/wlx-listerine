#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "file_service.h"
#include "markdown_parser.h"
#include "layout_engine.h"
#include "render_engine.h"
#include "theme_service.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

struct Options {
    std::wstring input_path;
    std::wstring config_path = L"config/wlx-mini-markdown.toml";
    int width = 800;
    int height = 600;
    float scroll = 0;
    bool full = false;
    bool dark = false;
};

static void print_usage() {
    std::fprintf(stderr,
        "Usage: screenshot_tool <input.md> [options]\n"
        "Options:\n"
        "  --width <px>     Viewport width (default: 800)\n"
        "  --height <px>    Viewport height (default: 600)\n"
        "  --full           Render entire document\n"
        "  --scroll <px>    Scroll offset in viewport mode (default: 0)\n"
        "  --config <path>  TOML config path (default: config/wlx-mini-markdown.toml)\n"
        "  --dark           Force dark mode\n");
}

static std::wstring to_wstring(const char* s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), len);
    return out;
}

static bool parse_args(int argc, char* argv[], Options& opts) {
    if (argc < 2) return false;

    opts.input_path = to_wstring(argv[1]);

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            opts.width = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            opts.height = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--scroll") == 0 && i + 1 < argc) {
            opts.scroll = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            opts.config_path = to_wstring(argv[++i]);
        } else if (std::strcmp(argv[i], "--full") == 0) {
            opts.full = true;
        } else if (std::strcmp(argv[i], "--dark") == 0) {
            opts.dark = true;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return false;
        }
    }
    if (opts.width <= 0 || opts.height <= 0) {
        std::fprintf(stderr, "Width and height must be positive integers\n");
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    Options opts;
    if (!parse_args(argc, argv, opts)) {
        print_usage();
        return 1;
    }

    // Build output path: test_data/<stem>.png or test_data/<stem>_dark.png
    fs::path input(opts.input_path);
    std::wstring stem = input.stem().wstring();
    std::wstring out_name = stem + (opts.dark ? L"_dark.png" : L".png");
    fs::path out_path = fs::path(L"test_data") / out_name;

    // Ensure test_data directory exists
    std::error_code ec;
    fs::create_directories(out_path.parent_path(), ec);
    if (ec) {
        std::fprintf(stderr, "Cannot create output directory: %s\n", ec.message().c_str());
        return 1;
    }

    // Initialize COM (needed for WIC)
    HRESULT hr_com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr_com)) {
        std::fprintf(stderr, "Failed to initialize COM: 0x%08lx\n", hr_com);
        return 1;
    }

    // run_pipeline returns the output path on success, or an empty string on
    // failure (error already printed to stderr).  All COM objects are created
    // and destroyed inside the lambda so that every Release() call happens
    // before CoUninitialize() below.
    auto run_pipeline = [&]() -> std::wstring {
        ComPtr<ID2D1Factory> d2d_factory;
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       d2d_factory.GetAddressOf());
        if (FAILED(hr)) {
            std::fprintf(stderr, "Failed to create D2D factory: 0x%08lx\n", hr);
            return {};
        }

        ComPtr<IDWriteFactory> dwrite_factory;
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(dwrite_factory.GetAddressOf()));
        if (FAILED(hr)) {
            std::fprintf(stderr, "Failed to create DWrite factory: 0x%08lx\n", hr);
            return {};
        }

        ComPtr<IWICImagingFactory> wic_factory;
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(wic_factory.GetAddressOf()));
        if (FAILED(hr)) {
            std::fprintf(stderr, "Failed to create WIC factory: 0x%08lx\n", hr);
            return {};
        }

        // Load theme config
        ThemeService theme;
        theme.load(opts.config_path);

        // Read and parse markdown
        FileService file_service;
        auto content = file_service.read(opts.input_path.c_str());
        if (!content) {
            std::fprintf(stderr, "Failed to read: %ls\n", opts.input_path.c_str());
            return {};
        }

        MarkdownParser parser;
        auto doc = parser.parse(content->raw_utf8.c_str(), content->raw_utf8.size());

        // Layout
        LayoutEngine layout_engine(dwrite_factory.Get(), theme, opts.dark);
        float viewport_width = static_cast<float>(opts.width);
        auto layout = layout_engine.layout(doc, viewport_width);

        // Determine bitmap dimensions
        int bmp_width = opts.width;
        int bmp_height = opts.height;
        float scroll_y = opts.scroll;

        if (opts.full) {
            bmp_height = std::max(1, static_cast<int>(std::ceil(layout.total_height)));
            scroll_y = 0;
        } else {
            bmp_height = opts.height;
            scroll_y = opts.scroll;
        }

        // Create render engine with bitmap target
        RenderEngine renderer(d2d_factory.Get(), dwrite_factory.Get(), theme, opts.dark);
        hr = renderer.create_bitmap_resources(wic_factory.Get(), bmp_width, bmp_height);
        if (FAILED(hr)) {
            std::fprintf(stderr, "Failed to create bitmap target: 0x%08lx\n", hr);
            return {};
        }

        // Paint
        renderer.paint(layout, scroll_y);
        if (renderer.needs_recreate()) {
            std::fprintf(stderr, "Render target lost during paint\n");
            return {};
        }

        // Save PNG
        hr = renderer.save_to_png(wic_factory.Get(), out_path.c_str());
        if (FAILED(hr)) {
            std::fprintf(stderr, "Failed to save PNG: 0x%08lx\n", hr);
            return {};
        }

        return out_path.wstring();
    };

    std::wstring result = run_pipeline();

    CoUninitialize();

    if (result.empty())
        return 1;

    std::fprintf(stdout, "%ls\n", result.c_str());
    return 0;
}
