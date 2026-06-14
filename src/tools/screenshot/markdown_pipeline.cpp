#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tools/screenshot/markdown_pipeline.h"
#include "tools/screenshot/options.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>

#include "runtime/io/file_service.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/layout/layout_engine.h"
#include "runtime/layout/md_materialize.h"
#include "runtime/render/render_engine.h"
#include "runtime/theme/theme_service.h"
#include "wlx_core/abi.h"
#include "runtime/search/search_index.h"
#include "runtime/search/search_hud_painter.h"
#include "tools/screenshot/working_set_sample.h"
#include "runtime/cache/memory_estimate.h"

#include <memory>

using namespace wlx::runtime::cache;
using namespace wlx::runtime::io;
using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;
using namespace wlx::runtime::render;
using namespace wlx::runtime::search;
using namespace wlx::runtime::theme;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace wlx::tools::screenshot {

namespace {

// ---------- high-resolution timer ----------

double timer_freq() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return static_cast<double>(f.QuadPart);
}

double now_ms() {
    static double freq = timer_freq();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / freq * 1000.0;
}

}  // namespace

std::wstring run_markdown_pipeline(const Options& opts) {
    // Capture baseline memory before any work — keeps the benchmark's
    // process delta number measuring just what this pipeline allocates.
    size_t mem_before = opts.bench ? sample_working_set().current : 0;

    // Per-phase working-set samples (only populated when --bench).
    size_t ws_read        = 0;
    size_t ws_parse       = 0;
    size_t ws_layout      = 0;
    size_t ws_target      = 0;
    size_t ws_materialize = 0;
    size_t ws_paint       = 0;

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
        return {};
    }

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
    if (opts.bench) ws_read = sample_working_set().current;

    MarkdownParser parser;
    // shared_ptr so the lazy path can hand ownership to the layout's materialize
    // ctx (its recipes hold const InlineNode* into this Document, which must
    // outlive the layout). Harmless for the eager path.
    auto doc = std::make_shared<Document>(
        parser.parse(content->raw_utf8.c_str(), content->raw_utf8.size()));

    double t_parse = now_ms();
    if (opts.bench) ws_parse = sample_working_set().current;

    // Layout
    LayoutEngine layout_engine(dwrite_factory.Get(), theme, opts.dark, core);
    float viewport_width = static_cast<float>(opts.width);
    // --full renders the whole document, so viewport-lazy materialization gives
    // nothing there — and it would size the bitmap from an estimated total_height
    // before materialize corrects it. Force eager for --full.
    const bool lazy = opts.lazy && !opts.full;
    auto layout = lazy
        ? layout_engine.layout(*doc, viewport_width, /*wrap_code=*/false,
                               /*gutter_width=*/0.0f, /*lazy=*/true)
        : layout_engine.layout(*doc, viewport_width);
    // Retained so the bench can pass &recipes to materialize_viewport (mirroring
    // the host, which evicts off-screen layouts). The materialize_block closure
    // also holds the ctx, but only this handle exposes the recipes.
    std::shared_ptr<MdMaterializeCtx> md_ctx;
    if (lazy) {
        // Lifetime guard: the materialize_block closure captured the ctx; the ctx
        // must own the Document so its recipe inline pointers stay valid until the
        // layout (and the ctx) is destroyed. Skipping this is a use-after-free.
        md_ctx = layout_engine.take_md_ctx();
        if (md_ctx)
            md_ctx->document = doc;
    }

    double t_layout = now_ms();
    if (opts.bench) ws_layout = sample_working_set().current;

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
    if (opts.bench) ws_target = sample_working_set().current;

    // Lazy only: build + reflow the blocks intersecting the painted viewport
    // before painting (and before search indexing). Eager docs are a no-op.
    // Without this, paint would materialize visible blocks mid-frame WITHOUT
    // reflow, mispositioning everything below a mis-estimated block this frame.
    // Use the same scroll_y / viewport height paint will use (DIPs == pixels at
    // the bitmap target's default 96 DPI).
    if (lazy)
        wlx::runtime::layout::materialize_viewport(layout, scroll_y, renderer.dip_height(),
                                                   md_ctx ? &md_ctx->recipes : nullptr);

    double t_materialize = now_ms();
    if (opts.bench) ws_materialize = sample_working_set().current;

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

    // Paint. A swallowed EndDraw failure here would save a corrupt PNG with
    // exit 0 (and bake it into goldens), so fail hard on any FAILED(hr).
    hr = renderer.paint(layout, scroll_y);
    if (FAILED(hr)) {
        std::fprintf(stderr, "Paint failed (EndDraw): 0x%08lx\n", hr);
        return {};
    }

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
                return {};
            }
        }
    }

    double t_paint = now_ms();
    if (opts.bench) ws_paint = sample_working_set().current;

    // Save PNG
    hr = renderer.save_to_png(wic_factory.Get(), out_path.c_str());
    if (FAILED(hr)) {
        std::fprintf(stderr, "Failed to save PNG: 0x%08lx\n", hr);
        return {};
    }

    double t_save = now_ms();

    if (opts.bench) {
        size_t mem_after = sample_working_set().current;

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
        if (lazy)
            std::fprintf(stderr, "  materialize%6.2f ms\n", t_materialize - t_target);
        // "paint" spans t_materialize..t_paint, so any --search indexing time is
        // included here (it runs between materialize and paint).
        std::fprintf(stderr, "  paint      %6.2f ms\n", t_paint - t_materialize);
        std::fprintf(stderr, "  save png   %6.2f ms\n", t_save - t_paint);
        std::fprintf(stderr, "  \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\n");
        std::fprintf(stderr, "  TOTAL      %6.2f ms\n", t_save - t0);
        std::fprintf(stderr, "\n");

        // In the real plugin, factories are created once and reused.
        // The "hot path" is read + parse + layout + (materialize) + paint.
        double hot = (t_read - t_theme) + (t_parse - t_read)
                   + (t_layout - t_parse) + (t_materialize - t_target)
                   + (t_paint - t_materialize);
        std::fprintf(stderr, "  hot path   %6.2f ms  (read+parse+layout+%spaint)\n",
                     hot, lazy ? "materialize+" : "");
        std::fprintf(stderr, "\n");

        std::fprintf(stderr, "Document:\n");
        std::fprintf(stderr, "  AST blocks     %zu\n", doc->blocks.size());
        std::fprintf(stderr, "  layout blocks  %zu\n", layout.blocks.size());
        std::fprintf(stderr, "  total height   %.0f px\n", layout.total_height);
        std::fprintf(stderr, "\n");

        size_t doc_mem = estimate_document_memory(*doc);
        size_t lay_mem = estimate_layout_memory(layout);
        std::fprintf(stderr, "Memory (estimates):\n");
        std::fprintf(stderr, "  document AST   %6zu bytes  (%.1f MB)\n",
                     doc_mem, static_cast<double>(doc_mem) / (1024.0 * 1024.0));
        std::fprintf(stderr, "  layout data    %6zu bytes  (%.1f MB)\n",
                     lay_mem, static_cast<double>(lay_mem) / (1024.0 * 1024.0));
        // Signed: the working set can legitimately SHRINK vs the baseline,
        // and unsigned size_t subtraction would wrap to a huge bogus delta.
        std::fprintf(stderr, "  process delta  %+.0f KB\n",
                     static_cast<double>(static_cast<ptrdiff_t>(mem_after) -
                                         static_cast<ptrdiff_t>(mem_before)) / 1024.0);
        std::fprintf(stderr, "\n");

        // Per-phase working-set deltas vs mem_before (bench-only).
        // Labels intentionally differ from "process delta" so RX_DELTA in bench.py
        // does not pick them up.
        auto phase_mb = [mem_before](size_t ws) {
            return (static_cast<double>(ws) - static_cast<double>(mem_before))
                   / (1024.0 * 1024.0);
        };
        std::fprintf(stderr, "Working-set phases (cumulative from baseline):\n");
        std::fprintf(stderr, "  ws after read         %+8.1f MB\n", phase_mb(ws_read));
        std::fprintf(stderr, "  ws after parse        %+8.1f MB\n", phase_mb(ws_parse));
        std::fprintf(stderr, "  ws after layout       %+8.1f MB\n", phase_mb(ws_layout));
        std::fprintf(stderr, "  ws after target       %+8.1f MB\n", phase_mb(ws_target));
        if (lazy)
            std::fprintf(stderr, "  ws after materialize  %+8.1f MB\n", phase_mb(ws_materialize));
        std::fprintf(stderr, "  ws after paint        %+8.1f MB\n", phase_mb(ws_paint));
        std::fprintf(stderr, "\n");
    }

    return out_path.wstring();
}

}  // namespace wlx::tools::screenshot
