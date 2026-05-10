# Markdown / colorizer URL-detection follow-ups — design

**Date:** 2026-05-10
**Status:** approved (brainstorming)
**Source:** four follow-ups logged at the close of the URL-detection branch (session `1fe965c3`, 2026-05-09).

## Goal

Close the four loose ends surfaced during review of the URL-detection branch:

1. Eliminate inline `ShellExecuteW + WLX_TRACE` duplication in both plugins' `EditConfig` handlers.
2. Add doctest coverage for `layout_source`'s URL-detection offset/coordinate math (today: visual-only).
3. Add doctest coverage for `build_colorizer_menu_context`'s URL-hit branch (today: only the no-hit path is tested).
4. Split `InteractionEngine::anchor_y` tests out of `test_text_selection.cpp` into `test_interaction_engine.cpp` so test files mirror source files.

## Non-goals

- No new user-visible behavior. All four items are pure cleanup.
- No refactor of `open_external_url` itself. It is reused as-is for the local config path; `ShellExecuteW open` handles file paths and URLs identically.
- No extraction of a separate URL-layout helper out of `layout_source`. Only build wiring changes for item 2.
- No move of `TextPosition`, `extract_selected_text`, or `find_word_boundaries` tests. Those belong to selection, not interaction-engine, and stay in `test_text_selection.cpp`.

## Components & changes per item

### Item 1 — `EditConfig` migrates to `open_external_url`

**Touches:** `src/plugin_md/window/host_adapter.cpp` (~lines 619–626), `src/plugin_colorizer/window/colorizer_host_adapter.cpp` (~lines 737–744).

Replace each inline block:

```cpp
HINSTANCE hi = ShellExecuteW(nullptr, L"open", ctx.config_path.c_str(),
                             nullptr, nullptr, SW_SHOW);
if (reinterpret_cast<INT_PTR>(hi) <= 32) {
    WLX_TRACE(L"EditConfig: ShellExecuteW failed (code %lld) for %s",
              static_cast<long long>(reinterpret_cast<INT_PTR>(hi)),
              ctx.config_path.c_str());
}
```

with a single call:

```cpp
wlx::runtime::host::open_external_url(ctx.config_path);
```

**Trace-tag note.** The helper emits `"open_external_url: ShellExecuteW failed..."` on failure instead of `"EditConfig: ..."`. Acceptable: the URL string itself identifies the path and `WLX_TRACE_TAG` already names the plugin. We lose the per-call-site label `EditConfig`, gain a single point of failure-logging.

**No tests added.** Replacing a `ShellExecuteW` call (uncovered) with a helper call (also uncovered) leaves coverage unchanged.

### Item 2 — `layout_source` URL-detection doctest

**New file:** `tests/plugin_colorizer/layout/test_colorizer_layout.cpp`.

**CMake changes** (`tests/CMakeLists.txt`):

- Add the new test file to `colorizer-tests` sources.
- Add `${CMAKE_SOURCE_DIR}/src/plugin_colorizer/layout/colorizer_layout.cpp` to compile the production code into the test binary.
- Add `wlx-core` to `target_link_libraries(colorizer-tests PRIVATE ...)` so layout, url_scanner, and parser symbols resolve.

**Test cases (4):**

1. **No URL in source → no `InteractiveSpan` with `ExternalUrl` kind** — negative baseline.
2. **One URL on one line → exactly one `ExternalUrl` span; rect's `left`/`top` lie within the line's block rect; `target.url` matches the source substring** — positive coverage of offset/coordinate math.
3. **URL spans wrapped across two visual rows → multiple spans with the same `target.url`, ordered by `top`** — covers the multi-rect `HitTestTextRange` path. Test sets a small `viewport_width` (~100 dip) and a long URL (~80+ chars) so wrap is deterministic.
4. **URL with trailing punctuation `https://example.com.` → span covers only `https://example.com`** — verifies `scan_urls` trim survives the layout pipeline.

Each test creates a real `IDWriteFactory` (mirrors `test_layout_engine.cpp`), calls `layout_source(...)`, walks `LayoutDocument::blocks[*].spans` looking for `ExternalUrl` kind. No screenshot, no Direct2D paint.

### Item 3 — `build_colorizer_menu_context` URL-hit doctest

**Touches:** `tests/runtime/host/test_context_menu.cpp` (existing file, has `FakeColorizerView` already).

**New test cases (2):**

1. **`build_colorizer_menu_context` populates `ctx.link` from a URL hit** — construct a `LayoutDocument` with one `LayoutBlock` containing an `InteractiveSpan{ kind=ExternalUrl, url=L"https://x", rect={0,0,100,20} }`, attach an `InteractionEngine`, call `build_colorizer_menu_context(vs, langs, 50.0f, 10.0f)`, assert `ctx.link.present == true`, `ctx.link.url == L"https://x"`, `ctx.link.external == true`.
2. **`build_colorizer_menu_context` ignores non-`ExternalUrl` link kinds** — span with `kind=InternalAnchor` (or `RelativeDoc`); assert `ctx.link.present == false`. Defends the deliberate divergence from `build_md_menu_context`, which does surface those kinds.

No CMake change — file is already in the `tests` target.

### Item 4 — Split `anchor_y` tests into `test_interaction_engine.cpp`

**Move:** Lines 349–419 of `tests/runtime/interaction/test_text_selection.cpp` (six `TEST_CASE`s for `InteractionEngine::anchor_y`) → new file `tests/runtime/interaction/test_interaction_engine.cpp`.

**The new file gets:** the doctest include, four `runtime/...` includes (parser, layout, theme, interaction), the `using` aliases for `wlx::runtime::interaction`/`layout`/`parser`/`theme`, and `using Microsoft::WRL::ComPtr`. Same boilerplate as `test_text_selection.cpp` lines 1–19.

**The old file loses:** the six `TEST_CASE` blocks plus any now-unused includes. Re-verify after the move; remaining tests (TextPosition, extract_selected_text, find_word_boundaries) likely keep all four runtime includes in use.

**CMake change:** add the new file to the `tests` target sources list.

## Order, dependencies, commits

The four items are independent — no item's source is touched by another — so any order is correct. We sort by ascending blast radius so trivial wins land first and the build-wiring lever pulls last.

| # | Item | Commit subject | Production code? | CMake change? |
|---|---|---|---|---|
| 1 | Item 4 — anchor_y split | `test(interaction): split anchor_y tests into test_interaction_engine.cpp` | no | yes (1 line) |
| 2 | Item 3 — menu-context URL-hit | `test(host): cover URL-hit branch of build_colorizer_menu_context` | no | no |
| 3 | Item 1 — EditConfig migration | `refactor(plugins): route EditConfig through open_external_url` | yes | no |
| 4 | Item 2 — layout_source doctest | `test(colorizer): doctest URL-detection in layout_source` | no | yes (3 lines + linkage) |

Four separate commits per the scope decision; no squash.

**Push timing:** push decision deferred to end of implementation.

## Error handling, edge cases, risks

### Item 1

- **Trace label change.** Existing logs grep for the literal `"EditConfig: ShellExecuteW failed"`; after migration the literal becomes `"open_external_url: ShellExecuteW failed"`. *Mitigation:* implementation step searches the repo for `EditConfig: ShellExecuteW`. If any hit appears outside the two changed call sites, surface and stop.
- **Path with spaces / non-ASCII.** Inline and helper paths use the same `ShellExecuteW(... L"open" ...)` call shape; behavior identical.

### Item 2

- **Linkage explosion.** Adding `wlx-core` to `colorizer-tests` pulls in the markdown-side parser/layout/interaction symbols. *Risk:* anonymous-namespace name collisions between `colorizer_layout.cpp` and `wlx-core`. *Mitigation:* trial-build the change first; if a collision surfaces, qualify or rename in a sub-step.
- **`IDWriteFactory` lifetime.** New file follows `test_layout_engine.cpp`'s per-test factory creation pattern. No global, no teardown order.
- **Multi-row hit-test brittleness.** Wrap test depends on viewport width. *Mitigation:* hard-code small `viewport_width` (≈100 dip) plus a sufficiently long URL (≥80 chars). DPI-aware code in `layout_source` already operates in dip throughout — test stays deterministic.

### Item 3

- **`InteractionEngine` constructor.** `test_text_selection.cpp:354` already constructs `InteractionEngine eng(layout)` from a `LayoutDocument`. Same constructor used here.
- **Parity-drift trip wire.** The negative test asserts `build_colorizer_menu_context` ignores `InternalAnchor`/`RelativeDoc` kinds — a deliberate divergence from the markdown variant. If the colorizer is later updated to follow internal anchors, this test must be updated alongside. Acceptable trade.

### Item 4

- **Doctest test-case identity.** Doctest identifies tests by the (file, name) pair, not name alone. Moving six `TEST_CASE`s with unchanged names to a new TU is safe.
- **Stale includes.** After removing the six tests, re-verify the four `runtime/...` includes in `test_text_selection.cpp` are still all in use. If any becomes orphaned, drop it. No behavior risk either way.

### Cross-cutting

- **Pre-commit visual-regression hook.** `.githooks/pre-commit` fires on staged `.cpp/.h/.toml` and test `.md` files. Adds ~30 seconds per commit. Cost note, not a risk.
- **Tests-only commits (items 2, 3, 4).** Visual regression should pass unchanged.
- **Item 1.** Same `ShellExecuteW open` semantics, no UI surface change. Visual regression should pass unchanged.
- **Branch model.** Commits land directly on `master`. No feature branch.

## Verification & success criteria

Per-commit checks (run before each `git commit`):

Numbers below match commit-landing position (1 = first to land, 4 = last) per the table above:

| Position | Subject | Build | Markdown tests | Colorizer tests | Visual |
|---|---|---|---|---|---|
| 1 | anchor_y split | `cmake --build --preset conan-release` | `tests.exe` PASS, count = 203 (unchanged) | `colorizer-tests.exe` PASS, 137 (unchanged) | all PASS |
| 2 | menu-context URL-hit | same | `tests.exe` PASS, count = **205** (203 + 2) | unchanged | unchanged |
| 3 | EditConfig migration | same | unchanged | unchanged | unchanged |
| 4 | layout_source doctest | same | unchanged | `colorizer-tests.exe` PASS, count = **141** (137 + 4) | unchanged |

**Final state target.**

- Markdown `tests.exe` 205/205 PASS.
- `colorizer-tests.exe` 141/141 PASS.
- Visual: 29/29 markdown + 26/26 colorizer tokens + 6/6 colorizer pixels (unchanged).
- 4 new commits on `master` (on top of whatever unpushed work already exists).
- `git status` clean.

**Implementation-time checks (must pass before claiming done):**

1. Repo grep for `EditConfig: ShellExecuteW` outside the two changed call sites — must yield nothing.
2. Diff of `tests/runtime/interaction/test_text_selection.cpp` shows only the six `TEST_CASE` removals plus any unused-include trim; no edits to remaining tests.
3. `test_interaction_engine.cpp` compiles and registers exactly 6 tests under `-tc=InteractionEngine::anchor_y*`.
4. `test_colorizer_layout.cpp` registers exactly 4 tests; all pass.
5. `target_link_libraries(colorizer-tests ...)` includes `wlx-core` after the edit.
6. `tests/CMakeLists.txt` lists both new test files plus the `colorizer_layout.cpp` source for the colorizer-tests target.

Success = all of the above plus the `superpowers:verification-before-completion` checklist (evidence before assertion).

**Stop conditions** (halt implementation and surface to user):

- Symbol collision when adding `wlx-core` linkage to `colorizer-tests`.
- New `layout_source` test reveals a real bug in URL-detection coordinate math (visual tests were tolerant of something they shouldn't be — decide before fixing).
- Total test count doesn't match the math above (hidden duplicate or skipped test).
