#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>

#include "tools/screenshot/options.h"
#include "tools/screenshot/colorizer_pipeline.h"
#include "tools/screenshot/markdown_pipeline.h"

namespace wlx::tools::screenshot {

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
        "  --lazy           Use lazy layout + viewport materialize (markdown only)\n"
        "  --search <term>  Run a search for <term> after layout\n"
        "                   (with --lazy, the index covers only materialized blocks)\n"
        "  --search-step N  Advance the search cursor by N steps (default 0)\n"
        "  --colorizer           Force colorizer mode (else inferred from extension)\n"
        "  --lang <id>           Override language id (else inferred from extension)\n"
        "  --dump-tokens         Write resolved-style token JSON instead of painting\n"
        "  --display-config <p>  TOML overrides for ColorizerDisplayConfig\n");
}

// Strict numeric parsing: the whole value must be consumed, so e.g.
// "--scroll abc" errors instead of silently becoming 0.
static bool parse_int(const wchar_t* flag, const wchar_t* s, int& out) {
    wchar_t* end = nullptr;
    long v = std::wcstol(s, &end, 10);
    if (end == s || *end != L'\0') {
        std::fprintf(stderr, "Invalid value for %ls: %ls\n", flag, s);
        return false;
    }
    out = static_cast<int>(v);
    return true;
}

static bool parse_float(const wchar_t* flag, const wchar_t* s, float& out) {
    wchar_t* end = nullptr;
    double v = std::wcstod(s, &end);
    if (end == s || *end != L'\0') {
        std::fprintf(stderr, "Invalid value for %ls: %ls\n", flag, s);
        return false;
    }
    out = static_cast<float>(v);
    return true;
}

// Case-insensitive markdown-extension check (.md / .markdown), so FILE.MD
// routes to the markdown pipeline instead of erroring in the colorizer.
static bool has_markdown_ext(const std::wstring& path) {
    auto dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot + 1);
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    return ext == L"md" || ext == L"markdown";
}

static bool parse_args(int argc, wchar_t* argv[], Options& opts) {
    if (argc < 2) return false;
    opts.input_path = argv[1];
    for (int i = 2; i < argc; ++i) {
        const wchar_t* arg = argv[i];
        // Value of a value-taking flag; reports "missing value" (distinct
        // from "Unknown option") when the flag is last on the command line.
        auto value = [&]() -> const wchar_t* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Missing value for %ls\n", arg);
                return nullptr;
            }
            return argv[++i];
        };
        if      (std::wcscmp(arg, L"--width")       == 0) { auto v = value(); if (!v || !parse_int(arg, v, opts.width))  return false; }
        else if (std::wcscmp(arg, L"--height")      == 0) { auto v = value(); if (!v || !parse_int(arg, v, opts.height)) return false; }
        else if (std::wcscmp(arg, L"--scroll")      == 0) { auto v = value(); if (!v || !parse_float(arg, v, opts.scroll)) return false; }
        else if (std::wcscmp(arg, L"--config")      == 0) { auto v = value(); if (!v) return false; opts.config_path = v; opts.config_path_explicit = true; }
        else if (std::wcscmp(arg, L"--full")        == 0) opts.full = true;
        else if (std::wcscmp(arg, L"--dark")        == 0) opts.dark = true;
        else if (std::wcscmp(arg, L"--bench")       == 0) opts.bench = true;
        else if (std::wcscmp(arg, L"--lazy")        == 0) opts.lazy = true;
        else if (std::wcscmp(arg, L"--search")      == 0) { auto v = value(); if (!v) return false; opts.search = v; }
        else if (std::wcscmp(arg, L"--search-step") == 0) { auto v = value(); if (!v || !parse_int(arg, v, opts.search_step)) return false; }
        else if (std::wcscmp(arg, L"--colorizer")     == 0) opts.colorizer = true;
        else if (std::wcscmp(arg, L"--lang")          == 0) { auto v = value(); if (!v) return false; opts.lang = v; }
        else if (std::wcscmp(arg, L"--dump-tokens")   == 0) opts.dump_tokens = true;
        else if (std::wcscmp(arg, L"--display-config")== 0) { auto v = value(); if (!v) return false; opts.display_config = v; }
        else { std::fprintf(stderr, "Unknown option: %ls\n", arg); return false; }
    }
    if (opts.width <= 0 || opts.height <= 0) {
        std::fprintf(stderr, "Width and height must be positive integers\n");
        return false;
    }
    return true;
}

}  // namespace wlx::tools::screenshot

using namespace wlx::tools::screenshot;

// wmain so argv arrives as UTF-16: MSVC's narrow main() receives ANSI-codepage
// bytes, which would corrupt any non-ASCII path or argument. The MSVC linker
// picks wmainCRTStartup automatically for console exes — no CMake change.
int wmain(int argc, wchar_t* argv[]) {
    Options opts;
    if (!parse_args(argc, argv, opts)) {
        print_usage();
        return 1;
    }

    HRESULT hr_com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr_com)) {
        std::fprintf(stderr, "Failed to initialize COM: 0x%08lx\n", hr_com);
        return 1;
    }

    bool is_md = !opts.colorizer && has_markdown_ext(opts.input_path);

    std::wstring result = is_md ? run_markdown_pipeline(opts)
                                : run_colorizer_pipeline(opts);

    CoUninitialize();

    if (result.empty()) return 1;
    std::fprintf(stdout, "%ls\n", result.c_str());
    return 0;
}
