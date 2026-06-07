// Note: WLX_CORE_BUILDING is set by CMake target_compile_definitions; do not
// redefine here.

#include "wlx_core/abi.h"
#include "core_dll/registry/core_registry.h"

#include <cstdlib>
#include <cstring>
#include <string_view>

extern "C" WLX_CORE_API int wlx_core_abi_version(void) {
    return WLX_CORE_ABI_VERSION;
}

extern "C" WLX_CORE_API WlxCore* wlx_core_acquire(void) {
    auto& reg = wlx::core::registry::CoreRegistry::instance();
    return reinterpret_cast<WlxCore*>(&reg);
}

extern "C" WLX_CORE_API void wlx_core_release(WlxCore*) {
    // No-op: the core handle is process-singleton, owned by the DLL itself.
    // All acquire() calls return the same pointer; release() is provided only
    // to give the ABI a symmetric shape for future versions.
}

extern "C" WLX_CORE_API int
wlx_core_supports(WlxCore* h, const char* language) {
    if (!h || !language) return -1;
    return reinterpret_cast<wlx::core::registry::CoreRegistry*>(h)->supports(language) ? 1 : 0;
}

extern "C" WLX_CORE_API void
wlx_core_prewarm(WlxCore* h, const char* language) {
    if (!h || !language) return;
    reinterpret_cast<wlx::core::registry::CoreRegistry*>(h)->prewarm(language);
}

extern "C" WLX_CORE_API int
wlx_core_colorize(WlxCore* h,
                  const char* source, uint32_t len,
                  const char* language,
                  int dark_mode,
                  uint32_t range_start, uint32_t range_end,
                  WlxColorSpan** out_spans, uint32_t* out_count) {
    if (!h || !source || !language || !out_spans || !out_count) return -1;
    auto& reg = *reinterpret_cast<wlx::core::registry::CoreRegistry*>(h);

    // View the caller's buffer directly — colorize() is synchronous and reads
    // it only for the duration of this call, so no owning copy is needed.
    auto result = reg.colorize(std::string_view(source, len), language,
                               dark_mode != 0, range_start, range_end);

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

extern "C" WLX_CORE_API WlxTree*
wlx_core_parse(WlxCore* h, const char* source, uint32_t len, const char* language) {
    if (!h || !source || !language) return nullptr;
    auto& reg = *reinterpret_cast<wlx::core::registry::CoreRegistry*>(h);
    return reg.parse_tree(std::string_view(source, len), language);
}

extern "C" WLX_CORE_API int
wlx_core_highlight_range(WlxCore* h, WlxTree* t, int dark_mode,
                         uint32_t range_start, uint32_t range_end,
                         WlxColorSpan** out_spans, uint32_t* out_count) {
    if (!h || !t || !out_spans || !out_count) return -1;
    auto& reg = *reinterpret_cast<wlx::core::registry::CoreRegistry*>(h);

    auto result = reg.highlight_tree_range(t, dark_mode != 0, range_start, range_end);

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

extern "C" WLX_CORE_API void wlx_core_free_tree(WlxCore* h, WlxTree* t) {
    if (!h) return;
    reinterpret_cast<wlx::core::registry::CoreRegistry*>(h)->free_tree(t);
}

extern "C" WLX_CORE_API int
wlx_core_theme_color(WlxCore* h, const char* scope, int dark_mode,
                     uint32_t* out_rgb, uint8_t* out_modifiers) {
    if (!h || !scope || !out_rgb) return -1;
    auto& reg = *reinterpret_cast<wlx::core::registry::CoreRegistry*>(h);
    const wlx::core::theme::HelixTheme& t = reg.theme(dark_mode != 0);
    auto resolved = t.resolve(scope);
    if (!resolved) return -1;
    *out_rgb = resolved->fg;
    if (out_modifiers) *out_modifiers = resolved->modifiers;
    return 0;
}

extern "C" WLX_CORE_API int
wlx_core_list_languages(WlxCore* h, WlxLanguageList* out_list) {
    if (!h || !out_list) return -1;
    auto& reg = *reinterpret_cast<wlx::core::registry::CoreRegistry*>(h);

    auto langs = reg.available_languages();

    if (langs.empty()) {
        out_list->ids = nullptr;
        out_list->count = 0;
        return 0;
    }

    auto** arr = static_cast<char**>(std::malloc(sizeof(char*) * langs.size()));
    if (!arr) return -2;

    for (size_t i = 0; i < langs.size(); ++i) {
        const auto& s = langs[i];
        arr[i] = static_cast<char*>(std::malloc(s.size() + 1));
        if (!arr[i]) {
            // Roll back: free what we've allocated so far.
            for (size_t j = 0; j < i; ++j) std::free(arr[j]);
            std::free(arr);
            return -2;
        }
        std::memcpy(arr[i], s.data(), s.size());
        arr[i][s.size()] = '\0';
    }

    out_list->ids = arr;
    out_list->count = static_cast<uint32_t>(langs.size());
    return 0;
}

extern "C" WLX_CORE_API void
wlx_core_free_language_list(WlxLanguageList* list) {
    if (!list) return;
    if (list->ids) {
        for (uint32_t i = 0; i < list->count; ++i)
            std::free(list->ids[i]);
        std::free(list->ids);
    }
    list->ids = nullptr;
    list->count = 0;
}
