#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
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
        "  --search <term>  Run a search for <term> after layout\n"
        "  --search-step N  Advance the search cursor by N steps (default 0)\n"
        "  --colorizer           Force colorizer mode (else inferred from extension)\n"
        "  --lang <id>           Override grammar language (else inferred from extension)\n"
        "  --cpp-grammar <kind>  \"standard\" or \"unreal\" — selects cpp grammar variant\n"
        "  --dump-tokens         Write resolved-style token JSON instead of painting\n"
        "  --display-config <p>  TOML overrides for ColorizerDisplayConfig\n");
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
        if      (std::strcmp(argv[i], "--width")       == 0 && i + 1 < argc) opts.width  = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height")      == 0 && i + 1 < argc) opts.height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--scroll")      == 0 && i + 1 < argc) opts.scroll = static_cast<float>(std::atof(argv[++i]));
        else if (std::strcmp(argv[i], "--config")      == 0 && i + 1 < argc) opts.config_path = to_wstring(argv[++i]);
        else if (std::strcmp(argv[i], "--full")        == 0) opts.full = true;
        else if (std::strcmp(argv[i], "--dark")        == 0) opts.dark = true;
        else if (std::strcmp(argv[i], "--bench")       == 0) opts.bench = true;
        else if (std::strcmp(argv[i], "--search")      == 0 && i + 1 < argc) opts.search = to_wstring(argv[++i]);
        else if (std::strcmp(argv[i], "--search-step") == 0 && i + 1 < argc) opts.search_step = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--colorizer")     == 0) opts.colorizer = true;
        else if (std::strcmp(argv[i], "--lang")          == 0 && i + 1 < argc) opts.lang = to_wstring(argv[++i]);
        else if (std::strcmp(argv[i], "--cpp-grammar")   == 0 && i + 1 < argc) opts.cpp_grammar = to_wstring(argv[++i]);
        else if (std::strcmp(argv[i], "--dump-tokens")   == 0) opts.dump_tokens = true;
        else if (std::strcmp(argv[i], "--display-config")== 0 && i + 1 < argc) opts.display_config = to_wstring(argv[++i]);
        else { std::fprintf(stderr, "Unknown option: %s\n", argv[i]); return false; }
    }
    if (opts.width <= 0 || opts.height <= 0) {
        std::fprintf(stderr, "Width and height must be positive integers\n");
        return false;
    }
    return true;
}

}  // namespace wlx::tools::screenshot

using namespace wlx::tools::screenshot;

int main(int argc, char* argv[]) {
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

    bool is_md = !opts.colorizer && opts.input_path.ends_with(L".md");

    std::wstring result = is_md ? run_markdown_pipeline(opts)
                                : run_colorizer_pipeline(opts);

    CoUninitialize();

    if (result.empty()) return 1;
    std::fprintf(stdout, "%ls\n", result.c_str());
    return 0;
}
