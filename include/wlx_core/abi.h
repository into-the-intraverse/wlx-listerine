#ifndef WLX_CORE_ABI_H
#define WLX_CORE_ABI_H

#include <stdint.h>

#ifdef WLX_CORE_BUILDING
#  define WLX_CORE_API __declspec(dllexport)
#else
#  define WLX_CORE_API __declspec(dllimport)
#endif

#define WLX_CORE_ABI_VERSION 6

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
// No-op: the core is a process-wide singleton owned by the core DLL.
// Provided only so the ABI shape is symmetric with acquire().
WLX_CORE_API void      wlx_core_release(WlxCore*);

// Return-code convention across this ABI:
// negative = error (bad argument, uninitialized core, or allocation failure);
// non-negative = result.
WLX_CORE_API int       wlx_core_supports(WlxCore*, const char* language);

WLX_CORE_API int       wlx_core_colorize(WlxCore*,
                                         const char* source, uint32_t len,
                                         const char* language,
                                         int dark_mode,
                                         uint32_t range_start, uint32_t range_end,
                                         WlxColorSpan** out_spans,
                                         uint32_t* out_count);
WLX_CORE_API void      wlx_core_free_spans(WlxColorSpan*);

WLX_CORE_API int       wlx_core_theme_color(WlxCore*,
                                            const char* scope,
                                            int dark_mode,
                                            uint32_t* out_rgb,
                                            uint8_t* out_modifiers);

typedef struct WlxLanguageList {
    char**   ids;        // array of `count` null-terminated UTF-8 strings
    uint32_t count;
    uint32_t _reserved;  // must be zero; reserves the implicit tail-padding
                         // slot so a future flags/version field doesn't grow
                         // the struct (mirrors WlxColorSpan::_pad)
} WlxLanguageList;

// Enumerates the grammars supported by the core. On success, fills *out_list
// with a heap-owned array (must be freed with wlx_core_free_language_list)
// and returns 0. Returns negative on bad arguments, uninitialized core, or
// allocation failure (-1 / -2 respectively, per the convention above).
WLX_CORE_API int  wlx_core_list_languages(WlxCore*, WlxLanguageList* out_list);

// Frees the buffers owned by `*list` and zeros it. Safe to call on an
// already-zeroed list.
WLX_CORE_API void wlx_core_free_language_list(WlxLanguageList* list);

#ifdef __cplusplus
} // extern "C"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <memory>

namespace wlx_core {
    // --- Inline C++ RAII shim for span ownership ---
    struct SpanDeleter {
        void operator()(WlxColorSpan* p) const noexcept { wlx_core_free_spans(p); }
    };
    using SpansPtr = std::unique_ptr<WlxColorSpan, SpanDeleter>;

    // Acquire the core handle, verifying the ABI version matches what this
    // plugin was compiled against. Returns nullptr on version mismatch
    // (logged via OutputDebugStringW); plugins should fall back to plain-
    // text rendering in that case.
    inline WlxCore* acquire_compatible() {
        if (wlx_core_abi_version() != WLX_CORE_ABI_VERSION) {
            ::OutputDebugStringW(
                L"[wlx-core] ABI version mismatch - core unavailable\n");
            return nullptr;
        }
        return wlx_core_acquire();
    }
}
#endif

#endif // WLX_CORE_ABI_H
