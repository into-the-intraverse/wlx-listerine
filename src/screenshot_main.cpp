#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <psapi.h>
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

#include "runtime/io/file_service.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/layout/layout_engine.h"
#include "runtime/render/render_engine.h"
#include "theme_service.h"
#include "wlx_core/abi.h"
#include "search_engine.h"
#include "search_hud_painter.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

struct Options {
    std::wstring input_path;
    std::wstring config_path = L"config/wlx-listerine-md.toml";
    int width = 800;
    int height = 600;
    float scroll = 0;
    bool full = false;
    bool dark = false;
    bool bench = false;

    std::wstring search;     // empty == no search
    int search_step = 0;     // number of post-findfirst advancements
};

// ---------- high-resolution timer ----------

static double timer_freq() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return static_cast<double>(f.QuadPart);
}

static double now_ms() {
    static double freq = timer_freq();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / freq * 1000.0;
}

// ---------- memory stats ----------

static size_t estimate_document_memory(const Document& doc) {
    size_t bytes = sizeof(Document);
    for (auto& block : doc.blocks) {
        bytes += sizeof(BlockNode);
        for (auto& inl : block.inlines)
            bytes += sizeof(InlineNode) + inl.text.size() * sizeof(wchar_t);
        for (auto& child : block.children) {
            bytes += sizeof(BlockNode);
            for (auto& inl : child.inlines)
                bytes += sizeof(InlineNode) + inl.text.size() * sizeof(wchar_t);
        }
    }
    return bytes;
}

static size_t estimate_layout_memory(const LayoutDocument& layout) {
    size_t bytes = sizeof(LayoutDocument);
    for (auto& block : layout.blocks) {
        bytes += sizeof(LayoutBlock);
        for (auto& run : block.text_runs)
            bytes += sizeof(TextRun) + run.text.size() * sizeof(wchar_t);
        bytes += block.spans.size() * sizeof(InteractiveSpan);
    }
    bytes += layout.anchors.size() * sizeof(AnchorEntry);
    return bytes;
}

static size_t process_working_set() {
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return pmc.WorkingSetSize;
}

// ---------- args ----------

static void print_usage() {
    std::fprintf(stderr,
        "Usage: screenshot_tool <input.md> [options]\n"
        "Options:\n"
        "  --width <px>     Viewport width (default: 800)\n"
        "  --height <px>    Viewport height (default: 600)\n"
        "  --full           Render entire document\n"
        "  --scroll <px>    Scroll offset in viewport mode (default: 0)\n"
        "  --config <path>  TOML config path (default: config/wlx-listerine-md.toml)\n"
        "  --dark           Force dark mode\n"
        "  --bench          Print timing and memory stats\n"
        "  --search <term>  Run a search for <term> after layout\n"
        "  --search-step N  Advance the search cursor by N steps (default 0)\n");
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
        } else if (std::strcmp(argv[i], "--bench") == 0) {
            opts.bench = true;
        } else if (std::strcmp(argv[i], "--search") == 0 && i + 1 < argc) {
            opts.search = to_wstring(argv[++i]);
        } else if (std::strcmp(argv[i], "--search-step") == 0 && i + 1 < argc) {
            opts.search_step = std::atoi(argv[++i]);
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

    // Build output path as sibling of input: dir/foo.md -> dir/foo.png
    fs::path input(opts.input_path);
    std::wstring stem = input.stem().wstring();
    std::wstring out_name = stem + (opts.dark ? L"_dark.png" : L".png");
    fs::path out_path = input.parent_path().empty()
        ? fs::path(out_name)
        : input.parent_path() / out_name;

    // Ensure output directory exists
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

    size_t mem_before = opts.bench ? process_working_set() : 0;

    // run_pipeline returns the output path on success, or an empty string on
    // failure (error already printed to stderr).  All COM objects are created
    // and destroyed inside the lambda so that every Release() call happens
    // before CoUninitialize() below.
    auto run_pipeline = [&]() -> std::wstring {
        double t0 = now_ms();

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

        double t_factories = now_ms();

        // Load theme config
        ThemeService theme;
        theme.load(opts.config_path);

        // Acquire the shared core singleton (lazy-init via GetModuleFileNameW
        // inside wlx-listerine-core.dll). Grammars/themes are mirrored next
        // to the DLL by a CMake POST_BUILD step.
        WlxCore* core = wlx_core::acquire_compatible();

        double t_theme = now_ms();

        // Read and parse markdown
        FileService file_service;
        auto content = file_service.read(opts.input_path.c_str());
        if (!content) {
            std::fprintf(stderr, "Failed to read: %ls\n", opts.input_path.c_str());
            return {};
        }

        double t_read = now_ms();

        MarkdownParser parser;
        auto doc = parser.parse(content->raw_utf8.c_str(), content->raw_utf8.size());

        double t_parse = now_ms();

        // Layout
        LayoutEngine layout_engine(dwrite_factory.Get(), theme, opts.dark, core);
        float viewport_width = static_cast<float>(opts.width);
        auto layout = layout_engine.layout(doc, viewport_width);

        double t_layout = now_ms();

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

        double t_target = now_ms();

        // Optional: run a search before painting so the document and the
        // match highlights are baked into a single paint pass.
        int hud_total  = -1;
        int hud_cursor = -1;
        if (!opts.search.empty()) {
            SearchIndex idx;
            idx.build(layout);
            SearchQuery q;
            q.needle      = opts.search;
            q.match_case  = false;
            q.whole_words = false;
            q.backwards   = false;
            auto matches = idx.find_all(q);

            hud_total = static_cast<int>(matches.size());
            hud_cursor = (hud_total > 0)
                ? std::clamp(opts.search_step, 0, hud_total - 1)
                : -1;
            renderer.set_search_matches(matches, hud_cursor);
        }

        // Paint
        renderer.paint(layout, scroll_y);

        // Overlay the HUD via the painter.
        if (!opts.search.empty()) {
            ID2D1RenderTarget* rt = renderer.render_target();
            if (rt) {
                SearchHudPainter hud_painter(dwrite_factory.Get(), theme);
                SearchHudState s{};
                s.current_one_based = (hud_total > 0) ? (hud_cursor + 1) : 0;
                s.total             = hud_total;
                auto rects = hud_painter.layout(s);

                rt->BeginDraw();
                float bx = static_cast<float>(bmp_width)  - rects.width  - 12.0f;
                float by = static_cast<float>(bmp_height) - rects.height - 12.0f;
                rt->SetTransform(D2D1::Matrix3x2F::Translation(bx, by));
                hud_painter.paint(rt, s, rects, opts.dark);
                rt->SetTransform(D2D1::Matrix3x2F::Identity());
                HRESULT hr_e = rt->EndDraw();
                if (FAILED(hr_e)) {
                    std::fprintf(stderr, "HUD overlay EndDraw failed: 0x%08lx\n", hr_e);
                }
            }
        }

        if (renderer.needs_recreate()) {
            std::fprintf(stderr, "Render target lost during paint\n");
            return {};
        }

        double t_paint = now_ms();

        // Save PNG
        hr = renderer.save_to_png(wic_factory.Get(), out_path.c_str());
        if (FAILED(hr)) {
            std::fprintf(stderr, "Failed to save PNG: 0x%08lx\n", hr);
            return {};
        }

        double t_save = now_ms();

        if (opts.bench) {
            size_t mem_after = process_working_set();

            std::fprintf(stderr, "\n--- Benchmark ---\n");
            std::fprintf(stderr, "File:       %ls\n", opts.input_path.c_str());
            std::fprintf(stderr, "Size:       %zu bytes UTF-8\n", content->raw_utf8.size());
            std::fprintf(stderr, "Viewport:   %d x %d\n", bmp_width, bmp_height);
            std::fprintf(stderr, "\n");

            std::fprintf(stderr, "Timing:\n");
            std::fprintf(stderr, "  factories  %6.2f ms\n", t_factories - t0);
            std::fprintf(stderr, "  theme      %6.2f ms\n", t_theme - t_factories);
            std::fprintf(stderr, "  file read  %6.2f ms\n", t_read - t_theme);
            std::fprintf(stderr, "  parse      %6.2f ms\n", t_parse - t_read);
            std::fprintf(stderr, "  layout     %6.2f ms\n", t_layout - t_parse);
            std::fprintf(stderr, "  target     %6.2f ms\n", t_target - t_layout);
            std::fprintf(stderr, "  paint      %6.2f ms\n", t_paint - t_target);
            std::fprintf(stderr, "  save png   %6.2f ms\n", t_save - t_paint);
            std::fprintf(stderr, "  ─────────────────\n");
            std::fprintf(stderr, "  TOTAL      %6.2f ms\n", t_save - t0);
            std::fprintf(stderr, "\n");

            // In the real plugin, factories are created once and reused.
            // The "hot path" is read + parse + layout + paint.
            double hot = (t_read - t_theme) + (t_parse - t_read)
                       + (t_layout - t_parse) + (t_paint - t_target);
            std::fprintf(stderr, "  hot path   %6.2f ms  (read+parse+layout+paint)\n", hot);
            std::fprintf(stderr, "\n");

            std::fprintf(stderr, "Document:\n");
            std::fprintf(stderr, "  AST blocks     %zu\n", doc.blocks.size());
            std::fprintf(stderr, "  layout blocks  %zu\n", layout.blocks.size());
            std::fprintf(stderr, "  total height   %.0f px\n", layout.total_height);
            std::fprintf(stderr, "\n");

            size_t doc_mem = estimate_document_memory(doc);
            size_t lay_mem = estimate_layout_memory(layout);
            std::fprintf(stderr, "Memory (estimates):\n");
            std::fprintf(stderr, "  document AST   %6zu bytes\n", doc_mem);
            std::fprintf(stderr, "  layout data    %6zu bytes\n", lay_mem);
            std::fprintf(stderr, "  process delta  %+.0f KB\n",
                         static_cast<double>(mem_after - mem_before) / 1024.0);
            std::fprintf(stderr, "\n");
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
