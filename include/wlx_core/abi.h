#ifndef WLX_CORE_ABI_H
#define WLX_CORE_ABI_H

#include <stdint.h>

#ifdef WLX_CORE_BUILDING
#  define WLX_CORE_API __declspec(dllexport)
#else
#  define WLX_CORE_API __declspec(dllimport)
#endif

#define WLX_CORE_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WlxCore WlxCore;

typedef struct WlxColorSpan {
    uint32_t start;
    uint32_t length;
    uint32_t color;
    uint32_t bg_color;
    uint8_t  has_bg;
    uint8_t  modifiers;
    uint8_t  _pad[2];
} WlxColorSpan;

WLX_CORE_API int       wlx_core_abi_version(void);

WLX_CORE_API WlxCore*  wlx_core_acquire(void);
WLX_CORE_API void      wlx_core_release(WlxCore*);

// Return-code convention across this ABI:
// negative = bad argument or core not initialized; non-negative = result.
WLX_CORE_API int       wlx_core_supports(WlxCore*, const char* language);

WLX_CORE_API int       wlx_core_colorize(WlxCore*,
                                         const char* source, uint32_t len,
                                         const char* language,
                                         int dark_mode,
                                         WlxColorSpan** out_spans,
                                         uint32_t* out_count);
WLX_CORE_API void      wlx_core_free_spans(WlxColorSpan*);

WLX_CORE_API int       wlx_core_theme_color(WlxCore*,
                                            const char* scope,
                                            int dark_mode,
                                            uint32_t* out_rgb,
                                            uint8_t* out_modifiers);

#ifdef __cplusplus
} // extern "C"

// --- Inline C++ RAII shim for span ownership ---
#include <memory>
namespace wlx_core {
    struct SpanDeleter {
        void operator()(WlxColorSpan* p) const noexcept { wlx_core_free_spans(p); }
    };
    using SpansPtr = std::unique_ptr<WlxColorSpan, SpanDeleter>;
}
#endif

#endif // WLX_CORE_ABI_H
