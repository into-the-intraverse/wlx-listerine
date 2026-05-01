// WLX_CORE_BUILDING is already defined on the command line by CMake for the
// wlx-listerine-core target; do not redefine here.
#include "wlx_core/abi.h"
#include "colorizer.h"
#include "helix_theme.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

// Task 2: caller-managed lifecycle backed by a single static Colorizer.
// Task 3 will replace this with a CoreRegistry singleton + GetModuleFileNameW.

namespace {
struct Core {
    std::mutex mu;
    std::unique_ptr<Colorizer> colorizer;
};
Core& g_core() {
    static Core c;
    return c;
}
} // namespace

// Internal hook used by the host_adapter migration in Task 2. Removed in Task 3.
extern "C" WLX_CORE_API void
wlx_core_install_for_task2(std::unique_ptr<Colorizer> c) {
    auto& core = g_core();
    std::lock_guard<std::mutex> lk(core.mu);
    if (!core.colorizer) core.colorizer = std::move(c);
}

extern "C" WLX_CORE_API int wlx_core_abi_version(void) {
    return WLX_CORE_ABI_VERSION;
}

extern "C" WLX_CORE_API WlxCore* wlx_core_acquire(void) {
    auto& core = g_core();
    std::lock_guard<std::mutex> lk(core.mu);
    if (!core.colorizer) return nullptr;
    return reinterpret_cast<WlxCore*>(&core);
}

extern "C" WLX_CORE_API void wlx_core_release(WlxCore*) {
    // No-op: the core handle is process-singleton, owned by the DLL itself.
    // All acquire() calls return the same pointer; release() is provided only
    // to give the ABI a symmetric shape for future versions.
}

extern "C" WLX_CORE_API int
wlx_core_supports(WlxCore* h, const char* language) {
    if (!h || !language) return -1;
    auto& core = *reinterpret_cast<Core*>(h);
    std::lock_guard<std::mutex> lk(core.mu);
    if (!core.colorizer) return -1;
    return core.colorizer->supports(language) ? 1 : 0;
}

extern "C" WLX_CORE_API int
wlx_core_colorize(WlxCore* h,
                  const char* source, uint32_t len,
                  const char* language,
                  int dark_mode,
                  WlxColorSpan** out_spans, uint32_t* out_count) {
    if (!h || !source || !language || !out_spans || !out_count) return -1;
    auto& core = *reinterpret_cast<Core*>(h);
    std::lock_guard<std::mutex> lk(core.mu);
    if (!core.colorizer) return -1;

    std::string src(source, len);
    auto result = core.colorizer->colorize(src, language, dark_mode != 0);

    if (result.spans.empty()) {
        *out_spans = nullptr;
        *out_count = 0;
        return 0;
    }

    auto* arr = static_cast<WlxColorSpan*>(
        std::malloc(sizeof(WlxColorSpan) * result.spans.size()));
    if (!arr) return -2;

    for (size_t i = 0; i < result.spans.size(); ++i) {
        const auto& s = result.spans[i];
        arr[i].start     = s.start;
        arr[i].length    = s.length;
        arr[i].color     = s.color;
        arr[i].bg_color  = s.bg_color;
        arr[i].has_bg    = s.has_bg ? 1 : 0;
        arr[i].modifiers = s.modifiers;
        arr[i]._pad[0]   = 0;
        arr[i]._pad[1]   = 0;
    }
    *out_spans = arr;
    *out_count = static_cast<uint32_t>(result.spans.size());
    return 0;
}

extern "C" WLX_CORE_API void wlx_core_free_spans(WlxColorSpan* spans) {
    std::free(spans);
}

extern "C" WLX_CORE_API int
wlx_core_theme_color(WlxCore* h, const char* scope, int dark_mode,
                     uint32_t* out_rgb, uint8_t* out_modifiers) {
    if (!h || !scope || !out_rgb) return -1;
    auto& core = *reinterpret_cast<Core*>(h);
    std::lock_guard<std::mutex> lk(core.mu);
    if (!core.colorizer) return -1;
    const HelixTheme& t = core.colorizer->theme(dark_mode != 0);
    auto resolved = t.resolve(scope);
    if (!resolved) return -1;
    *out_rgb = resolved->fg;
    if (out_modifiers) *out_modifiers = resolved->modifiers;
    return 0;
}
