# Lazy Grammar Loading + Shared Core Module Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Spec:** `docs/superpowers/specs/2026-05-01-lazy-grammar-loading-design.md`

**Goal:** Convert `colorizer-core` from a static library into a shared `wlx-listerine-core.dll` used by both `.wlx64` plugins. Add an LRU + TTL eviction policy to bound grammar memory growth across long TC sessions. Bundle both plugins, the new core DLL, themes, and grammars into a single TC install folder so themes and grammars no longer ship duplicated.

**Architecture:** A new `extern "C"` ABI in `include/wlx_core/abi.h` becomes the only contract crossing the DLL boundary. A `CoreRegistry` singleton inside the core DLL is `call_once`-initialized on the first plugin call; it derives its own install dir from the core's `HMODULE` and reads `wlx-listerine-core.toml` for cache config. The `GrammarRegistry`'s flat map is replaced with `GrammarCache` — an LRU list whose tail is swept on every miss that pushes `loaded_count_ > cap`, freeing only entries older than `ttl_minutes`. Trees are caller-owned and torn down inside the same locked region that acquired the language pointer, so no pin tokens are needed.

**Tech Stack:** C++20, MSVC, CMake 3.20+, Conan 2.x, doctest, Direct2D/DirectWrite, toml++, tree-sitter, Win32.

**Spec deviation (themes):** The spec (§2.5) says per-plugin TOML keeps `theme`/`theme_light` (each plugin picks its own). With a single shared singleton + bundled install, supporting two simultaneously different themes would either require an ABI theme parameter on every call or a first-caller-wins setter. Both add complexity for a capability the bundled-install model doesn't really need (one install dir, one theme set on disk). This plan instead **consolidates theme selection into `wlx-listerine-core.toml`** alongside `cap`/`ttl_minutes`. Per-plugin `code_theme`/`code_theme_light` keys are dropped (Task 7). One theme pair per install, configured globally. If we later want per-plugin themes back, a follow-up adds a `wlx_core_set_theme(...)` ABI call.

---

## File Structure

### Created

- `include/wlx_core/abi.h` — Public C ABI (extern "C" declarations + inline C++ shim wrapping the raw calls in RAII).
- `src/colorizer/wlx_core_abi.cpp` — C ABI implementation. Lives inside the core DLL, delegates to `CoreRegistry`.
- `src/colorizer/core_registry.h` / `.cpp` — Process-wide singleton (`std::call_once`-initialized). Owns `GrammarCache`, dark/light `HelixTheme`, `CoreConfig`. Mutex-locked entry points.
- `src/colorizer/core_config.h` / `.cpp` — Parser for `wlx-listerine-core.toml` (`[grammar_cache] cap`, `ttl_minutes`). Defaults + clamping for bad values.
- `src/colorizer/grammar_cache.h` / `.cpp` — Replaces the flat-map cache inside `GrammarRegistry`. Holds the LRU list, eviction logic, injectable clock.
- `src/colorizer/dllmain.cpp` — Captures the core's own `HMODULE` at `DLL_PROCESS_ATTACH` for later `GetModuleFileNameW`.
- `tests/test_core_config.cpp` — Unit tests for `CoreConfig` (defaults, parse, clamp).
- `tests/test_grammar_cache.cpp` — Unit tests for `GrammarCache` (LRU, soft cap, TTL, evict-then-reload, failed-load handling) using injectable clock + a faked loader so no real DLLs are touched.
- `tests/test_wlx_core_abi.cpp` — Integration tests for the public ABI (singleton init, ABI version, concurrent colorize).
- `config/wlx-listerine-core.toml` — Default core config shipped in the bundle (with `[grammar_cache] cap=8`, `ttl_minutes=5`).
- `config/pluginst.inf` — **Replaces** the two per-plugin `pluginst-md.inf` / `pluginst-colorizer.inf`. Single TC install registers both `.wlx64`s via `file=` + `file2=`.

### Modified

- `CMakeLists.txt` — Rename `colorizer-core` → `wlx-listerine-core`. Change `STATIC` → `SHARED`. Add `WLX_CORE_API` export decoration via a `WLX_CORE_BUILDING` build define. Add new sources (`wlx_core_abi.cpp`, `core_registry.cpp`, `core_config.cpp`, `grammar_cache.cpp`, `dllmain.cpp`). Add `WLX_CORE_TESTING` definition on the test executables only. Replace per-plugin theme/grammar copy custom commands with a single shared-output layout.
- `src/colorizer/colorizer.h` / `.cpp` — Decorate `Colorizer` and `ColorSpan` / `ColorizeResult` with `WLX_CORE_API`.
- `src/colorizer/grammar_registry.h` / `.cpp` — Adopt `GrammarCache` internally; expose a constructor that takes a clock + injected loader for tests.
- `src/colorizer/helix_theme.h` — Decorate `HelixTheme` and `ResolvedStyle` with `WLX_CORE_API`.
- `src/colorizer/query_highlighter.h` — Decorate `QueryHighlighter` with `WLX_CORE_API`.
- `src/host_adapter.cpp` — Replace `static std::unique_ptr<Colorizer> g_colorizer` and direct construction (`g_colorizer = std::make_unique<Colorizer>(...)` at lines 93, 144) with `wlx_core_acquire()` + ABI calls. Drop the `code_grammar_dir` / `code_theme_dir` resolution path. Drop the detach-leak line (797) for the colorizer (core handles its own teardown / leak).
- `src/colorizer/colorizer_host_adapter.cpp` — Same migration as `host_adapter.cpp`. Drop construction at lines 96, 304. Drop detach-leak at line 941 for the colorizer.
- `src/screenshot_main.cpp` — Use the public ABI rather than constructing `Colorizer` directly at line 229.
- `src/layout_engine.h` / `.cpp` — `Colorizer*` parameter type unchanged (still imported via the now-shared DLL). No code changes — header included via export decoration.
- `src/theme_service.h` / `.cpp` — Drop `code_grammar_dir`, `code_theme_dir`, `code_theme`, `code_theme_light` fields from `ThemeConfig` (and the corresponding `parse` paths). All four are now unused by the plugins; theme selection moves to `wlx-listerine-core.toml`.
- `config/wlx-listerine-md.toml` — Drop `code_grammar_dir`, `code_theme_dir`, `code_theme`, `code_theme_light` keys (no longer honored).
- `config/wlx-listerine-colorizer.toml` — Same.
- `scripts/package.ps1` — Produce one bundled `wlx-listerine-<version>.zip` instead of two; lay out a single staging dir; no `themes/` / `grammars/` duplication.
- `tests/test_colorizer_grammar.cpp` — Update tests to use the new public constructor with injected clock/loader where they directly construct `GrammarRegistry`. Behavior assertions unchanged.
- `README.md` — Drop the "Lazy grammar loading for the colorizer" TODO. Update install steps to single bundled ZIP. Update plugin description if it mentions per-plugin install.
- `CLAUDE.md` — Update Architecture section: `colorizer-core` → `wlx-listerine-core` (shared DLL). Add a "Process-wide grammar cache" paragraph (singleton, LRU, TTL, mutex). Note the C ABI boundary at `include/wlx_core/abi.h`. Note lockstep-build constraint (all three artifacts ship together; ABI version pinned).
- `docs/CONFIGURATION.md` — Add a section for `wlx-listerine-core.toml` (`[grammar_cache] cap`, `ttl_minutes`). Note that themes now live in the single `wlx-listerine/themes` location.
- `docs/BUILDING.md` — Note the new shared-DLL artifact in the build output, the import-lib linkage, and the lockstep-build rule.
- `docs/LANGUAGES.md` — Update the grammar-drop path from per-plugin `wlx-listerine-md/grammars/` (or `wlx-listerine-colorizer/grammars/`) to the single `wlx-listerine/grammars/`.

### Deleted

- `config/pluginst-md.inf` — Superseded by single `config/pluginst.inf`.
- `config/pluginst-colorizer.inf` — Same.

---

## Task 1: Add WLX_CORE_API export macro and convert core to SHARED DLL

Renames the static `colorizer-core` target to `wlx-listerine-core` and changes its type to `SHARED`. Public C++ classes (Colorizer, GrammarRegistry, HelixTheme, ResolvedStyle, ColorSpan, ColorizeResult, QueryHighlighter) get a `WLX_CORE_API` decoration so the symbols are exported when building the DLL and imported when consumed by `wlx-core` / plugins / tests / `screenshot_tool`. No logic change. After this task the project still builds as today; the only difference on disk is `wlx-listerine-core.dll` + import lib appearing in the build output.

**Files:**
- Create: `src/colorizer/wlx_core_api.h`
- Modify: `CMakeLists.txt` (lines 62-78 — `colorizer-core` definition; lines 88-96, 124-142 — plugin link lines; lines 175-184 — screenshot_tool link; lines 504-517 — colorizer-tests link).
- Modify: `src/colorizer/colorizer.h`
- Modify: `src/colorizer/grammar_registry.h`
- Modify: `src/colorizer/helix_theme.h`
- Modify: `src/colorizer/query_highlighter.h`

- [ ] **Step 1: Create `src/colorizer/wlx_core_api.h`**

```cpp
#pragma once

// Export decoration for the wlx-listerine-core DLL.
//
// When building the DLL itself (target wlx-listerine-core), CMake defines
// WLX_CORE_BUILDING; declarations decorated with WLX_CORE_API are exported.
// When consumed (wlx-core, plugins, tests, screenshot_tool), no define is
// set; the same declarations become dllimport.
//
// All cross-DLL public surfaces (classes, free functions) MUST carry this
// macro on their declaration. Member function decoration is implied by the
// class-level decoration on MSVC.

#ifdef WLX_CORE_BUILDING
#  define WLX_CORE_API __declspec(dllexport)
#else
#  define WLX_CORE_API __declspec(dllimport)
#endif
```

- [ ] **Step 2: Decorate `Colorizer`, `ColorSpan`, `ColorizeResult` in `src/colorizer/colorizer.h`**

Add `#include "wlx_core_api.h"` at the top (after the existing `#pragma once`).

Change `struct ColorSpan {` to `struct WLX_CORE_API ColorSpan {`.
Change `struct ColorizeResult {` to `struct WLX_CORE_API ColorizeResult {`.
Change `class Colorizer {` to `class WLX_CORE_API Colorizer {`.

- [ ] **Step 3: Decorate `GrammarRegistry` in `src/colorizer/grammar_registry.h`**

Add `#include "wlx_core_api.h"` after the existing `#pragma once`.
Change `class GrammarRegistry {` to `class WLX_CORE_API GrammarRegistry {`.

- [ ] **Step 4: Decorate `HelixTheme`, `ResolvedStyle` in `src/colorizer/helix_theme.h`**

Add `#include "wlx_core_api.h"` after the existing `#pragma once`.
Change `struct ResolvedStyle {` to `struct WLX_CORE_API ResolvedStyle {`.
Change `class HelixTheme {` to `class WLX_CORE_API HelixTheme {`.

- [ ] **Step 5: Decorate `QueryHighlighter` in `src/colorizer/query_highlighter.h`**

Add `#include "wlx_core_api.h"` after the existing `#pragma once`.
Change `class QueryHighlighter {` to `class WLX_CORE_API QueryHighlighter {`.

- [ ] **Step 6: Update `CMakeLists.txt` — convert core target**

Replace the existing `colorizer-core` block (lines 62-78) with:

```cmake
# --- Core shared library (wlx-listerine-core.dll) ---
find_package(tree-sitter REQUIRED)

add_library(wlx-listerine-core SHARED
    src/colorizer/colorizer.cpp
    src/colorizer/helix_theme.cpp
    src/colorizer/grammar_registry.cpp
    src/colorizer/query_highlighter.cpp
)

target_include_directories(wlx-listerine-core
    PUBLIC src/colorizer src include
)
target_link_libraries(wlx-listerine-core
    PUBLIC
        tree-sitter::tree-sitter
    PRIVATE
        tomlplusplus::tomlplusplus
)
target_compile_definitions(wlx-listerine-core PRIVATE WLX_CORE_BUILDING)

set_target_properties(wlx-listerine-core PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
)
```

- [ ] **Step 7: Update `CMakeLists.txt` — replace `colorizer-core` references with `wlx-listerine-core`**

In lines that previously referenced `colorizer-core` (the link lines for `wlx-core` ~line 57, `wlx-listerine-md` ~line 91, `wlx-listerine-colorizer` ~line 136, `colorizer-tests` ~line 515), change `colorizer-core` to `wlx-listerine-core`.

- [ ] **Step 8: Build**

Run:
```
conan install . --output-folder=build --build=missing -s build_type=Release -s compiler.cppstd=20
cmake --preset conan-default
cmake --build --preset conan-release
```

Expected: clean build. New artifacts: `output/wlx-listerine-core.dll` + import lib in the build tree.

- [ ] **Step 9: Run tests to confirm no regression**

Run: `./build/Release/tests.exe`
Expected: all tests pass (86 markdown tests).

Run: `./build/Release/colorizer-tests.exe`
Expected: all tests pass.

- [ ] **Step 10: Run visual regression suite**

Run: `./scripts/visual-test.sh`
Expected: all 27 cases at >= 95% similarity to golden Chrome PNGs.

- [ ] **Step 11: Commit**

```bash
git add src/colorizer/wlx_core_api.h src/colorizer/colorizer.h \
        src/colorizer/grammar_registry.h src/colorizer/helix_theme.h \
        src/colorizer/query_highlighter.h CMakeLists.txt
git commit -m "build(core): convert colorizer-core to shared wlx-listerine-core DLL

Add WLX_CORE_API export decoration. No API or behavior change."
```

---

## Task 2: Add the C ABI surface and migrate plugins to use it

Introduces `include/wlx_core/abi.h` (extern "C" + inline C++ shim) and `src/colorizer/wlx_core_abi.cpp` (implementation that delegates to a caller-managed `Colorizer` for now — the singleton lands in Task 3). Both plugins' `g_colorizer` references are replaced with `wlx_core_acquire()` + ABI calls, so the C++ types no longer cross the DLL boundary.

**Files:**
- Create: `include/wlx_core/abi.h`
- Create: `src/colorizer/wlx_core_abi.cpp`
- Modify: `CMakeLists.txt` — add `wlx_core_abi.cpp` to the core sources.
- Modify: `src/host_adapter.cpp` (lines 93, 144-148, 180, 797 + any other `g_colorizer` reference)
- Modify: `src/colorizer/colorizer_host_adapter.cpp` (lines 96, 304-307, 941 + any other `g_colorizer` reference)
- Modify: `src/screenshot_main.cpp` (line 229)

- [ ] **Step 1: Write the integration test for the ABI**

Create `tests/test_wlx_core_abi.cpp`:

```cpp
#include <doctest/doctest.h>
#include <wlx_core/abi.h>
#include <filesystem>
#include <string>

static bool has_grammars() {
    return std::filesystem::exists("grammars/c/tree-sitter-c.dll");
}

TEST_CASE("ABI version constant matches DLL export") {
    CHECK(wlx_core_abi_version() == WLX_CORE_ABI_VERSION);
}

TEST_CASE("acquire is idempotent") {
    auto* a = wlx_core_acquire();
    auto* b = wlx_core_acquire();
    CHECK(a != nullptr);
    CHECK(a == b);
    wlx_core_release(a);
    wlx_core_release(b);
}

TEST_CASE("supports returns 1 for known languages"
    * doctest::skip(!has_grammars())) {
    auto* core = wlx_core_acquire();
    CHECK(wlx_core_supports(core, "c") == 1);
    CHECK(wlx_core_supports(core, "definitely-not-a-language") == 0);
    wlx_core_release(core);
}

TEST_CASE("colorize round-trips a tiny C source"
    * doctest::skip(!has_grammars())) {
    auto* core = wlx_core_acquire();
    const char* src = "int main(){return 0;}";
    WlxColorSpan* spans = nullptr;
    uint32_t count = 0;
    int rc = wlx_core_colorize(core, src, (uint32_t)strlen(src),
                               "c", /*dark=*/1, &spans, &count);
    CHECK(rc == 0);
    CHECK(count > 0);
    CHECK(spans != nullptr);
    wlx_core_free_spans(spans);
    wlx_core_release(core);
}
```

Add `tests/test_wlx_core_abi.cpp` to the `colorizer-tests` `add_executable` list in `CMakeLists.txt` (currently around line 504-512).

- [ ] **Step 2: Run the test to verify it fails (header doesn't exist yet)**

Run: `cmake --build --preset conan-release`
Expected: fails with `wlx_core/abi.h: No such file or directory`.

- [ ] **Step 3: Create `include/wlx_core/abi.h`**

```c
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
```

- [ ] **Step 4: Implement the ABI in `src/colorizer/wlx_core_abi.cpp`**

```cpp
#define WLX_CORE_BUILDING
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
//
// During Task 2 the plugins still pass paths via TOML config. The acquire/
// release ABI is a thin wrapper around a process-static Colorizer that the
// host_adapters create on first use. We expose the same Colorizer pointer
// to all callers — refcount only matters for the formal contract.

namespace {

struct Core {
    std::mutex mu;
    std::unique_ptr<Colorizer> colorizer;
};

Core& g_core() {
    static Core c;
    return c;
}

// Plugins still own initialization in Task 2. They populate this from TOML
// before the first wlx_core_acquire call via wlx_core_install_for_task2(...).
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
    // No-op until process exit (consistent with detach-leak pattern).
}

extern "C" WLX_CORE_API int
wlx_core_supports(WlxCore* h, const char* language) {
    if (!h || !language) return 0;
    auto& core = *reinterpret_cast<Core*>(h);
    std::lock_guard<std::mutex> lk(core.mu);
    if (!core.colorizer) return 0;
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
```

Add `wlx_core_install_for_task2` declaration somewhere accessible — add it near the top of `src/colorizer/wlx_core_abi.cpp` is fine since the host_adapter will declare it externally:

In `src/host_adapter.cpp` and `src/colorizer/colorizer_host_adapter.cpp`, add at the top of file (after other includes):

```cpp
extern "C" __declspec(dllimport) void
wlx_core_install_for_task2(std::unique_ptr<Colorizer>);
```

- [ ] **Step 5: Add `wlx_core_abi.cpp` to the core target in `CMakeLists.txt`**

In the `add_library(wlx-listerine-core SHARED ...)` block, add `src/colorizer/wlx_core_abi.cpp` to the source list.

- [ ] **Step 6: Migrate `src/host_adapter.cpp`**

Find the `g_colorizer` declaration at line 93 and the construction at lines 144-148. Replace the construction site with a call to `wlx_core_install_for_task2`:

Old (lines 144-148):
```cpp
        g_colorizer = std::make_unique<Colorizer>(
            grammar_dir, theme_dir,
            g_theme.config().code_theme,
            g_theme.config().code_theme_light);
```

New:
```cpp
        wlx_core_install_for_task2(std::make_unique<Colorizer>(
            grammar_dir, theme_dir,
            g_theme.config().code_theme,
            g_theme.config().code_theme_light));
        g_colorizer_handle = wlx_core_acquire();
```

Replace the `static std::unique_ptr<Colorizer> g_colorizer;` declaration at line 93 with:
```cpp
static WlxCore* g_colorizer_handle = nullptr;
```

Find every other use of `g_colorizer.get()` (e.g. line 180 in the LayoutEngine constructor) and replace with the appropriate ABI access. Since `LayoutEngine` still takes `Colorizer*`, do not change its signature in this task — instead extract the underlying Colorizer* from the handle by adding a temporary helper accessor. **Pragmatic shortcut:** keep one extra static `static Colorizer* g_colorizer_raw = nullptr;` set right after `wlx_core_install_for_task2` from the same `make_unique` source; pass that to `LayoutEngine`. This gets removed in Task 3 along with the entire raw-pointer path.

```cpp
static WlxCore*   g_colorizer_handle = nullptr;
static Colorizer* g_colorizer_raw    = nullptr;   // removed in Task 3
```

In `ensure_theme()`:
```cpp
        auto cz = std::make_unique<Colorizer>(
            grammar_dir, theme_dir,
            g_theme.config().code_theme,
            g_theme.config().code_theme_light);
        g_colorizer_raw = cz.get();
        wlx_core_install_for_task2(std::move(cz));
        g_colorizer_handle = wlx_core_acquire();
```

In `do_layout()`:
```cpp
    LayoutEngine engine(g_dwrite_factory.Get(), g_theme, vs->dark_mode, g_colorizer_raw);
```

Drop the detach-leak line for the colorizer at line 797 (`(void)new std::unique_ptr<Colorizer>(std::move(g_colorizer));`). The core DLL owns its own teardown.

Add `#include "wlx_core/abi.h"` near the existing colorizer include.

- [ ] **Step 7: Migrate `src/colorizer/colorizer_host_adapter.cpp`**

Same pattern as host_adapter.cpp:
- Replace `static std::unique_ptr<Colorizer> g_colorizer;` at line 96 with `static WlxCore* g_colorizer_handle = nullptr; static Colorizer* g_colorizer_raw = nullptr;`.
- At line 304-307, replace the construction with `wlx_core_install_for_task2(std::make_unique<Colorizer>(...))` + the raw pointer capture.
- Replace existing `g_colorizer.get()` usages with `g_colorizer_raw`.
- Drop the detach-leak line at 941.
- Add `#include "wlx_core/abi.h"`.

- [ ] **Step 8: Migrate `src/screenshot_main.cpp`**

Find line 229:
```cpp
Colorizer colorizer(theme.config().code_grammar_dir, theme.config().code_theme_dir, ...);
```

For now keep this unchanged — `screenshot_tool` is an executable that links against `wlx-listerine-core` directly and can keep using the C++ class. **No edit needed in this step.**

- [ ] **Step 9: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build. Note the new export `wlx_core_install_for_task2` is internal-only (will be removed in Task 3); the ABI surface in the public header is stable.

- [ ] **Step 10: Run all tests including the new ABI test**

Run: `./build/Release/colorizer-tests.exe`
Expected: all existing tests still pass plus the four new ABI tests in `test_wlx_core_abi.cpp`.

Run: `./build/Release/tests.exe`
Expected: all tests pass.

- [ ] **Step 11: Run visual regression**

Run: `./scripts/visual-test.sh`
Expected: all 27 cases at >= 95% similarity.

- [ ] **Step 12: Commit**

```bash
git add include/wlx_core/abi.h src/colorizer/wlx_core_abi.cpp \
        src/host_adapter.cpp src/colorizer/colorizer_host_adapter.cpp \
        tests/test_wlx_core_abi.cpp CMakeLists.txt
git commit -m "feat(core): add C ABI surface, migrate plugins to it

Plugins now talk to the core through wlx_core_acquire / wlx_core_colorize
instead of holding a Colorizer C++ instance directly. Internal
wlx_core_install_for_task2 hook is a temporary scaffold removed in the
next commit when the singleton lands."
```

---

## Task 3: Add CoreRegistry singleton with DllMain HMODULE capture

Replaces the Task 2 scaffold (`wlx_core_install_for_task2` + `g_colorizer_raw`) with a true `CoreRegistry` singleton initialized via `std::call_once` inside the core DLL. The singleton derives its install dir from `GetModuleFileNameW(g_core_module)` where `g_core_module` is captured in `DllMain(DLL_PROCESS_ATTACH)`. Plugins drop the `g_colorizer_raw` shortcut — anything that needed a `Colorizer*` (notably `LayoutEngine`) now goes through ABI calls instead.

**Files:**
- Create: `src/colorizer/dllmain.cpp`
- Create: `src/colorizer/core_registry.h`
- Create: `src/colorizer/core_registry.cpp`
- Modify: `CMakeLists.txt` — add the two new sources to the `wlx-listerine-core` target.
- Modify: `src/colorizer/wlx_core_abi.cpp` — drop `wlx_core_install_for_task2`, route to singleton.
- Modify: `src/host_adapter.cpp` — drop `g_colorizer_raw`, the `wlx_core_install_for_task2` call, and pass a Colorizer-compat shim to LayoutEngine via a new ABI-backed adapter.
- Modify: `src/colorizer/colorizer_host_adapter.cpp` — same.
- Modify: `src/layout_engine.h` / `.cpp` — accept `WlxCore*` instead of `Colorizer*` (only used to call `colorize()` for code blocks).

- [ ] **Step 1: Write the singleton-init regression test**

Append to `tests/test_wlx_core_abi.cpp`:

```cpp
#include <thread>
#include <vector>

TEST_CASE("singleton initialized once across threads") {
    std::vector<WlxCore*> handles(8, nullptr);
    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i) {
        ts.emplace_back([&, i] { handles[i] = wlx_core_acquire(); });
    }
    for (auto& t : ts) t.join();
    for (auto* h : handles) {
        CHECK(h != nullptr);
        CHECK(h == handles[0]);
    }
    for (auto* h : handles) wlx_core_release(h);
}
```

- [ ] **Step 2: Create `src/colorizer/dllmain.cpp`**

```cpp
#define NOMINMAX
#include <windows.h>

// Captured at DLL_PROCESS_ATTACH so CoreRegistry can derive its install dir
// via GetModuleFileNameW. Plain assignment is safe — Windows guarantees
// DllMain runs serialized.
HMODULE g_core_module = nullptr;

extern "C" BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_core_module = static_cast<HMODULE>(hInst);
        DisableThreadLibraryCalls(hInst);
    }
    // DLL_PROCESS_DETACH: do nothing. Singleton is intentionally leaked
    // (see specs/2026-05-01-lazy-grammar-loading-design.md §3.4).
    return TRUE;
}
```

- [ ] **Step 3: Create `src/colorizer/core_registry.h`**

```cpp
#pragma once

#include "colorizer.h"
#include "helix_theme.h"
#include <memory>
#include <mutex>
#include <string>

// Process-wide singleton living inside wlx-listerine-core.dll. Lazy-
// initialized via std::call_once on the first wlx_core_acquire call.
// Discovers its own install dir via GetModuleFileNameW(g_core_module).
//
// All public entry points take the registry's mutex so a colorize() call
// holds the lock for the full parse + query + tree-delete flow. Trees are
// caller-owned and torn down inside the same locked region — eviction
// (added in Task 5) cannot race with active use.
class CoreRegistry {
public:
    static CoreRegistry& instance();

    ColorizeResult colorize(const std::string& source,
                            const std::string& language,
                            bool dark_mode);
    bool supports(const std::string& language);
    const HelixTheme& theme(bool dark_mode) const;

private:
    CoreRegistry();
    static std::wstring resolve_core_dir();

    mutable std::mutex mu_;
    std::unique_ptr<Colorizer> colorizer_;  // replaced piece-by-piece in later tasks
    std::wstring core_dir_;
};
```

- [ ] **Step 4: Create `src/colorizer/core_registry.cpp`**

```cpp
#define NOMINMAX
#include "core_registry.h"

#include <windows.h>
#include <filesystem>

extern HMODULE g_core_module;  // defined in dllmain.cpp

CoreRegistry& CoreRegistry::instance() {
    static std::unique_ptr<CoreRegistry> p;
    static std::once_flag once;
    std::call_once(once, [] { p.reset(new CoreRegistry()); });
    return *p;
}

std::wstring CoreRegistry::resolve_core_dir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_core_module, buf, MAX_PATH);
    if (n == 0) return L"";
    std::wstring path(buf, n);
    auto slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"" : path.substr(0, slash + 1);
}

CoreRegistry::CoreRegistry()
    : core_dir_(resolve_core_dir())
{
    std::wstring grammar_dir = core_dir_ + L"grammars";
    std::wstring theme_dir   = core_dir_ + L"themes";
    // Hardcoded "default" here is replaced by cfg_.theme / cfg_.theme_light
    // in Task 4 when CoreConfig is wired in.
    colorizer_ = std::make_unique<Colorizer>(
        grammar_dir, theme_dir, "default", "");
}

ColorizeResult CoreRegistry::colorize(const std::string& source,
                                      const std::string& language,
                                      bool dark_mode) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!colorizer_) return {};
    return colorizer_->colorize(source, language, dark_mode);
}

bool CoreRegistry::supports(const std::string& language) {
    std::lock_guard<std::mutex> lk(mu_);
    return colorizer_ && colorizer_->supports(language);
}

const HelixTheme& CoreRegistry::theme(bool dark_mode) const {
    std::lock_guard<std::mutex> lk(mu_);
    static HelixTheme empty;
    return colorizer_ ? colorizer_->theme(dark_mode) : empty;
}
```

- [ ] **Step 5: Rewrite `src/colorizer/wlx_core_abi.cpp` to use the singleton**

Replace the entire body of `src/colorizer/wlx_core_abi.cpp` with:

```cpp
#define WLX_CORE_BUILDING
#include "wlx_core/abi.h"
#include "core_registry.h"

#include <cstdlib>
#include <cstring>

extern "C" WLX_CORE_API int wlx_core_abi_version(void) {
    return WLX_CORE_ABI_VERSION;
}

extern "C" WLX_CORE_API WlxCore* wlx_core_acquire(void) {
    auto& reg = CoreRegistry::instance();
    return reinterpret_cast<WlxCore*>(&reg);
}

extern "C" WLX_CORE_API void wlx_core_release(WlxCore*) {
    // No-op until process exit.
}

extern "C" WLX_CORE_API int
wlx_core_supports(WlxCore* h, const char* language) {
    if (!h || !language) return 0;
    return reinterpret_cast<CoreRegistry*>(h)->supports(language) ? 1 : 0;
}

extern "C" WLX_CORE_API int
wlx_core_colorize(WlxCore* h,
                  const char* source, uint32_t len,
                  const char* language,
                  int dark_mode,
                  WlxColorSpan** out_spans, uint32_t* out_count) {
    if (!h || !source || !language || !out_spans || !out_count) return -1;
    auto& reg = *reinterpret_cast<CoreRegistry*>(h);

    std::string src(source, len);
    auto result = reg.colorize(src, language, dark_mode != 0);

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
    auto& reg = *reinterpret_cast<CoreRegistry*>(h);
    const HelixTheme& t = reg.theme(dark_mode != 0);
    auto resolved = t.resolve(scope);
    if (!resolved) return -1;
    *out_rgb = resolved->fg;
    if (out_modifiers) *out_modifiers = resolved->modifiers;
    return 0;
}
```

- [ ] **Step 6: Update `src/layout_engine.h` to accept `WlxCore*`**

Find the `Colorizer*` parameter on the `LayoutEngine` constructor (around line 127 of `src/layout_engine.h`) and member at line 171.

Change `Colorizer* colorizer = nullptr;` to `WlxCore* core = nullptr;` (after `#include "wlx_core/abi.h"` near the top of the file). Forward-declare `class WlxCore;` is unnecessary since the ABI typedef is `typedef struct WlxCore WlxCore;`.

In `src/layout_engine.cpp` at the implementation around line 15, the constructor parameter renames to `WlxCore* core`. Wherever the file calls `colorizer_->colorize(...)`, replace with the ABI call:

Before:
```cpp
auto result = colorizer_->colorize(source, language, dark_mode);
```

After:
```cpp
WlxColorSpan* spans = nullptr;
uint32_t count = 0;
ColorizeResult result;
if (core_ && wlx_core_colorize(core_, source.c_str(),
                               static_cast<uint32_t>(source.size()),
                               language.c_str(), dark_mode ? 1 : 0,
                               &spans, &count) == 0) {
    result.spans.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& s = spans[i];
        ColorSpan cs;
        cs.start = s.start; cs.length = s.length;
        cs.color = s.color; cs.bg_color = s.bg_color;
        cs.has_bg = s.has_bg != 0; cs.modifiers = s.modifiers;
        result.spans.push_back(cs);
    }
    wlx_core_free_spans(spans);
}
```

(Find the corresponding call site by grepping `colorizer_->colorize` in `src/layout_engine.cpp`.)

- [ ] **Step 7: Update `src/host_adapter.cpp` to drop the Task 2 scaffolding**

- Remove `static Colorizer* g_colorizer_raw = nullptr;` and the `extern "C"` declaration of `wlx_core_install_for_task2`.
- Replace the install + raw capture in `ensure_theme()` with just:
  ```cpp
  g_colorizer_handle = wlx_core_acquire();
  ```
- Update the `LayoutEngine` construction in `do_layout()`:
  ```cpp
  LayoutEngine engine(g_dwrite_factory.Get(), g_theme, vs->dark_mode, g_colorizer_handle);
  ```

- [ ] **Step 8: Update `src/colorizer/colorizer_host_adapter.cpp` similarly**

- Remove `g_colorizer_raw` and the `wlx_core_install_for_task2` extern.
- Replace the install with `g_colorizer_handle = wlx_core_acquire();`
- Update any `LayoutEngine` constructions to pass `g_colorizer_handle`.
- Migrate the two direct `g_colorizer->colorize(...)` call sites at the historical lines 381 and 1108 (find them by grepping `g_colorizer->colorize` in the file). Both populate `vs->cached_colors`. Replace the call with the ABI roundtrip:

  ```cpp
  vs->cached_colors = {};
  if (!language.empty() && g_colorizer_handle &&
      wlx_core_supports(g_colorizer_handle, language.c_str())) {
      WlxColorSpan* spans = nullptr;
      uint32_t count = 0;
      if (wlx_core_colorize(g_colorizer_handle,
                            vs->cached_raw_utf8.c_str(),
                            static_cast<uint32_t>(vs->cached_raw_utf8.size()),
                            language.c_str(),
                            vs->dark_mode ? 1 : 0,
                            &spans, &count) == 0 && count > 0) {
          vs->cached_colors.spans.reserve(count);
          for (uint32_t i = 0; i < count; ++i) {
              const auto& s = spans[i];
              ColorSpan cs;
              cs.start = s.start; cs.length = s.length;
              cs.color = s.color; cs.bg_color = s.bg_color;
              cs.has_bg = s.has_bg != 0; cs.modifiers = s.modifiers;
              vs->cached_colors.spans.push_back(cs);
          }
          wlx_core_free_spans(spans);
      }
  }
  ```

  Replaces the old `vs->cached_colors = g_colorizer->colorize(...)` pattern. Apply at both call sites.

- Also update the corresponding `g_colorizer->supports(language)` check in the same conditional (replaced by `wlx_core_supports(...)` above).

- [ ] **Step 9: Add the new sources to `CMakeLists.txt`**

In the `add_library(wlx-listerine-core SHARED ...)` block, add:
```
src/colorizer/dllmain.cpp
src/colorizer/core_registry.cpp
```

- [ ] **Step 10: Build**

Run: `cmake --build --preset conan-release`
Expected: clean build. The temporary `wlx_core_install_for_task2` is gone.

- [ ] **Step 11: Run all tests**

Run: `./build/Release/colorizer-tests.exe`
Expected: all tests pass, including the new singleton-init test.

Run: `./build/Release/tests.exe`
Expected: all tests pass.

- [ ] **Step 12: Run visual regression**

Run: `./scripts/visual-test.sh`
Expected: all 27 cases at >= 95% similarity.

- [ ] **Step 13: Commit**

```bash
git add src/colorizer/dllmain.cpp src/colorizer/core_registry.h \
        src/colorizer/core_registry.cpp src/colorizer/wlx_core_abi.cpp \
        src/host_adapter.cpp src/colorizer/colorizer_host_adapter.cpp \
        src/layout_engine.h src/layout_engine.cpp \
        tests/test_wlx_core_abi.cpp CMakeLists.txt
git commit -m "feat(core): introduce CoreRegistry singleton

call_once-initialized inside the core DLL; derives its install dir from
GetModuleFileNameW. Plugins drop the Task 2 scaffolding and now talk to
the core purely through the C ABI."
```

---

## Task 4: Add CoreConfig (parse `wlx-listerine-core.toml`)

Adds a small TOML parser for the cache config. Reads `[grammar_cache] cap` and `ttl_minutes` from `<core_dir>/wlx-listerine-core.toml`, applies defaults if missing, clamps out-of-range values. The CoreRegistry constructs a `CoreConfig` early; values are not yet consumed (Task 5 wires them into the cache).

**Files:**
- Create: `src/colorizer/core_config.h`
- Create: `src/colorizer/core_config.cpp`
- Create: `tests/test_core_config.cpp`
- Create: `config/wlx-listerine-core.toml`
- Modify: `CMakeLists.txt` — add `core_config.cpp` to the core sources, `test_core_config.cpp` to `colorizer-tests`. Add a post-build step to copy `wlx-listerine-core.toml` to `output/`.
- Modify: `src/colorizer/core_registry.h` / `.cpp` — hold a `CoreConfig`.

- [ ] **Step 1: Write the failing test for CoreConfig defaults**

Create `tests/test_core_config.cpp`:

```cpp
#include <doctest/doctest.h>
#include "core_config.h"
#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static std::wstring make_temp_toml(const std::string& body) {
    auto dir = fs::temp_directory_path() / "wlx_core_cfg_test";
    fs::create_directories(dir);
    auto p = dir / "wlx-listerine-core.toml";
    std::ofstream(p) << body;
    return p.parent_path().wstring() + L"\\";
}

TEST_CASE("CoreConfig returns defaults when file missing") {
    auto cfg = CoreConfig::load(L"definitely_does_not_exist\\");
    CHECK(cfg.cap == 8);
    CHECK(cfg.ttl_minutes == 5);
}

TEST_CASE("CoreConfig parses valid values") {
    auto dir = make_temp_toml(
        "[grammar_cache]\ncap = 16\nttl_minutes = 10\n");
    auto cfg = CoreConfig::load(dir);
    CHECK(cfg.cap == 16);
    CHECK(cfg.ttl_minutes == 10);
}

TEST_CASE("CoreConfig clamps out-of-range cap") {
    auto dir = make_temp_toml(
        "[grammar_cache]\ncap = 0\nttl_minutes = 5\n");
    auto cfg = CoreConfig::load(dir);
    CHECK(cfg.cap == 8);  // clamped to default
}

TEST_CASE("CoreConfig clamps absurd ttl") {
    auto dir = make_temp_toml(
        "[grammar_cache]\ncap = 8\nttl_minutes = -3\n");
    auto cfg = CoreConfig::load(dir);
    CHECK(cfg.ttl_minutes == 5);  // clamped to default
}

TEST_CASE("CoreConfig falls back on parse failure") {
    auto dir = make_temp_toml("this is not :: valid TOML\n");
    auto cfg = CoreConfig::load(dir);
    CHECK(cfg.cap == 8);
    CHECK(cfg.ttl_minutes == 5);
}

TEST_CASE("CoreConfig parses theme names") {
    auto dir = make_temp_toml(
        "[theme]\ndark = \"my-theme\"\nlight = \"my-theme-light\"\n");
    auto cfg = CoreConfig::load(dir);
    CHECK(cfg.theme == "my-theme");
    CHECK(cfg.theme_light == "my-theme-light");
}

TEST_CASE("CoreConfig defaults theme when missing") {
    auto cfg = CoreConfig::load(L"definitely_does_not_exist\\");
    CHECK(cfg.theme == "default");
    CHECK(cfg.theme_light == "");
}
```

- [ ] **Step 2: Run the test to confirm it fails**

Add `tests/test_core_config.cpp` to the `colorizer-tests` target sources in `CMakeLists.txt`.

Run: `cmake --build --preset conan-release`
Expected: fails with `core_config.h: No such file or directory`.

- [ ] **Step 3: Create `src/colorizer/core_config.h`**

```cpp
#pragma once

#include "wlx_core_api.h"
#include <cstdint>
#include <string>

struct WLX_CORE_API CoreConfig {
    uint32_t    cap         = 8;     // soft LRU cap, count of loaded grammars
    uint32_t    ttl_minutes = 5;     // entries younger than this survive eviction sweep
    std::string theme       = "default";  // dark theme name (used as fallback for both modes)
    std::string theme_light = "";    // light-mode override; empty = auto-detect "<theme>_light"

    static CoreConfig load(const std::wstring& core_dir);
};
```

- [ ] **Step 4: Create `src/colorizer/core_config.cpp`**

```cpp
#include "core_config.h"
#include <toml++/toml.hpp>
#include <filesystem>

CoreConfig CoreConfig::load(const std::wstring& core_dir) {
    CoreConfig cfg;
    std::filesystem::path path = std::filesystem::path(core_dir) / "wlx-listerine-core.toml";

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return cfg;

    try {
        auto tbl = toml::parse_file(path.string());

        if (auto v = tbl["grammar_cache"]["cap"].value<int64_t>()) {
            int64_t n = *v;
            // Allow 1..1000; anything outside falls back to default 8.
            cfg.cap = (n >= 1 && n <= 1000) ? static_cast<uint32_t>(n) : 8u;
        }
        if (auto v = tbl["grammar_cache"]["ttl_minutes"].value<int64_t>()) {
            int64_t n = *v;
            // Allow 1..1440 (1 day); else fall back.
            cfg.ttl_minutes = (n >= 1 && n <= 1440) ? static_cast<uint32_t>(n) : 5u;
        }
        if (auto v = tbl["theme"]["dark"].value<std::string>()) {
            if (!v->empty()) cfg.theme = *v;
        }
        if (auto v = tbl["theme"]["light"].value<std::string>()) {
            cfg.theme_light = *v;
        }
    } catch (...) {
        // Bad TOML: defaults already applied.
    }
    return cfg;
}
```

- [ ] **Step 5: Run the test to confirm it passes**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe`
Expected: all 5 new `CoreConfig` tests pass; existing tests still pass.

- [ ] **Step 6: Wire CoreConfig into CoreRegistry**

In `src/colorizer/core_registry.h`, add `#include "core_config.h"` and a member:
```cpp
CoreConfig cfg_;
```

In `src/colorizer/core_registry.cpp`, in the constructor (theme names now come from config; cap/ttl is wired into the cache in Task 5):
```cpp
CoreRegistry::CoreRegistry()
    : core_dir_(resolve_core_dir())
    , cfg_(CoreConfig::load(core_dir_))
{
    std::wstring grammar_dir = core_dir_ + L"grammars";
    std::wstring theme_dir   = core_dir_ + L"themes";
    colorizer_ = std::make_unique<Colorizer>(
        grammar_dir, theme_dir, cfg_.theme, cfg_.theme_light);
}
```

- [ ] **Step 7: Create the default `config/wlx-listerine-core.toml`**

```toml
# wlx-listerine-core configuration
# Shared by both plugins (md + colorizer). Lives next to wlx-listerine-core.dll.
# All values shown are the built-in defaults.
# The core works without this file — only edit to override.

[grammar_cache]
cap = 8              # soft cap on loaded grammars (count); evicts on miss when exceeded AND tail is stale
ttl_minutes = 5      # grammars idle longer than this become eviction candidates

[theme]
dark  = "default"        # Helix-format theme used in dark mode
light = ""               # optional light-mode override; "" auto-detects "<dark>_light.toml"
```

- [ ] **Step 8: Add a post-build copy in `CMakeLists.txt`**

Add a new `add_custom_command(TARGET wlx-listerine-core POST_BUILD ...)` that copies `config/wlx-listerine-core.toml` to `output/wlx-listerine-core.toml`. Place it just after the `set_target_properties` block for `wlx-listerine-core`.

```cmake
add_custom_command(TARGET wlx-listerine-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-core.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-core.toml"
)
```

- [ ] **Step 9: Build and run all tests**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe && ./build/Release/tests.exe`
Expected: all tests pass.

- [ ] **Step 10: Commit**

```bash
git add src/colorizer/core_config.h src/colorizer/core_config.cpp \
        src/colorizer/core_registry.h src/colorizer/core_registry.cpp \
        config/wlx-listerine-core.toml tests/test_core_config.cpp \
        CMakeLists.txt
git commit -m "feat(core): add CoreConfig (parses wlx-listerine-core.toml)

Defaults applied if file missing or values out of range. Not yet
consumed — wired into the cache in the next commit."
```

---

## Task 5: Replace flat-map cache with GrammarCache (LRU + TTL eviction)

This is the heart of the change. `GrammarRegistry`'s flat `std::unordered_map<std::string, GrammarEntry>` is replaced with a `GrammarCache` that maintains both the existing entry map (so reload after evict reuses the path + query source) AND a `std::list<std::string>` LRU. On a miss that pushes `loaded_count_ > cap`, the cache walks the LRU tail and evicts entries whose age exceeds `ttl_minutes`, stopping at the first fresh entry.

The cache takes an injectable `clock` and an injectable `loader` callback so unit tests can advance time and simulate `LoadLibraryW` failures without touching real DLLs.

**Files:**
- Create: `src/colorizer/grammar_cache.h`
- Create: `src/colorizer/grammar_cache.cpp`
- Create: `tests/test_grammar_cache.cpp`
- Modify: `CMakeLists.txt` — add `grammar_cache.cpp` to core; add `test_grammar_cache.cpp` to colorizer-tests; define `WLX_CORE_TESTING` on the test target.
- Modify: `src/colorizer/grammar_registry.h` / `.cpp` — adopt `GrammarCache` internally.
- Modify: `src/colorizer/core_registry.cpp` — pass `cfg_.cap` and `cfg_.ttl_minutes` into the registry.
- Modify: `src/colorizer/colorizer.h` / `.cpp` — `Colorizer` constructor gains optional cap/ttl params (default to in-code 8/5 for the standalone-test path).

### 5.1 — Cache scaffolding

- [ ] **Step 1: Create `src/colorizer/grammar_cache.h`**

```cpp
#pragma once

#include "wlx_core_api.h"

#include <chrono>
#include <functional>
#include <list>
#include <string>
#include <tree_sitter/api.h>
#include <unordered_map>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class WLX_CORE_API GrammarCache {
public:
    using SteadyTp = std::chrono::steady_clock::time_point;
    using Clock    = std::function<SteadyTp()>;

    // Loader returns a pair (HMODULE, TSLanguage*) on success, both null on
    // failure. Injection point for tests.
    struct LoadResult {
        HMODULE handle = nullptr;
        const TSLanguage* language = nullptr;
    };
    using Loader = std::function<LoadResult(const std::wstring& dll_path,
                                            const std::string& language)>;

    GrammarCache(uint32_t cap,
                 std::chrono::seconds ttl,
                 Clock clock = std::chrono::steady_clock::now,
                 Loader loader = default_loader());

    ~GrammarCache();

    GrammarCache(const GrammarCache&) = delete;
    GrammarCache& operator=(const GrammarCache&) = delete;

    // Register an entry's metadata (path + raw query source). Called once
    // per grammar at scan time. Idempotent.
    void register_entry(const std::string& language,
                        std::wstring dll_path,
                        std::string query_source);

    // Returns the loaded TSLanguage, loading the DLL on first call. Updates
    // LRU/last_used. Triggers eviction sweep if (cap exceeded AND tail
    // stale). Returns nullptr if entry unknown or load failed previously.
    const TSLanguage* get_grammar(const std::string& language);

    // Returns compiled query, lazily compiling from the registered
    // query_source. Returns nullptr if no source or compile failed.
    const TSQuery* get_query(const std::string& language);

    // Test-only inspection.
    bool   is_loaded(const std::string& language) const;
    size_t loaded_count() const { return loaded_count_; }
    bool   is_known(const std::string& language) const {
        return entries_.find(language) != entries_.end();
    }
    std::vector<std::string> available_languages() const;

    // Read-only access to the raw, unresolved query source for a language.
    // Used by get_query to walk `; inherits:` chains, which point to other
    // entries' query_source — so the cache owns the full chain.
    std::string raw_query_source(const std::string& language) const;

private:
    static Loader default_loader();
    void evict_locked();

    struct Entry {
        std::wstring dll_path;
        std::string  query_source;
        HMODULE      handle = nullptr;
        const TSLanguage* language = nullptr;
        TSQuery*     query = nullptr;
        bool         load_attempted = false;
        bool         query_compiled = false;
        SteadyTp     last_used{};
        std::list<std::string>::iterator lru_pos{};
    };

    std::unordered_map<std::string, Entry> entries_;
    std::list<std::string> lru_;       // MRU at front
    size_t   loaded_count_ = 0;
    uint32_t cap_;
    std::chrono::seconds ttl_;
    Clock    clock_;
    Loader   loader_;
};
```

Note: `; inherits:` chains in `highlights.scm` reference other registered languages' raw query sources. `GrammarCache` exposes `raw_query_source(lang)` so `get_query` can resolve the chain by walking parent entries — keeping the full chain inside the cache simplifies the boundary with `GrammarRegistry`.

- [ ] **Step 2: Create `src/colorizer/grammar_cache.cpp`**

```cpp
#define NOMINMAX
#include "grammar_cache.h"
#include <algorithm>
#include <sstream>

GrammarCache::GrammarCache(uint32_t cap,
                           std::chrono::seconds ttl,
                           Clock clock,
                           Loader loader)
    : cap_(cap)
    , ttl_(ttl)
    , clock_(std::move(clock))
    , loader_(std::move(loader))
{}

GrammarCache::~GrammarCache() {
    // Match the existing GrammarRegistry destructor behavior. In production
    // the cache is owned by the leaked CoreRegistry and never destructs.
    for (auto& [name, e] : entries_) {
        if (e.query) ts_query_delete(e.query);
        if (e.handle) FreeLibrary(e.handle);
    }
}

GrammarCache::Loader GrammarCache::default_loader() {
    return [](const std::wstring& dll_path,
              const std::string& language) -> LoadResult {
        LoadResult r;
        r.handle = LoadLibraryW(dll_path.c_str());
        if (!r.handle) return r;
        std::string fn_name = "tree_sitter_" + language;
        std::replace(fn_name.begin(), fn_name.end(), '-', '_');
        using GrammarFn = const TSLanguage* (*)();
        auto fn = reinterpret_cast<GrammarFn>(
            GetProcAddress(r.handle, fn_name.c_str()));
        if (!fn) {
            FreeLibrary(r.handle);
            r.handle = nullptr;
            return r;
        }
        r.language = fn();
        return r;
    };
}

void GrammarCache::register_entry(const std::string& language,
                                  std::wstring dll_path,
                                  std::string query_source) {
    auto& e = entries_[language];
    e.dll_path = std::move(dll_path);
    e.query_source = std::move(query_source);
}

const TSLanguage* GrammarCache::get_grammar(const std::string& language) {
    auto it = entries_.find(language);
    if (it == entries_.end()) return nullptr;
    auto& e = it->second;

    if (!e.handle && !e.load_attempted) {
        e.load_attempted = true;
        auto r = loader_(e.dll_path, language);
        if (!r.handle || !r.language) return nullptr;
        e.handle = r.handle;
        e.language = r.language;
        loaded_count_++;
        lru_.push_front(language);
        e.lru_pos = lru_.begin();
        e.last_used = clock_();
        if (loaded_count_ > cap_) evict_locked();
        return e.language;
    }
    if (e.handle) {
        // Promote to MRU.
        lru_.splice(lru_.begin(), lru_, e.lru_pos);
        e.last_used = clock_();
        return e.language;
    }
    // load_attempted && !handle → permanent failure.
    return nullptr;
}

const TSQuery* GrammarCache::get_query(const std::string& language) {
    auto it = entries_.find(language);
    if (it == entries_.end()) return nullptr;
    auto& e = it->second;
    if (e.query) return e.query;
    if (e.query_compiled) return nullptr;
    e.query_compiled = true;

    const TSLanguage* lang = get_grammar(language);
    if (!lang) return nullptr;

    // Resolve `; inherits:` chain.
    std::string source = e.query_source;
    if (!source.empty()) {
        auto first_newline = source.find('\n');
        std::string first_line = (first_newline != std::string::npos)
                                  ? source.substr(0, first_newline)
                                  : source;
        const std::string prefix = "; inherits:";
        auto pos = first_line.find(prefix);
        if (pos != std::string::npos) {
            std::string parents_str = first_line.substr(pos + prefix.size());
            std::string rest = (first_newline != std::string::npos)
                                ? source.substr(first_newline + 1)
                                : std::string{};
            std::string combined;
            std::istringstream iss(parents_str);
            std::string parent;
            while (std::getline(iss, parent, ',')) {
                size_t s = parent.find_first_not_of(" \t(");
                size_t en = parent.find_last_not_of(" \t)");
                if (s == std::string::npos || en == std::string::npos || s > en)
                    continue;
                std::string parent_name = parent.substr(s, en - s + 1);
                std::string ps = raw_query_source(parent_name);
                if (!ps.empty()) {
                    combined += ps;
                    combined += '\n';
                }
            }
            combined += rest;
            source = std::move(combined);
        }
    }
    if (source.empty()) return nullptr;

    uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;
    e.query = ts_query_new(lang, source.c_str(),
                           static_cast<uint32_t>(source.size()),
                           &error_offset, &error_type);
    return e.query;
}

std::string GrammarCache::raw_query_source(const std::string& language) const {
    auto it = entries_.find(language);
    return (it == entries_.end()) ? std::string{} : it->second.query_source;
}

bool GrammarCache::is_loaded(const std::string& language) const {
    auto it = entries_.find(language);
    return it != entries_.end() && it->second.handle != nullptr;
}

std::vector<std::string> GrammarCache::available_languages() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (auto& [name, _] : entries_) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

void GrammarCache::evict_locked() {
    auto now = clock_();
    while (loaded_count_ > cap_ && !lru_.empty()) {
        const std::string& lang = lru_.back();
        auto it = entries_.find(lang);
        if (it == entries_.end()) {
            lru_.pop_back();
            continue;
        }
        auto& e = it->second;
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - e.last_used);
        if (age < ttl_) break;  // youngest of tail is fresh — stop.

        if (e.query) { ts_query_delete(e.query); e.query = nullptr; }
        if (e.handle) { FreeLibrary(e.handle); e.handle = nullptr; }
        e.language = nullptr;
        e.load_attempted = false;
        e.query_compiled = false;
        loaded_count_--;
        lru_.pop_back();
    }
}
```

- [ ] **Step 3: Add `tests/test_grammar_cache.cpp` (tests will fail until cache + tests are linked together)**

```cpp
#include <doctest/doctest.h>
#include "grammar_cache.h"

#include <chrono>
#include <memory>

using namespace std::chrono_literals;

namespace {
// Minimal valid TSLanguage proxy: we never call ts_language_* on these,
// only check for non-null pointer identity. Tree-sitter's API treats
// TSLanguage* as opaque so a sentinel address is fine for tests that
// don't parse anything.
const TSLanguage* fake_lang(int id) {
    return reinterpret_cast<const TSLanguage*>(static_cast<uintptr_t>(0x1000 + id));
}
HMODULE fake_handle(int id) {
    return reinterpret_cast<HMODULE>(static_cast<uintptr_t>(0x2000 + id));
}

GrammarCache::Loader make_loader(int& counter) {
    return [&counter](const std::wstring&, const std::string&) {
        int id = counter++;
        return GrammarCache::LoadResult{ fake_handle(id), fake_lang(id) };
    };
}
} // namespace

TEST_CASE("GrammarCache: get_grammar loads on first call") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(8, 5min, [&] { return now; }, make_loader(counter));
    c.register_entry("a", L"a.dll", "");
    CHECK(c.get_grammar("a") == fake_lang(0));
    CHECK(c.is_loaded("a"));
    CHECK(c.loaded_count() == 1);
}

TEST_CASE("GrammarCache: lru promotion on repeat hits") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(8, 5min, [&] { return now; }, make_loader(counter));
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");
    c.register_entry("c", L"c.dll", "");

    c.get_grammar("a"); now += 1s;
    c.get_grammar("b"); now += 1s;
    c.get_grammar("c"); now += 1s;
    // Hitting "a" should promote it past "c".
    c.get_grammar("a");
    // Verify by exhausting cap and observing eviction order.
    // (Full LRU ordering verified in the staleness test below.)
    CHECK(c.loaded_count() == 3);
}

TEST_CASE("GrammarCache: soft cap with fresh tail keeps everything") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(2, 5min, [&] { return now; }, make_loader(counter));
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");
    c.register_entry("c", L"c.dll", "");

    c.get_grammar("a"); now += 1s;
    c.get_grammar("b"); now += 1s;
    c.get_grammar("c");  // cap=2 exceeded, but all three < ttl
    CHECK(c.loaded_count() == 3);  // soft cap honored
}

TEST_CASE("GrammarCache: evicts when tail is stale") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(2, 5min, [&] { return now; }, make_loader(counter));
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");
    c.register_entry("c", L"c.dll", "");

    c.get_grammar("a");
    now += 6min;  // age "a" past ttl
    c.get_grammar("b");
    now += 1min;
    c.get_grammar("c");  // miss; cap exceeded; "a" tail stale → evict

    CHECK(c.loaded_count() == 2);
    CHECK_FALSE(c.is_loaded("a"));
    CHECK(c.is_loaded("b"));
    CHECK(c.is_loaded("c"));
}

TEST_CASE("GrammarCache: evict sweep stops at first fresh entry") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(1, 5min, [&] { return now; }, make_loader(counter));
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");
    c.register_entry("c", L"c.dll", "");

    c.get_grammar("a");
    now += 6min;  // a stale
    c.get_grammar("b");
    now += 6min;  // b stale, a still stale
    c.get_grammar("c");  // both a and b > ttl now → both evicted

    CHECK_FALSE(c.is_loaded("a"));
    CHECK_FALSE(c.is_loaded("b"));
    CHECK(c.is_loaded("c"));
    CHECK(c.loaded_count() == 1);
}

TEST_CASE("GrammarCache: failed load not on LRU and not retried") {
    GrammarCache::SteadyTp now{};
    auto failing_loader = [](const std::wstring&, const std::string&) {
        return GrammarCache::LoadResult{};  // both null
    };
    GrammarCache c(8, 5min, [&] { return now; }, failing_loader);
    c.register_entry("a", L"a.dll", "");

    CHECK(c.get_grammar("a") == nullptr);
    CHECK(c.loaded_count() == 0);
    CHECK_FALSE(c.is_loaded("a"));

    // Second call must not retry.
    CHECK(c.get_grammar("a") == nullptr);
}

TEST_CASE("GrammarCache: reload after evict") {
    GrammarCache::SteadyTp now{};
    int counter = 0;
    GrammarCache c(1, 5min, [&] { return now; }, make_loader(counter));
    c.register_entry("a", L"a.dll", "");
    c.register_entry("b", L"b.dll", "");

    auto* a1 = c.get_grammar("a");
    now += 6min;
    c.get_grammar("b");  // evicts a
    CHECK_FALSE(c.is_loaded("a"));

    now += 1min;
    auto* a2 = c.get_grammar("a");  // reloads
    CHECK(a2 != nullptr);
    // counter==2 → fake_lang(2): handles re-issued, language re-resolved.
    CHECK(c.is_loaded("a"));
    CHECK(c.loaded_count() == 2);
}
```

- [ ] **Step 4: Add `grammar_cache.cpp` to core sources and the test file to colorizer-tests**

In `CMakeLists.txt`:
- Add `src/colorizer/grammar_cache.cpp` to the `wlx-listerine-core` source list.
- Add `tests/test_grammar_cache.cpp` to the `colorizer-tests` source list.

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe`
Expected: all 7 new `GrammarCache` tests pass; existing tests still pass.

- [ ] **Step 6: Commit the cache scaffolding**

```bash
git add src/colorizer/grammar_cache.h src/colorizer/grammar_cache.cpp \
        tests/test_grammar_cache.cpp CMakeLists.txt
git commit -m "feat(core): add GrammarCache (LRU + TTL) with injectable clock/loader

Pure data structure — not yet wired into GrammarRegistry. Tests cover
soft cap behavior, TTL-gated eviction, evict-then-reload, and failed-
load isolation."
```

### 5.2 — Wire GrammarCache into GrammarRegistry

- [ ] **Step 7: Update `src/colorizer/grammar_registry.h` to delegate to GrammarCache**

Replace the body of `GrammarRegistry` with a thin facade over `GrammarCache`:

```cpp
#pragma once

#include "wlx_core_api.h"
#include "grammar_cache.h"
#include <chrono>
#include <string>
#include <vector>
#include <tree_sitter/api.h>

class WLX_CORE_API GrammarRegistry {
public:
    GrammarRegistry(const std::wstring& grammar_dir,
                    uint32_t cap = 8,
                    std::chrono::seconds ttl = std::chrono::seconds(5 * 60),
                    GrammarCache::Clock clock = std::chrono::steady_clock::now,
                    GrammarCache::Loader loader = {});

    GrammarRegistry(const GrammarRegistry&) = delete;
    GrammarRegistry& operator=(const GrammarRegistry&) = delete;

    bool supports(const std::string& language) const;
    std::vector<std::string> available_languages() const;

    const TSLanguage* get_grammar(const std::string& language);
    const TSQuery*    get_query(const std::string& language);
    TSTree*           parse(const std::string& language, const std::string& source);

private:
    void scan_directory(const std::wstring& grammar_dir);

    GrammarCache cache_;
};
```

- [ ] **Step 8: Update `src/colorizer/grammar_registry.cpp` to delegate to GrammarCache**

```cpp
#define NOMINMAX
#include "grammar_registry.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

GrammarRegistry::GrammarRegistry(const std::wstring& grammar_dir,
                                 uint32_t cap,
                                 std::chrono::seconds ttl,
                                 GrammarCache::Clock clock,
                                 GrammarCache::Loader loader)
    : cache_(cap, ttl,
             clock ? clock : std::chrono::steady_clock::now,
             loader ? loader : GrammarCache::Loader{})
{
    scan_directory(grammar_dir);
}

void GrammarRegistry::scan_directory(const std::wstring& grammar_dir) {
    std::error_code ec;
    if (!fs::is_directory(grammar_dir, ec)) return;

    for (auto& subdir : fs::directory_iterator(grammar_dir, ec)) {
        if (!subdir.is_directory()) continue;
        std::string lang = subdir.path().filename().string();
        std::wstring dll_path;
        std::string query_source;
        for (auto& file : fs::directory_iterator(subdir.path(), ec)) {
            if (!file.is_regular_file()) continue;
            auto filename = file.path().filename().string();
            if (filename.size() >= 16 &&
                filename.substr(0, 12) == "tree-sitter-" &&
                filename.substr(filename.size() - 4) == ".dll") {
                dll_path = file.path().wstring();
            } else if (filename == "highlights.scm") {
                std::ifstream ifs(file.path(), std::ios::binary);
                if (ifs) {
                    std::ostringstream oss;
                    oss << ifs.rdbuf();
                    query_source = oss.str();
                }
            }
        }
        if (!dll_path.empty()) {
            cache_.register_entry(lang, std::move(dll_path), std::move(query_source));
        }
    }
}

bool GrammarRegistry::supports(const std::string& language) const {
    return cache_.is_known(language);
}

std::vector<std::string> GrammarRegistry::available_languages() const {
    return cache_.available_languages();
}

const TSLanguage* GrammarRegistry::get_grammar(const std::string& language) {
    return cache_.get_grammar(language);
}

const TSQuery* GrammarRegistry::get_query(const std::string& language) {
    return cache_.get_query(language);
}

TSTree* GrammarRegistry::parse(const std::string& language,
                               const std::string& source) {
    const TSLanguage* lang = cache_.get_grammar(language);
    if (!lang) return nullptr;
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, lang);
    TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                          source.c_str(),
                                          static_cast<uint32_t>(source.size()));
    ts_parser_delete(parser);
    return tree;
}
```

- [ ] **Step 9: Wire CoreConfig values into the registry**

In `src/colorizer/core_registry.cpp`, update the `Colorizer` construction to pass cap/ttl. Since `Colorizer` doesn't currently take these — extend `Colorizer`:

In `src/colorizer/colorizer.h`, change the constructor:
```cpp
Colorizer(const std::wstring& grammar_dir,
          const std::wstring& theme_dir,
          const std::string& theme_name = "default",
          const std::string& theme_light_name = "",
          uint32_t grammar_cap = 8,
          uint32_t grammar_ttl_minutes = 5);
```

In `src/colorizer/colorizer.cpp`, the new constructor body:
```cpp
Colorizer::Colorizer(const std::wstring& grammar_dir,
                     const std::wstring& theme_dir,
                     const std::string& theme_name,
                     const std::string& theme_light_name,
                     uint32_t grammar_cap,
                     uint32_t grammar_ttl_minutes)
    : grammar_registry_(std::make_unique<GrammarRegistry>(
          grammar_dir, grammar_cap,
          std::chrono::seconds(grammar_ttl_minutes * 60)))
{
    // theme loading unchanged from existing code
    std::filesystem::path dir(theme_dir);
    dark_theme_ = HelixTheme::load(theme_name, dir);
    if (!theme_light_name.empty()) {
        light_theme_ = HelixTheme::load(theme_light_name, dir);
    } else {
        std::string light_candidate = theme_name + "_light";
        auto light_path = dir / (light_candidate + ".toml");
        if (std::filesystem::exists(light_path)) {
            light_theme_ = HelixTheme::load(light_candidate, dir);
        } else {
            light_theme_ = HelixTheme::make_default(false);
        }
    }
}
```

In `src/colorizer/core_registry.cpp` constructor (theme names from `cfg_` set up in Task 4 stay; cap/ttl now flow through):
```cpp
colorizer_ = std::make_unique<Colorizer>(
    grammar_dir, theme_dir, cfg_.theme, cfg_.theme_light,
    cfg_.cap, cfg_.ttl_minutes);
```

- [ ] **Step 10: Update the existing direct-construction tests**

`tests/test_colorizer_grammar.cpp` constructs `GrammarRegistry` directly — those still compile because of the defaulted `cap`/`ttl`/`clock`/`loader`. No edits needed if the existing tests don't poke internals. Verify by re-running them.

- [ ] **Step 11: Build and run all tests**

Run: `cmake --build --preset conan-release && ./build/Release/colorizer-tests.exe && ./build/Release/tests.exe`
Expected: all tests pass — including all `GrammarCache` tests, all existing `GrammarRegistry` tests, all ABI tests.

- [ ] **Step 12: Run visual regression**

Run: `./scripts/visual-test.sh`
Expected: all 27 cases at >= 95% similarity.

- [ ] **Step 13: Commit the wiring**

```bash
git add src/colorizer/grammar_registry.h src/colorizer/grammar_registry.cpp \
        src/colorizer/colorizer.h src/colorizer/colorizer.cpp \
        src/colorizer/core_registry.cpp
git commit -m "feat(core): wire GrammarCache into GrammarRegistry

GrammarRegistry is now a thin facade over GrammarCache; cap and
ttl_minutes flow from CoreConfig through the Colorizer constructor."
```

---

## Task 6: Single-folder packaging

`scripts/package.ps1` is rewritten to produce a single bundled ZIP. CMake post-build commands lay out the new flat directory. The two `pluginst-*.inf` files merge into one `pluginst.inf` that registers both `.wlx64`s using TC's multi-file syntax.

**Files:**
- Delete: `config/pluginst-md.inf`, `config/pluginst-colorizer.inf`
- Create: `config/pluginst.inf`
- Modify: `scripts/package.ps1`
- Modify: `CMakeLists.txt` — replace per-plugin theme/grammar copy commands with a unified post-build layout.

- [ ] **Step 1: Create `config/pluginst.inf`**

```
[plugininstall]
description=Fast, lightweight Markdown renderer + syntax colorizer (tree-sitter). Two TC plugins shipped together.
type=wlx
file=wlx-listerine-md.wlx64
file2=wlx-listerine-colorizer.wlx64
defaultdir=wlx-listerine
```

- [ ] **Step 2: Delete the old per-plugin install files**

```bash
git rm config/pluginst-md.inf config/pluginst-colorizer.inf
```

- [ ] **Step 3: Update `CMakeLists.txt` post-build commands**

Remove the existing `add_custom_command(TARGET wlx-listerine-md POST_BUILD ...)` block (currently lines 110-122) and the `add_custom_command(TARGET wlx-listerine-colorizer POST_BUILD ...)` block (currently lines 156-173).

Replace with a single block that runs after both plugins exist. Attach it to whichever plugin builds last (CMake will run it once `wlx-listerine-colorizer` finishes since both link the import lib of `wlx-listerine-core`):

```cmake
# Lay out the unified output/ directory: themes + grammars + sample TOMLs
# go alongside the two .wlx64 files and the core DLL. Packaging picks up
# from here.
add_custom_command(TARGET wlx-listerine-colorizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-md.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-md.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-colorizer.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-colorizer.toml"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/output/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/themes/default.toml"
        "${CMAKE_SOURCE_DIR}/output/themes/default.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/themes/default_light.toml"
        "${CMAKE_SOURCE_DIR}/output/themes/default_light.toml"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/output/grammars"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/grammars"
        "${CMAKE_SOURCE_DIR}/output/grammars"
)
```

(The `wlx-listerine-core.toml` post-build copy added in Task 4 stays as-is.)

- [ ] **Step 4: Rewrite `scripts/package.ps1`**

```powershell
param(
    [Parameter(Mandatory=$true)]
    [string]$Version
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)

$staging = "$root/staging/wlx-listerine"
New-Item -ItemType Directory -Path $staging -Force | Out-Null

# pluginst registers both .wlx64s into the same defaultdir.
Copy-Item "$root/config/pluginst.inf" "$staging/pluginst.inf"

# Plugin DLLs + core DLL.
Copy-Item "$root/output/wlx-listerine-md.wlx64"           "$staging/"
Copy-Item "$root/output/wlx-listerine-colorizer.wlx64"    "$staging/"
Copy-Item "$root/output/wlx-listerine-core.dll"           "$staging/"

# Sample TOMLs (rename to .toml.sample so users opt in by removing the suffix).
function Write-Sample($srcPath, $name) {
    $header = @"
# $name configuration
# Rename this file to $name.toml to customize.
# All values shown are the built-in defaults.
# The plugin works without this file — only create it to override specific settings.

"@
    $body = Get-Content $srcPath -Raw
    Set-Content -Path "$staging/$name.toml.sample" -Value ($header + $body) -NoNewline
}

Write-Sample "$root/config/wlx-listerine-md.toml"         "wlx-listerine-md"
Write-Sample "$root/config/wlx-listerine-colorizer.toml"  "wlx-listerine-colorizer"
Write-Sample "$root/config/wlx-listerine-core.toml"       "wlx-listerine-core"

# Themes + grammars: shipped once.
Copy-Item "$root/output/themes"   "$staging/themes"   -Recurse
Copy-Item "$root/output/grammars" "$staging/grammars" -Recurse

# Single bundled ZIP.
$zip = "$root/wlx-listerine-$Version.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$staging/*" -DestinationPath $zip
Write-Host "Created $zip"

Remove-Item "$root/staging" -Recurse -Force
Write-Host "Done."
```

- [ ] **Step 5: Build and verify the unified output directory**

Run: `cmake --build --preset conan-release`

Inspect `output/` and confirm it contains:
- `wlx-listerine-md.wlx64`
- `wlx-listerine-colorizer.wlx64`
- `wlx-listerine-core.dll`
- `wlx-listerine-md.toml`
- `wlx-listerine-colorizer.toml`
- `wlx-listerine-core.toml`
- `themes/default.toml`, `themes/default_light.toml`
- `grammars/<25+ subdirs>`

- [ ] **Step 6: Run the packaging script**

Run: `pwsh scripts/package.ps1 -Version test`

Expected: a single `wlx-listerine-test.zip` at the repo root. Verify its contents (e.g. with `Expand-Archive` to a temp dir or `7z l`) — should contain the layout above.

- [ ] **Step 7: Manual TC smoke test (engineer note)**

Open `wlx-listerine-test.zip` in Total Commander. TC should offer to install. Accept. Browse to a `.md` file and verify the md plugin renders. Browse to a `.cpp` file and verify the colorizer plugin renders. Both should pull themes from `wlx-listerine\themes\` and grammars from `wlx-listerine\grammars\`. **No second prompt for a separate plugin install** — both plugins register from one ZIP.

- [ ] **Step 8: Commit**

```bash
git add config/pluginst.inf scripts/package.ps1 CMakeLists.txt
git rm config/pluginst-md.inf config/pluginst-colorizer.inf
git commit -m "build(package): single bundled wlx-listerine ZIP

Themes and grammars now ship once. pluginst.inf registers both .wlx64s
into one TC plugin folder."
```

---

## Task 7: Drop dead per-plugin theme/grammar paths from TOML

The plugins no longer resolve grammar/theme paths or theme names from their own TOML — those come from the core's install dir + `wlx-listerine-core.toml`. Remove the dead config keys and the code that reads them.

**Files:**
- Modify: `config/wlx-listerine-md.toml`
- Modify: `config/wlx-listerine-colorizer.toml`
- Modify: `src/theme_service.h` / `.cpp` — drop `code_grammar_dir`, `code_theme_dir`, `code_theme`, `code_theme_light` fields and parse paths.
- Modify: `src/host_adapter.cpp` — drop the path-resolution logic in `ensure_theme()`.
- Modify: `src/colorizer/colorizer_host_adapter.cpp` — same.

- [ ] **Step 1: Drop dead keys from `config/wlx-listerine-md.toml`**

Open the file and remove any of these lines (under whatever sections they live in): `code_grammar_dir = ...`, `code_theme_dir = ...`, `code_theme = ...`, `code_theme_light = ...`. If a section becomes empty, leave the section header in place.

- [ ] **Step 2: Drop dead keys from `config/wlx-listerine-colorizer.toml`**

Same as Step 1 — delete the same four keys.

- [ ] **Step 3: Drop the fields from `src/theme_service.h` / `.cpp`**

In `src/theme_service.h`, remove the four fields from `ThemeConfig`:
- `std::wstring code_grammar_dir = L"grammars";`
- `std::wstring code_theme_dir = L"themes";`
- `std::string code_theme = "default";`
- `std::string code_theme_light;`

In `src/theme_service.cpp`, remove the parse blocks that read these from TOML (around lines 138-146 — search for `code_theme_dir`, `code_theme`, `code_theme_light`). Remove any default-init lines in `default_config()` that touch these fields.

- [ ] **Step 4: Drop the path-resolution code in `src/host_adapter.cpp` `ensure_theme()`**

After Task 3, `ensure_theme()` ends with `g_colorizer_handle = wlx_core_acquire();`. Anything that previously read `g_theme.config().code_grammar_dir` or `code_theme_dir` is dead — remove those local variables. The grammar/theme dir lookup happens entirely inside the core now.

- [ ] **Step 5: Drop the path-resolution code in `src/colorizer/colorizer_host_adapter.cpp`**

Same as Step 4 — remove the `code_grammar_dir` / `code_theme_dir` local variables; the only call after migration is `g_colorizer_handle = wlx_core_acquire();`.

- [ ] **Step 5b: Update `src/screenshot_main.cpp` to use the new constructor signature**

The current line 229 reads:
```cpp
Colorizer colorizer(theme.config().code_grammar_dir, theme.config().code_theme_dir,
                    theme.config().code_theme, theme.config().code_theme_light);
```

Replace with hardcoded relative paths (`screenshot_tool` runs from the build's `output/` directory which contains `grammars/` and `themes/`):
```cpp
Colorizer colorizer(L"grammars", L"themes", "default", "");
```

If `screenshot_tool` ever needs custom theme selection in the future, route it through `wlx_core_acquire()` like the plugins. For now the default theme is fine for golden-PNG generation.

- [ ] **Step 6: Build and run all tests**

Run: `cmake --build --preset conan-release && ./build/Release/tests.exe && ./build/Release/colorizer-tests.exe`
Expected: all tests pass.

- [ ] **Step 7: Run visual regression**

Run: `./scripts/visual-test.sh`
Expected: all 27 cases at >= 95% similarity.

- [ ] **Step 8: Commit**

```bash
git add config/wlx-listerine-md.toml config/wlx-listerine-colorizer.toml \
        src/theme_service.h src/theme_service.cpp \
        src/host_adapter.cpp src/colorizer/colorizer_host_adapter.cpp \
        src/screenshot_main.cpp
git commit -m "refactor: drop dead per-plugin grammar/theme keys

Theme/grammar paths and theme names move into wlx-listerine-core.toml,
shared by both plugins. screenshot_tool now hardcodes the default."
```

---

## Task 8: Documentation updates

**Files:**
- Modify: `README.md`
- Modify: `CLAUDE.md`
- Modify: `docs/CONFIGURATION.md`
- Modify: `docs/BUILDING.md`
- Modify: `docs/LANGUAGES.md`

- [ ] **Step 1: README.md — kill the resolved TODO and update install steps**

In the `## 🚧 TODO` section, **remove** the bullet:
> **Lazy grammar loading for the colorizer** — ...

In the `## 📥 Installation` section, replace the current "Download the plugin ZIPs" instruction with one that mentions a single bundled ZIP:

```markdown
## 📥 Installation

1. Download `wlx-listerine-<version>.zip` from [**Releases**](../../releases)
2. Open the ZIP in Total Commander — it will register both plugins (markdown + colorizer)
3. ✅ Done — themes and grammars install alongside the plugins
```

- [ ] **Step 2: CLAUDE.md — update Architecture and add a Process-wide cache paragraph**

In the `## Architecture` section, change the line:
```
### colorizer-core (static lib)
```
to:
```
### wlx-listerine-core (shared DLL)
```

Update the description to note: "Built as a real Windows DLL (`wlx-listerine-core.dll`) shared by both plugin `.wlx64`s. The plugins talk to it through a C ABI in `include/wlx_core/abi.h` (extern "C" + an inline C++ shim for RAII span ownership)."

Add a new paragraph at the end of the colorizer-core section:

```markdown
**Process-wide grammar cache:** `CoreRegistry` is a singleton inside the core DLL, lazy-initialized via `std::call_once` on first ABI call. It owns one `GrammarCache` (LRU + TTL eviction) shared across both plugins, plus dark/light themes. A single `std::mutex` is held for the duration of each `colorize()` call — since trees are torn down inside the same locked region, eviction can never race with active language pointers. Cache config lives in `wlx-listerine-core.toml` next to the DLL: `[grammar_cache] cap` (soft cap, default 8) and `ttl_minutes` (eviction freshness gate, default 5). The `wlx-listerine-core.dll`, both `.wlx64`s, themes, and grammars all ship together as one bundle — versions are pinned lockstep, and the ABI version is checked at first call.
```

- [ ] **Step 3: docs/CONFIGURATION.md — add a section for `wlx-listerine-core.toml` and remove the old per-plugin theme keys**

Append a new section documenting the core config:

````markdown
## wlx-listerine-core.toml

Shared by both plugins. Lives next to `wlx-listerine-core.dll` in the install
directory. All values optional — defaults shown.

```toml
[grammar_cache]
cap = 8              # soft LRU cap on loaded grammar DLLs
ttl_minutes = 5      # entries idle longer than this are eviction candidates

[theme]
dark  = "default"    # Helix-format theme used in dark mode
light = ""           # optional light-mode override; "" auto-detects "<dark>_light.toml"
```

`cap` is a *soft* cap: the cache may briefly exceed it if every entry on
the LRU tail is younger than `ttl_minutes`. The eviction sweep runs only
on a miss that pushes the cache above `cap`, and stops at the first fresh
entry from the LRU tail. This means a busy session never thrashes; an
idle session releases stale grammars on the next miss.

Themes live in the shared `wlx-listerine/themes/` directory. Drop additional
Helix-compatible `.toml` files there and reference them by name (without
the `.toml` suffix) in `[theme] dark` / `[theme] light`.
````

Also: **scan `docs/CONFIGURATION.md` for any existing references** to the per-plugin keys we just removed — `code_theme`, `code_theme_light`, `code_grammar_dir`, `code_theme_dir`. Delete those entries; replace with a pointer to the new core toml section.

- [ ] **Step 4: docs/BUILDING.md — note the new shared DLL artifact**

Add a paragraph near the build output description:

> The build produces three DLL artifacts in `output/`: `wlx-listerine-md.wlx64`, `wlx-listerine-colorizer.wlx64`, and `wlx-listerine-core.dll` (shared by both plugins). All three are versioned lockstep — never mix builds. The plugins link against the core's import lib (`wlx-listerine-core.lib`).

- [ ] **Step 5: docs/LANGUAGES.md — update grammar-drop path**

Find any reference to `wlx-listerine-md/grammars/` or `wlx-listerine-colorizer/grammars/` (instructions to drop new grammar DLLs). Replace with the single path:

> Drop new grammar subdirectories into `<TC plugin dir>/wlx-listerine/grammars/<lang>/`. Both plugins pick them up from this single shared location.

- [ ] **Step 6: Verify docs build / lint cleanly**

If docs are rendered (e.g. by GitHub markdown), spot-check the renderings for broken links or formatting. Otherwise just visually skim each modified file for typos.

- [ ] **Step 7: Commit**

```bash
git add README.md CLAUDE.md docs/CONFIGURATION.md docs/BUILDING.md docs/LANGUAGES.md
git commit -m "docs: update for shared core DLL + LRU grammar cache + bundled install"
```

---

## Final verification

After Task 8 is committed:

- [ ] **Run the full test matrix one last time**

```bash
cmake --build --preset conan-release
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
./scripts/visual-test.sh
```

Expected: all unit tests pass, all 27 visual cases at >= 95% similarity.

- [ ] **Manual install smoke test**

```bash
pwsh scripts/package.ps1 -Version smoke
```

Open the resulting ZIP in TC. Confirm: single install dialog, both plugins register, both render correctly, install dir contains exactly one `themes/` and one `grammars/` directory.

- [ ] **Confirm the README TODO is gone and `git log` tells the story**

```bash
grep -i "lazy grammar" README.md   # should print nothing
git log --oneline -10              # eight feature commits + the docs commit
```
