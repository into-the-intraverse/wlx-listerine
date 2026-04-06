# Screenshot Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a standalone CLI tool that renders markdown files to PNG screenshots using the same Direct2D/DirectWrite pipeline as the WLX plugin.

**Architecture:** Extract shared rendering code into a static library (`wlx-core`). Add a new executable target (`screenshot_tool`) that uses WIC bitmap-backed render targets for offscreen rendering. The plugin, tests, and screenshot tool all link against `wlx-core`.

**Tech Stack:** C++17, MSVC, CMake 3.20+, Direct2D, DirectWrite, WIC (Windows Imaging Component), md4c, tomlplusplus, doctest

---

### Task 1: Extract wlx-core static library in CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

This task restructures the build to eliminate source duplication. The core rendering pipeline becomes a static library. The plugin becomes a thin DLL wrapper. Tests link the library instead of recompiling sources.

- [ ] **Step 1: Replace the CMakeLists.txt content**

Replace the entire `CMakeLists.txt` with:

```cmake
cmake_minimum_required(VERSION 3.20)
project(wlx-mini-markdown LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(md4c REQUIRED)
find_package(tomlplusplus REQUIRED)

# --- Core static library ---
add_library(wlx-core STATIC
    src/file_service.cpp
    src/markdown_parser.cpp
    src/layout_engine.cpp
    src/render_engine.cpp
    src/interaction_engine.cpp
    src/theme_service.cpp
    src/cache_service.cpp
)

target_include_directories(wlx-core PUBLIC include src)
target_link_libraries(wlx-core PUBLIC
    md4c::md4c
    tomlplusplus::tomlplusplus
    d2d1
    dwrite
)

# --- Plugin DLL ---
add_library(wlx-mini-markdown SHARED
    src/host_adapter.cpp
    src/plugin.def
    src/resource.rc
)

target_link_libraries(wlx-mini-markdown PRIVATE
    wlx-core
    shell32
)

set_target_properties(wlx-mini-markdown PROPERTIES
    SUFFIX ".wlx64"
    PREFIX ""
)

# --- Screenshot tool ---
add_executable(screenshot_tool
    src/screenshot_main.cpp
)

target_link_libraries(screenshot_tool PRIVATE
    wlx-core
    windowscodecs
)

# --- Tests ---
find_package(doctest REQUIRED)

add_executable(tests
    tests/test_main.cpp
    tests/test_document_model.cpp
    tests/test_theme_service.cpp
    tests/test_file_service.cpp
    tests/test_markdown_parser.cpp
    tests/test_cache_service.cpp
    tests/test_layout_engine.cpp
)

target_link_libraries(tests PRIVATE
    wlx-core
    doctest::doctest
)
```

- [ ] **Step 2: Create a stub screenshot_main.cpp so the build doesn't fail**

Create `src/screenshot_main.cpp`:

```cpp
int main() {
    return 0;
}
```

- [ ] **Step 3: Build and verify all targets compile**

Run:
```bash
conan install . --output-folder=build --build=missing -s build_type=Release
cmake --preset conan-default
cmake --build --preset conan-release
```

Expected: All four targets build successfully (wlx-core, wlx-mini-markdown, screenshot_tool, tests).

- [ ] **Step 4: Run existing tests to verify no regressions**

Run:
```bash
./build/Release/tests.exe
```

Expected: All 86 tests pass.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/screenshot_main.cpp
git commit -m "refactor: extract wlx-core static library from CMakeLists.txt"
```

---

### Task 2: Widen RenderEngine to support bitmap render targets

**Files:**
- Modify: `src/render_engine.h`
- Modify: `src/render_engine.cpp`

The render engine currently stores `ComPtr<ID2D1HwndRenderTarget>`. This task changes it to `ComPtr<ID2D1RenderTarget>` (the base interface) and adds a WIC-backed bitmap creation path. The HWND path remains unchanged in behavior.

- [ ] **Step 1: Update render_engine.h**

In `src/render_engine.h`, make these changes:

1. Add WIC includes and a forward declaration at the top (after the existing includes):

```cpp
#include <wincodec.h>
```

2. Add new public methods after `create_device_resources`:

```cpp
    HRESULT create_bitmap_resources(IWICImagingFactory* wic_factory, int width, int height);
    HRESULT save_to_png(IWICImagingFactory* wic_factory, const wchar_t* path);
```

3. Change the `rt_` member from `ComPtr<ID2D1HwndRenderTarget>` to `ComPtr<ID2D1RenderTarget>`:

```cpp
    ComPtr<ID2D1RenderTarget> rt_;
```

4. Add a WIC bitmap member and a flag to track target type:

```cpp
    ComPtr<IWICBitmap> wic_bitmap_;
    bool is_hwnd_target_ = false;
```

The full updated header should be:

```cpp
#pragma once

#include "layout_engine.h"
#include "theme_service.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <optional>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

class RenderEngine {
public:
    RenderEngine(ID2D1Factory* d2d_factory, IDWriteFactory* dwrite_factory,
                 const ThemeService& theme, bool dark_mode);

    HRESULT create_device_resources(HWND hwnd);
    HRESULT create_bitmap_resources(IWICImagingFactory* wic_factory, int width, int height);
    HRESULT save_to_png(IWICImagingFactory* wic_factory, const wchar_t* path);
    void discard_device_resources();
    void resize(UINT width, UINT height);

    void paint(const LayoutDocument& layout, float scroll_y);

    void set_dark_mode(bool dark);
    void set_hovered_span(int index) { hovered_span_ = index; }

    bool needs_recreate() const { return needs_recreate_; }
    UINT width() const { return width_; }
    UINT height() const { return height_; }

private:
    ID2D1SolidColorBrush* get_brush(uint32_t color);
    void paint_block_background(const LayoutBlock& block, float offset_y);
    void paint_block_decoration(const LayoutBlock& block, float offset_y);
    void paint_bullet(const LayoutBlock& block, float offset_y);
    void paint_text_runs(const LayoutBlock& block, float offset_y);

    ID2D1Factory* d2d_factory_;
    IDWriteFactory* dwrite_factory_;
    const ThemeService& theme_;
    bool dark_mode_;

    ComPtr<ID2D1RenderTarget> rt_;
    ComPtr<IWICBitmap> wic_bitmap_;
    bool is_hwnd_target_ = false;
    std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>> brush_cache_;

    // Cached text format for bullet rendering
    ComPtr<IDWriteTextFormat> bullet_format_;

    int hovered_span_ = -1;
    bool needs_recreate_ = false;
    UINT width_ = 0;
    UINT height_ = 0;
};
```

- [ ] **Step 2: Update create_device_resources in render_engine.cpp**

The existing `create_device_resources` method stores the result into a `ComPtr<ID2D1HwndRenderTarget>`. Since `rt_` is now `ComPtr<ID2D1RenderTarget>`, we need to create via a local HWND target pointer and assign to the base:

Replace the `create_device_resources` method:

```cpp
HRESULT RenderEngine::create_device_resources(HWND hwnd) {
    if (rt_) return S_OK;

    RECT rc;
    GetClientRect(hwnd, &rc);
    width_ = static_cast<UINT>(rc.right - rc.left);
    height_ = static_cast<UINT>(rc.bottom - rc.top);

    D2D1_SIZE_U size = D2D1::SizeU(width_, height_);
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(hwnd, size);

    ComPtr<ID2D1HwndRenderTarget> hwnd_rt;
    HRESULT hr = d2d_factory_->CreateHwndRenderTarget(rtProps, hwndProps, hwnd_rt.GetAddressOf());
    if (SUCCEEDED(hr)) {
        rt_ = hwnd_rt;
        is_hwnd_target_ = true;
    }
    needs_recreate_ = false;
    return hr;
}
```

- [ ] **Step 3: Add create_bitmap_resources method**

Add after `create_device_resources`:

```cpp
HRESULT RenderEngine::create_bitmap_resources(IWICImagingFactory* wic_factory, int width, int height) {
    if (rt_) return S_OK;

    width_ = static_cast<UINT>(width);
    height_ = static_cast<UINT>(height);

    HRESULT hr = wic_factory->CreateBitmap(
        width_, height_,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapCacheOnDemand,
        wic_bitmap_.GetAddressOf());
    if (FAILED(hr)) return hr;

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    hr = d2d_factory_->CreateWicBitmapRenderTarget(
        wic_bitmap_.Get(), rtProps, rt_.GetAddressOf());

    is_hwnd_target_ = false;
    needs_recreate_ = false;
    return hr;
}
```

- [ ] **Step 4: Add save_to_png method**

Add after `create_bitmap_resources`:

```cpp
HRESULT RenderEngine::save_to_png(IWICImagingFactory* wic_factory, const wchar_t* path) {
    if (!wic_bitmap_) return E_FAIL;

    ComPtr<IWICStream> stream;
    HRESULT hr = wic_factory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapEncoder> encoder;
    hr = wic_factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->SetSize(width_, height_);
    if (FAILED(hr)) return hr;

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) return hr;

    hr = frame->WriteSource(wic_bitmap_.Get(), nullptr);
    if (FAILED(hr)) return hr;

    hr = frame->Commit();
    if (FAILED(hr)) return hr;

    return encoder->Commit();
}
```

- [ ] **Step 5: Guard resize() for HWND targets only**

Replace the `resize` method:

```cpp
void RenderEngine::resize(UINT width, UINT height) {
    width_ = width;
    height_ = height;
    if (rt_ && is_hwnd_target_) {
        ComPtr<ID2D1HwndRenderTarget> hwnd_rt;
        if (SUCCEEDED(rt_.As(&hwnd_rt))) {
            hwnd_rt->Resize(D2D1::SizeU(width, height));
        }
    }
}
```

- [ ] **Step 6: Update discard_device_resources to also reset wic_bitmap_**

Replace the `discard_device_resources` method:

```cpp
void RenderEngine::discard_device_resources() {
    rt_.Reset();
    wic_bitmap_.Reset();
    brush_cache_.clear();
    is_hwnd_target_ = false;
}
```

- [ ] **Step 7: Build and run tests**

Run:
```bash
cmake --build --preset conan-release
./build/Release/tests.exe
```

Expected: All 86 tests pass. The plugin target also builds. (Note: tests don't exercise RenderEngine directly, but this confirms the header/source changes don't break compilation.)

- [ ] **Step 8: Commit**

```bash
git add src/render_engine.h src/render_engine.cpp
git commit -m "feat: add WIC bitmap render target support to RenderEngine"
```

---

### Task 3: Implement screenshot_main.cpp

**Files:**
- Modify: `src/screenshot_main.cpp` (replace the stub from Task 1)

This is the CLI tool. It parses args, sets up D2D/DWrite/WIC factories, runs the rendering pipeline, and saves a PNG.

- [ ] **Step 1: Write the full screenshot_main.cpp**

Replace `src/screenshot_main.cpp` with:

```cpp
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

    // Initialize COM (needed for WIC)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Create factories
    ComPtr<ID2D1Factory> d2d_factory;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_factory.GetAddressOf());
    if (FAILED(hr)) {
        std::fprintf(stderr, "Failed to create D2D factory: 0x%08lx\n", hr);
        return 1;
    }

    ComPtr<IDWriteFactory> dwrite_factory;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(dwrite_factory.GetAddressOf()));
    if (FAILED(hr)) {
        std::fprintf(stderr, "Failed to create DWrite factory: 0x%08lx\n", hr);
        return 1;
    }

    ComPtr<IWICImagingFactory> wic_factory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(wic_factory.GetAddressOf()));
    if (FAILED(hr)) {
        std::fprintf(stderr, "Failed to create WIC factory: 0x%08lx\n", hr);
        return 1;
    }

    // Load theme config
    ThemeService theme;
    theme.load(opts.config_path);

    // Read and parse markdown
    FileService file_service;
    auto content = file_service.read(opts.input_path.c_str());
    if (!content) {
        std::fprintf(stderr, "Failed to read: %ls\n", opts.input_path.c_str());
        return 1;
    }

    MarkdownParser parser;
    auto doc = parser.parse(content->raw_utf8.c_str(), content->raw_utf8.size());

    // Layout
    LayoutEngine layout_engine(dwrite_factory.Get(), theme, opts.dark);
    float viewport_width = static_cast<float>(opts.width);
    auto layout = layout_engine.layout(doc, viewport_width);

    // Determine bitmap dimensions
    int bmp_width = opts.width;
    int bmp_height;
    float scroll_y;

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
        return 1;
    }

    // Paint
    renderer.paint(layout, scroll_y);

    // Save PNG
    hr = renderer.save_to_png(wic_factory.Get(), out_path.c_str());
    if (FAILED(hr)) {
        std::fprintf(stderr, "Failed to save PNG: 0x%08lx\n", hr);
        return 1;
    }

    std::fprintf(stdout, "%ls\n", out_path.c_str());

    CoUninitialize();
    return 0;
}
```

- [ ] **Step 2: Build all targets**

Run:
```bash
cmake --build --preset conan-release
```

Expected: All targets compile successfully.

- [ ] **Step 3: Run existing tests to verify no regressions**

Run:
```bash
./build/Release/tests.exe
```

Expected: All 86 tests pass.

- [ ] **Step 4: Test the screenshot tool with sample.md in viewport mode**

Run:
```bash
./build/Release/screenshot_tool.exe test_data/sample.md
```

Expected: Prints `test_data/sample.png` to stdout. File exists and is a valid PNG showing the top 800x600 of the rendered markdown.

- [ ] **Step 5: Test full-document mode**

Run:
```bash
./build/Release/screenshot_tool.exe test_data/sample.md --full
```

Expected: `test_data/sample.png` is overwritten with a tall image showing the entire document.

- [ ] **Step 6: Test dark mode**

Run:
```bash
./build/Release/screenshot_tool.exe test_data/sample.md --dark
```

Expected: `test_data/sample_dark.png` is created with dark background colors.

- [ ] **Step 7: Test with custom width and scroll**

Run:
```bash
./build/Release/screenshot_tool.exe test_data/sample.md --width 1024 --scroll 200
```

Expected: `test_data/sample.png` shows a 1024x600 viewport scrolled 200px down.

- [ ] **Step 8: Test error handling — missing file**

Run:
```bash
./build/Release/screenshot_tool.exe nonexistent.md
```

Expected: Prints error to stderr, exits with code 1.

- [ ] **Step 9: Commit**

```bash
git add src/screenshot_main.cpp
git commit -m "feat: implement screenshot_tool CLI for offscreen markdown rendering"
```

---

### Task 4: Add .gitignore entry for generated PNGs

**Files:**
- Modify: `.gitignore` (or create if it doesn't exist)

Generated screenshots should not be committed.

- [ ] **Step 1: Add test_data/*.png to .gitignore**

Append to `.gitignore`:

```
# Generated screenshots
test_data/*.png
```

- [ ] **Step 2: Commit**

```bash
git add .gitignore
git commit -m "chore: ignore generated screenshot PNGs in test_data"
```
