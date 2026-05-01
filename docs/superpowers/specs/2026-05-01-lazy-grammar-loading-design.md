# Lazy Grammar Loading + Shared Core Module — Design

**Date:** 2026-05-01
**Status:** Approved, ready for plan
**Tracks:** README TODO "Lazy grammar loading for the colorizer"

## Problem

Today the project ships two TC plugin DLLs (`wlx-listerine-md.wlx64` and `wlx-listerine-colorizer.wlx64`), each linked against `colorizer-core` as a **static** library. Consequences:

1. **Memory duplication at runtime.** Each plugin instantiates its own `Colorizer` and therefore its own `GrammarRegistry`. When both plugins are active in the same TC instance, every grammar DLL the user touches is loaded *twice* (once per plugin), and every `TSQuery` is compiled twice.
2. **No eviction.** `GrammarRegistry` already lazy-loads grammars on first `get_grammar()` call (the README TODO's wording is partly stale — load-on-demand exists). What's missing is *unloading*. Once loaded, a grammar DLL stays loaded for the lifetime of the plugin DLL. A long TC session that touches many languages monotonically grows memory.
3. **Disk duplication at install.** `scripts/package.ps1` ships `themes/` and `grammars/` (~26 grammar DLLs, multi-MB) inside both ZIPs. The user's install has two copies of every grammar.

## Goals

- Eliminate runtime duplication by sharing one `GrammarRegistry` across both plugins.
- Bound memory growth by adding eviction to the registry.
- Eliminate disk duplication by shipping themes + grammars + core as one bundle.

## Non-goals

- Per-call multi-threaded colorize (single lock is sufficient; reconsider only if profiling demands).
- Pin/unpin tokens for cross-call grammar holding (no caller currently retains parse trees across calls).
- Runtime hot-reload of grammars or themes (TC restart still required).
- Splitting the cache per-view or per-plugin (defeats the dedupe purpose).
- A separate "core only" ZIP for users who want one plugin without the other (we picked single-bundle).

---

## Architecture

### Install topology

```
<TC>/wlx/wlx-listerine/
   pluginst.inf                       (registers both .wlx64s — file= + file2=)
   wlx-listerine-md.wlx64
   wlx-listerine-colorizer.wlx64
   wlx-listerine-core.dll             (NEW — shared module)
   wlx-listerine-core.toml            (optional — grammar cache config)
   wlx-listerine-md.toml.sample
   wlx-listerine-colorizer.toml.sample
   themes/
       default.toml
       default_light.toml
   grammars/
       <lang>/tree-sitter-<lang>.dll
       <lang>/highlights.scm
       ...
```

One ZIP. One TC install. One folder. No duplication.

### Build module graph

| Target                    | Type        | Was        | Notes                                                                            |
|---------------------------|-------------|------------|----------------------------------------------------------------------------------|
| `wlx-listerine-core`      | **SHARED**  | static lib | Renamed from `colorizer-core`. Owns registry, themes, query highlighter, ABI.    |
| `wlx-core`                | static      | (same)     | md-specific layout/render/cache. Now links the import lib.                       |
| `wlx-listerine-md.wlx64`  | shared      | (same)     | Links `wlx-core` + import lib.                                                   |
| `wlx-listerine-colorizer.wlx64` | shared | (same)     | Links `wlx-core` + import lib.                                                   |
| `screenshot_tool`         | exe         | (same)     | Links the import lib.                                                            |
| `tests`, `colorizer-tests`| exe         | (same)     | Link the import lib. Build flag `WLX_CORE_TESTING` exposes test-only constructors. |

### Lifecycle

1. TC loads (say) `wlx-listerine-md.wlx64`. The Windows loader resolves its static import on `wlx-listerine-core.dll` from the same directory.
2. `DllMain(DLL_PROCESS_ATTACH)` on core stashes its own `HMODULE` for later `GetModuleFileNameW` use.
3. First call from any plugin into the public ABI runs `CoreRegistry::instance()` under `std::call_once`:
   - Derive `core_dir` from the stashed `HMODULE` (callers don't need to pass paths).
   - Parse `<core_dir>/wlx-listerine-core.toml` (defaults if missing/bad).
   - `GrammarCache::scan_directory(<core_dir>/grammars/)` — populates entry metadata only, no `LoadLibraryW`.
   - `HelixTheme::load(<core_dir>/themes/...)` for both light and dark themes.
4. The second plugin loads. The OS loader sees `wlx-listerine-core.dll` already mapped — same instance, same singleton.
5. At plugin `DLL_PROCESS_DETACH`, the plugin-side `Colorizer` shim is **intentionally leaked** (matches the existing pattern at `host_adapter.cpp:797` / `colorizer_host_adapter.cpp:941`).
6. At core `DLL_PROCESS_DETACH`, do nothing. The singleton is intentionally leaked. No `FreeLibrary` of grammar DLLs at process exit — the OS reaps everything.

### Concurrency

A single `std::mutex` inside the singleton, taken in every public ABI entry point. A `colorize()` call holds the lock for the full parse + query + tree-delete flow. Since trees are caller-owned and torn down inside the same call (`colorizer.cpp:59,67`), no language pointer outlives the lock — eviction can never race with active use.

Cross-plugin contention is bounded (UI-thread driven, short calls). If profiling later shows pain, the mutex can become a `shared_mutex` with eviction taking the exclusive side. Out of scope here.

### ABI boundary

C ABI across the DLL boundary — opaque handles, POD spans, no STL. C++ ergonomics provided by an inline header-only shim that wraps the raw calls. Plugins keep using the familiar `Colorizer` API; the wire format is C.

---

## Components

### Public ABI — `include/wlx_core/abi.h`

```c
#define WLX_CORE_ABI_VERSION 1

typedef struct WlxCore WlxCore;
typedef struct WlxColorSpan {
    uint32_t start;
    uint32_t length;
    uint32_t color;
    uint32_t bg_color;
    uint8_t  has_bg;
    uint8_t  modifiers;
} WlxColorSpan;

WLX_CORE_API int       wlx_core_abi_version(void);
WLX_CORE_API WlxCore*  wlx_core_acquire(void);          // singleton, idempotent
WLX_CORE_API void      wlx_core_release(WlxCore*);      // refcount-- (no-op until exit)

WLX_CORE_API int       wlx_core_supports(WlxCore*, const char* language);
WLX_CORE_API int       wlx_core_colorize(WlxCore*,
                                         const char* source, uint32_t len,
                                         const char* language,
                                         int dark_mode,
                                         WlxColorSpan** out_spans, uint32_t* out_count);
WLX_CORE_API void      wlx_core_free_spans(WlxColorSpan*);

WLX_CORE_API int       wlx_core_theme_color(WlxCore*, const char* scope, int dark_mode,
                                            uint32_t* out_rgb, uint8_t* out_modifiers);
```

Allocations from `wlx_core_colorize` must be released with `wlx_core_free_spans` only — never `free()`. The inline shim in `abi.h` wraps the result in RAII so plugin code can't forget.

### `CoreRegistry` (singleton, internal to core)

```cpp
class CoreRegistry {
public:
    static CoreRegistry& instance();   // lazy via call_once

    ColorizeResult colorize(std::string_view src, std::string_view lang, bool dark);
    bool supports(std::string_view lang) const;
    const HelixTheme& theme(bool dark) const;

private:
    CoreRegistry();
    const TSLanguage* get_grammar_locked(const std::string& lang);
    const TSQuery*    get_query_locked(const std::string& lang);
    void              evict_locked();

    mutable std::mutex mu_;
    GrammarCache cache_;
    HelixTheme   dark_theme_;
    HelixTheme   light_theme_;
    CoreConfig   cfg_;
};
```

`core_dir` is derived inside the core DLL via `GetModuleFileNameW(g_core_module)`, ignoring caller-supplied paths. Removes a class of bugs where two plugins compute different dirs.

### `GrammarCache` (LRU + TTL)

```cpp
struct CacheEntry {
    std::wstring dll_path;
    std::string  query_source;       // raw, with `; inherits:` directives
    HMODULE      handle = nullptr;
    const TSLanguage* language = nullptr;
    TSQuery*     query = nullptr;
    bool         load_attempted = false;
    bool         query_compiled = false;
    SteadyTp     last_used;
    ListIter     lru_pos;            // iterator into lru_
};

class GrammarCache {
    std::unordered_map<std::string, CacheEntry> entries_;
    std::list<std::string>                       lru_;       // MRU at front
    size_t loaded_count_ = 0;                                // entries with handle != null
    size_t cap_;
    std::chrono::seconds ttl_;
    std::function<SteadyTp()> clock_;                        // injectable for tests
};
```

`scan_directory` populates `entries_` with `dll_path` and `query_source` only. Nothing is loaded.

### Eviction algorithm

On `get_grammar_locked(lang)`:

1. Lookup `entries_[lang]`. Absent → return nullptr.
2. If `entry.handle == null && !entry.load_attempted`:
   - `LoadLibraryW(entry.dll_path)`; resolve `tree_sitter_<lang>` symbol; set `entry.language`; `loaded_count_++`.
   - Splice into front of `lru_`, stamp `last_used`.
3. Else if loaded: splice to front, stamp `last_used`.
4. If `loaded_count_ > cap_`: `evict_locked()`.

`evict_locked()`:

```
while loaded_count_ > cap_:
    tail_iter   = lru_.back()
    tail_entry  = entries_[*tail_iter]
    age         = clock_() - tail_entry.last_used

    if age < ttl_:
        break                                // youngest of tail is fresh — stop

    ts_query_delete(tail_entry.query)       // safe if null
    FreeLibrary(tail_entry.handle)
    tail_entry.handle = nullptr
    tail_entry.language = nullptr
    tail_entry.query = nullptr
    tail_entry.load_attempted = false       // allow future reload
    tail_entry.query_compiled = false
    lru_.pop_back()
    loaded_count_--
```

Properties:
- **Soft cap.** `loaded_count_` may exceed `cap_` if all tail entries are within `ttl_`. A busy session never thrashes.
- **TTL-driven cleanup.** An idle session releases stale grammars on the next miss.
- **Metadata persists.** Evicted entries stay in `entries_` (paths and `query_source`). Re-load is fast: no rescan, no re-read of `highlights.scm`.
- **Failed loads excluded.** `load_attempted && !handle` entries don't count toward `loaded_count_` and aren't on `lru_`.

### Config — `wlx-listerine-core.toml`

```toml
[grammar_cache]
cap = 8              # soft cap (count of loaded grammars)
ttl_minutes = 5      # entries younger than this survive the eviction sweep
```

- File optional. Missing → defaults (`cap=8`, `ttl_minutes=5`).
- Parse failure or out-of-range values → log + defaults. Not a fatal error.
- Path: `<core_dir>/wlx-listerine-core.toml`.

### Themes & grammars

Both resolved from `<core_dir>/themes/` and `<core_dir>/grammars/`. Per-plugin TOML keeps `theme = "..."` and `theme_light = "..."` (each plugin can pick a different theme). Per-plugin `theme_dir` and `grammar_dir` keys are dropped from the schema.

---

## Data flow

### Colorize hot path

```
plugin's colorize() shim
  → wlx_core_colorize(core, src, lang, dark)
    → CoreRegistry::colorize                [acquires mu_]
       ├─ get_grammar_locked(lang)
       │    ├─ load if needed (LoadLibraryW + GetProcAddress)
       │    ├─ splice to LRU front, stamp last_used
       │    └─ if loaded_count_ > cap: evict_locked()
       ├─ get_query_locked(lang)            (compile on first use)
       ├─ ts_parser_new + parse + ts_parser_delete
       ├─ QueryHighlighter::run on root node + theme
       ├─ ts_tree_delete                    ← tree gone before lock release
       └─ build ColorSpan vector → out_spans
                                            [releases mu_]
```

The parse tree is created and destroyed inside the same locked region that acquired the language pointer. Eviction (only fires inside `get_grammar_locked`) cannot run concurrently with any tree being held. No pin tokens needed.

### Plugin shutdown

- `ListCloseWindow`: per-view state torn down in plugin land; **no core interaction**. Cache freshness is driven by `last_used`, not view lifecycle.
- Plugin `DLL_PROCESS_DETACH`: existing leak pattern preserved.
- Core `DLL_PROCESS_DETACH`: do nothing. Process-exit cleanup is the OS's responsibility.

### What does NOT cross the ABI

- `tree-sitter::api.h` types — pure core internals.
- STL containers — never on exported function signatures.
- `HelixTheme` C++ object — exposed via `wlx_core_theme_color(scope, dark)` flat lookup.

---

## Error handling

Principle: a viewer that fails to highlight should still display the file. Every error mode degrades to plain text — never crash, never popup.

| Failure                              | Behavior                                                                                  |
|--------------------------------------|-------------------------------------------------------------------------------------------|
| Core DLL unresolved at plugin load   | Plugin DLL fails to load; TC silently moves on. (Corrupted install only — L1 prevents.)   |
| ABI version mismatch                 | Inline shim checks `wlx_core_abi_version() == WLX_CORE_ABI_VERSION` at first call; mismatch → log + sentinel "core unavailable" + plain-text mode. No popup. |
| `wlx-listerine-core.toml` missing/bad| Defaults (`cap=8`, `ttl_minutes=5`).                                                      |
| Out-of-range config values           | Sanitize to defaults, log. Don't reject the whole file.                                   |
| Themes missing                       | Fall back to in-code `HelixTheme::make_default` (already implemented).                    |
| `grammars/` empty                    | `wlx_core_supports` returns 0 for everything. Plain-text rendering.                        |
| `LoadLibraryW` returns null          | `load_attempted=true`, return null forever. Don't retry every keystroke.                  |
| `GetProcAddress` fails               | `FreeLibrary`, return null forever.                                                       |
| Query parse error                    | `query_compiled=true`, return null forever; **language stays loaded** for parse-only use. |
| Antivirus quarantine mid-session     | Treated as normal `LoadLibraryW` failure.                                                 |

### Allocator-mismatch hazard

`wlx_core_colorize` allocates with `malloc` inside core. Plugin must release via `wlx_core_free_spans`. The inline C++ shim in `abi.h` enforces this with RAII.

### Logging

Existing `WLX_TRACE_ENABLE` build option (`CMakeLists.txt:29`) reused. Cache hits/evicts/misses gated by it; true errors (load failures, ABI mismatch) unconditional. All via `OutputDebugStringW`.

### Eviction safety scenarios

- **Two plugins call colorize concurrently from two TC views:** lock serializes; bounded wait acceptable for a viewer.
- **Spans rendered on screen:** `ColorSpan` is POD with `uint32_t` color values. No pointers into the cache survive past `wlx_core_free_spans`. Eviction free between renders.
- **Selection / search holds spans across calls:** spans are POD copies; cache-owned pointers (`TSLanguage*`, `TSQuery*`) bounded by single colorize call.

### Intentionally not handled

- Disk full / IO during scan (silently skipped, same as today).
- Concurrent modification of `grammars/` at runtime — TC restart required.

---

## Testing

### New unit tests (`colorizer-tests`)

- `GrammarCache_lru_promotion`
- `GrammarCache_soft_cap_with_fresh_tail` — 9 entries within ttl, no eviction (cap=8).
- `GrammarCache_evicts_when_tail_stale` — advance injectable clock, miss triggers sweep.
- `GrammarCache_evict_stops_at_first_fresh`
- `GrammarCache_failed_load_not_in_lru`
- `GrammarCache_reload_after_evict` — re-loads cleanly with metadata reuse.
- `CoreConfig_defaults_when_missing`
- `CoreConfig_clamps_bad_values`

`GrammarCache` takes an injectable `std::function<SteadyTp()>` clock (defaults to `steady_clock::now`).

### New integration tests

- `CoreSingleton_initialized_once` — two threads call `wlx_core_acquire`, single init via `call_once`.
- `CoreSingleton_handles_concurrent_colorize` — N threads, different languages, no crash.
- `ABI_version_check_round_trip` — header constant matches DLL export.

### Existing tests

- All current `colorizer-tests` and `tests` keep passing, now linked against the import lib.
- `WLX_CORE_TESTING` build flag (set on `tests` and `colorizer-tests` targets only, never on plugin DLLs) exposes test-only constructors so direct-construction tests (e.g. `tests/test_colorizer_grammar.cpp`) keep working without singleton init.
- Visual regression suite (`scripts/visual-test.sh`) — must pass identically. Run after each migration step.

### Out of scope

- Cross-DLL `FreeLibrary` correctness — too OS-bound; relies on existing detach-leak pattern + manual smoke testing in TC.

---

## Migration plan

Each step is a separate commit; each leaves the project building and tests passing.

| Step | Title                                      | Notes                                                                                  |
|------|--------------------------------------------|----------------------------------------------------------------------------------------|
| 1    | Rename + retarget core as SHARED            | `colorizer-core` → `wlx-listerine-core`, `STATIC` → `SHARED`; add `WLX_CORE_API` decoration; both plugins link import lib. No API changes. Verify: tests pass, both `.wlx64`s load in TC. |
| 2    | Add C ABI shim                             | New `include/wlx_core/abi.h` (extern "C" + inline C++ shim). Plugins still use C++ types internally; only the wire format is C. Verify: tests link, smoke run unchanged. |
| 3    | Singleton + GetModuleFileNameW             | Move `Colorizer` instance into core; `call_once`-initialized `CoreRegistry`; mutex; `core_dir` derived from stashed core HMODULE. Plugins replace their `g_colorizer` with `wlx_core_acquire`. Verify: full visual regression unchanged. |
| 4    | LRU + TTL eviction + core TOML             | Replace flat map with `GrammarCache` (map + lru list + injectable clock). Parse `wlx-listerine-core.toml`. Verify: new unit tests pass; visual regression unchanged. |
| 5    | Single-folder packaging                    | `scripts/package.ps1` produces one bundled ZIP; `pluginst.inf` lists both `.wlx64`s. Drop per-plugin `themes/` and `grammars/` from staging. Verify: fresh TC install registers both plugins; themes/grammars resolve from `wlx-listerine/`. |
| 6    | Drop dead per-plugin paths                 | Remove `theme_dir`/`grammar_dir` resolution from `host_adapter.cpp` and `colorizer_host_adapter.cpp`. Update sample TOMLs. |
| 7    | Docs                                       | (5.3 deliverables.)                                                                    |

Each step revertible without affecting the others.

---

## Documentation updates (Step 7)

- **`README.md`** — kill the "Lazy grammar loading" TODO. Update install steps to single bundled ZIP. Update "What's in each ZIP" if documented.
- **`CLAUDE.md`** — update architecture block: `colorizer-core` → `wlx-listerine-core` (shared). Add a "Process-wide cache" paragraph (singleton + LRU + TTL). Note the C ABI boundary and the lockstep-build constraint.
- **`docs/CONFIGURATION.md`** — new section for `wlx-listerine-core.toml` (`[grammar_cache] cap`, `ttl_minutes`). Note themes now live in `wlx-listerine/themes`.
- **`docs/BUILDING.md`** — note the new shared-DLL artifact, the import-lib linkage, and the lockstep-build rule.
- **`docs/LANGUAGES.md`** — update grammar-drop path to single `wlx-listerine/grammars/`.
