# Colorizer Visual Regression Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an automated visual regression suite for the colorizer plugin, mirroring the markdown side: per-token JSON snapshots (27 goldens) for grammar/theme regressions and pixel-PNG smokes (5 goldens) for render-engine axes — all driven by `scripts/visual-test.sh` and `bun run update-goldens`.

**Architecture:** Three additions to one existing pipeline. (1) Extend `screenshot_tool.exe` with a colorizer mode + `--dump-tokens` flag. (2) Add `compare_tokens.py` next to `compare.py`. (3) Extend `visual-test.sh`, `update-goldens.ts`, and `.githooks/pre-commit` to cover both. No ABI change; no new binaries; no new top-level dependencies.

**Tech Stack:** C++20 / MSVC / CMake 3.20+ (existing), Conan 2.x, doctest, Direct2D / DirectWrite, tree-sitter via `wlx-listerine-core.dll`, Python 3 + Pillow via `uv`, Bun + Playwright (existing for markdown side; not used by colorizer side).

**Reference:** `docs/superpowers/specs/2026-05-05-colorizer-visual-regression-design.md` — read this if any task feels under-specified or you need *why* context.

---

## Common Procedures

These rituals repeat across many tasks. Defined once; each task references them by name.

### Build ritual (`BUILD_OK`)

After C++ changes:

```bash
cmake --build --preset conan-release
```

**Expected:** clean build, zero warnings escalated to errors. If anything fails, stop and fix — do not commit a red state.

### Build + test ritual (`BUILD_TEST_OK`)

After C++ changes that touch behavior or tests:

```bash
cmake --build --preset conan-release
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

**Expected:** clean build, both binaries report `0 failed`.

### Existing visual regression (`MD_VISUAL_OK`)

```bash
./scripts/visual-test.sh
```

**Expected (after Stage 7 onward):** Stage 1 (markdown) all PASS, Stage 2 (colorizer tokens) all PASS, Stage 3 (colorizer pixel smokes) all PASS. Before Stage 7, only Stage 1 exists.

### Python self-tests (`PY_TEST_OK`)

```bash
uv run --with pytest pytest test_data/test_compare_tokens.py -v
```

**Expected:** all tests pass. Used after Stage 5.

### Manual smoke commands (`SMOKE_TOOL`)

For each stage that touches `screenshot_tool`:

```bash
./build/Release/screenshot_tool.exe test_data/cases/01_headings_atx.md --full
```

**Expected:** writes `test_data/cases/01_headings_atx.png`, prints the path on stdout, exits 0. Confirms the markdown path still works after refactor.

---

## Stage 0 — Pre-flight

### Task 0.1: Confirm baseline is green

**Files:** none (sanity check only).

- [ ] **Step 1: Build the existing project**

```bash
cmake --build --preset conan-release
```

Expected: clean build.

- [ ] **Step 2: Run the existing test suites**

```bash
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

Expected: both green. Capture the test counts (e.g. "86 passed, 0 failed" + colorizer count) and note them — they're your "didn't break anything" baseline.

- [ ] **Step 3: Run the existing visual regression**

```bash
./scripts/visual-test.sh
```

Expected: 28 markdown cases all PASS. If anything is red on master *before* you start, stop and report — don't proceed on a red baseline.

- [ ] **Step 4: No commit** — this stage is verification only.

---

## Stage 1 — Token JSON writer (header-only, TDD)

The writer converts a `ColorizeResult` (UTF-8 byte-indexed `ColorSpan`s) plus the source string into a deterministic JSON document. Header-only so both `screenshot_tool` and `colorizer-tests` can use it without library plumbing.

### Task 1.1: Write the failing JSON-writer determinism test

**Files:**
- Create: `tests/tools/test_token_json_writer.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source to `colorizer-tests`)

- [ ] **Step 1: Create the failing test**

Write `tests/tools/test_token_json_writer.cpp`:

```cpp
#include <doctest/doctest.h>

#include "tools/screenshot/token_json_writer.h"
#include "core_dll/colorizer/colorize_result.h"

#include <string>

using wlx::core::colorizer::ColorSpan;
using wlx::core::colorizer::ColorizeResult;
using wlx::tools::screenshot::TokenJsonWriter;
using wlx::tools::screenshot::TokenJsonOptions;

namespace {

constexpr uint8_t MOD_BOLD      = 1 << 0;
constexpr uint8_t MOD_ITALIC    = 1 << 1;
constexpr uint8_t MOD_UNDERLINE = 1 << 2;

ColorSpan span(uint32_t start, uint32_t length, uint32_t color, uint8_t mods) {
    ColorSpan s;
    s.start = start;
    s.length = length;
    s.color = color;
    s.modifiers = mods;
    return s;
}

TokenJsonOptions test_opts() {
    TokenJsonOptions opt;
    opt.source_name   = "synthetic.cpp";
    opt.language      = "cpp";
    opt.theme_name    = "test_dark";
    opt.config_hash   = "deadbeef";
    return opt;
}

}  // namespace

TEST_CASE("TokenJsonWriter: empty result produces empty tokens array") {
    ColorizeResult cr;
    std::string out = TokenJsonWriter::write(cr, /*source=*/"", test_opts());
    CHECK(out.find("\"token_count\": 0") != std::string::npos);
    CHECK(out.find("\"tokens\": []") != std::string::npos);
}

TEST_CASE("TokenJsonWriter: adjacent identical-style tokens collapse") {
    ColorizeResult cr;
    // "abcdef" — two spans (0..3) and (3..6), same color and mods → must collapse
    cr.spans = {
        span(0, 3, 0xFF7B72, MOD_BOLD),
        span(3, 3, 0xFF7B72, MOD_BOLD),
    };
    std::string out = TokenJsonWriter::write(cr, "abcdef", test_opts());
    CHECK(out.find("\"token_count\": 1") != std::string::npos);
    CHECK(out.find("\"len\": 6") != std::string::npos);
}

TEST_CASE("TokenJsonWriter: tokens are sorted by (line, col, -len)") {
    ColorizeResult cr;
    // Out-of-order input. Two spans starting at byte 5, lengths 3 and 8 — longer first.
    cr.spans = {
        span(10, 2, 0xAAAAAA, 0),
        span( 5, 3, 0xBBBBBB, 0),
        span( 5, 8, 0xCCCCCC, 0),  // longer span at same start → sorts first
    };
    std::string out = TokenJsonWriter::write(cr, "0123456789ABCDEFGHIJ", test_opts());
    // Find the three "len": entries in order; first len after the third "line" should be 8.
    auto p1 = out.find("\"len\": 8");
    auto p2 = out.find("\"len\": 3");
    auto p3 = out.find("\"len\": 2");
    REQUIRE(p1 != std::string::npos);
    REQUIRE(p2 != std::string::npos);
    REQUIRE(p3 != std::string::npos);
    CHECK(p1 < p2);
    CHECK(p2 < p3);
}

TEST_CASE("TokenJsonWriter: modifiers are alphabetized lowercase") {
    ColorizeResult cr;
    cr.spans = { span(0, 4, 0xFFFFFF, MOD_UNDERLINE | MOD_BOLD | MOD_ITALIC) };
    std::string out = TokenJsonWriter::write(cr, "test", test_opts());
    CHECK(out.find("\"mods\": [\"bold\", \"italic\", \"underline\"]") != std::string::npos);
}

TEST_CASE("TokenJsonWriter: byte offsets convert to 1-based char line/col over UTF-8") {
    ColorizeResult cr;
    // Source: "ab\nαβγ" (UTF-8: a b \n α=2B β=2B γ=2B)
    // Span at byte 3 (start of α) length 2 (just α) → wchar offset 3 → line 2, col 1, len 1
    cr.spans = { span(3, 2, 0xFF0000, 0) };
    std::string out = TokenJsonWriter::write(cr, "ab\n\xCE\xB1\xCE\xB2\xCE\xB3", test_opts());
    CHECK(out.find("\"line\": 2") != std::string::npos);
    CHECK(out.find("\"col\": 1") != std::string::npos);
    CHECK(out.find("\"len\": 1") != std::string::npos);
}

TEST_CASE("TokenJsonWriter: identical input produces identical bytes across two calls") {
    ColorizeResult cr;
    cr.spans = {
        span(0, 5, 0x123456, MOD_BOLD),
        span(5, 5, 0x789ABC, 0),
    };
    std::string a = TokenJsonWriter::write(cr, "0123456789", test_opts());
    std::string b = TokenJsonWriter::write(cr, "0123456789", test_opts());
    CHECK(a == b);
}

TEST_CASE("TokenJsonWriter: header fields appear in canonical order") {
    ColorizeResult cr;
    std::string out = TokenJsonWriter::write(cr, "", test_opts());
    auto p_source       = out.find("\"source\":");
    auto p_language     = out.find("\"language\":");
    auto p_theme        = out.find("\"theme\":");
    auto p_config_hash  = out.find("\"config_hash\":");
    auto p_token_count  = out.find("\"token_count\":");
    auto p_tokens       = out.find("\"tokens\":");
    CHECK(p_source < p_language);
    CHECK(p_language < p_theme);
    CHECK(p_theme < p_config_hash);
    CHECK(p_config_hash < p_token_count);
    CHECK(p_token_count < p_tokens);
}
```

- [ ] **Step 2: Wire the test into `colorizer-tests`**

Edit `tests/CMakeLists.txt`. Find the `add_executable(colorizer-tests …)` block and add `tools/test_token_json_writer.cpp` to its source list (alphabetically near the other `tools/` would be — there are none yet, so put it after the `plugin_colorizer/` lines):

```cmake
add_executable(colorizer-tests
    ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
    core_dll/colorizer/test_colorizer.cpp
    core_dll/theme/test_colorizer_helix_theme.cpp
    core_dll/grammar/test_colorizer_grammar.cpp
    core_dll/highlighting/test_colorizer_query_highlighter.cpp
    core_dll/grammar/test_colorizer_grammars.cpp
    plugin_colorizer/language/test_colorizer_routing.cpp
    core_dll/abi/test_wlx_core_abi.cpp
    core_dll/registry/test_core_config.cpp
    core_dll/grammar/test_grammar_cache.cpp
    tools/test_token_json_writer.cpp
)
```

- [ ] **Step 3: Run the test — confirm it fails**

```bash
cmake --build --preset conan-release
```

Expected: **build error** — `tools/screenshot/token_json_writer.h` does not exist. This is the failing test state. Do not proceed past this until you've reproduced the build error.

- [ ] **Step 4: No commit yet** — failing tests don't get committed standalone.

### Task 1.2: Implement the JSON writer

**Files:**
- Create: `src/tools/screenshot/token_json_writer.h`

- [ ] **Step 1: Implement the writer**

Write `src/tools/screenshot/token_json_writer.h`:

```cpp
#pragma once

#include "core_dll/colorizer/color_span.h"
#include "core_dll/colorizer/colorize_result.h"
#include "text_modifiers.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace wlx::tools::screenshot {

struct TokenJsonOptions {
    std::string source_name;     // basename, e.g. "sample.cpp"
    std::string language;        // tree-sitter language id, e.g. "cpp"
    std::string theme_name;      // e.g. "default_dark"
    std::string config_hash;     // SHA1 hex; computed by caller
};

// Header-only deterministic JSON serializer for ColorizeResult.
//
// Output is two-space indented, with header keys emitted in this fixed order:
//   source, language, theme, config_hash, token_count, tokens
// Tokens are emitted as a sorted, adjacent-collapsed array. Each token has
// keys (line, col, len, color, mods) in that order.
//
// Byte→char offset conversion walks the UTF-8 source assuming valid UTF-8
// (the colorizer rejects invalid encodings upstream). Line/col are 1-based
// over wchar positions in the source.
class TokenJsonWriter {
public:
    static std::string write(const wlx::core::colorizer::ColorizeResult& result,
                             std::string_view source_utf8,
                             const TokenJsonOptions& opts);

private:
    struct Token {
        uint32_t line;
        uint32_t col;
        uint32_t len;
        uint32_t color;
        uint8_t  mods;
    };

    // Build (byte_offset → wchar_offset, line, col) lookup over the source.
    // Returns a vector indexed by byte offset; the entry at byte b gives the
    // (wchar_offset, line, col) of the wchar starting at that byte. Only byte
    // positions at code-point starts have valid line/col; other bytes still
    // get values (mid-codepoint bytes inherit the prior code point's).
    struct ByteSite { uint32_t wchar_off; uint32_t line; uint32_t col; };

    static std::vector<ByteSite> index_source(std::string_view src);

    static std::vector<Token> build_tokens(
        const wlx::core::colorizer::ColorizeResult& result,
        const std::vector<ByteSite>& sites,
        std::string_view src);

    static void sort_and_collapse(std::vector<Token>& tokens);

    static std::string mods_array(uint8_t modifiers);
    static std::string color_hex(uint32_t rgb);
};

inline std::vector<TokenJsonWriter::ByteSite>
TokenJsonWriter::index_source(std::string_view src) {
    std::vector<ByteSite> sites(src.size() + 1);
    uint32_t line = 1;
    uint32_t col = 1;
    uint32_t wch = 0;
    size_t i = 0;
    while (i <= src.size()) {
        sites[i] = { wch, line, col };
        if (i == src.size()) break;
        unsigned char c = static_cast<unsigned char>(src[i]);
        size_t step = 1;
        if      ((c & 0x80) == 0x00) step = 1;
        else if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;
        // Mid-codepoint bytes carry the same line/col so byte-offset lookups
        // always land on something sensible.
        for (size_t k = 1; k < step && i + k < src.size(); ++k)
            sites[i + k] = { wch, line, col };
        // Treat surrogate pairs (U+10000+) as 2 wchars on Windows. A 4-byte
        // UTF-8 sequence yields one code point but two wchar units in UTF-16.
        if (step == 4) wch += 2;
        else           wch += 1;
        if (c == '\n') { ++line; col = 1; }
        else           { ++col; }
        i += step;
    }
    return sites;
}

inline std::vector<TokenJsonWriter::Token>
TokenJsonWriter::build_tokens(const wlx::core::colorizer::ColorizeResult& result,
                              const std::vector<ByteSite>& sites,
                              std::string_view src) {
    std::vector<Token> out;
    out.reserve(result.spans.size());
    for (const auto& s : result.spans) {
        if (s.length == 0) continue;
        if (s.start >= sites.size()) continue;
        const auto& at_start = sites[s.start];
        size_t end_byte = static_cast<size_t>(s.start) + s.length;
        if (end_byte > src.size()) end_byte = src.size();
        const auto& at_end = sites[end_byte];
        Token t;
        t.line  = at_start.line;
        t.col   = at_start.col;
        t.len   = at_end.wchar_off - at_start.wchar_off;
        t.color = s.color & 0x00FFFFFFu;
        t.mods  = s.modifiers;
        if (t.len > 0) out.push_back(t);
    }
    return out;
}

inline void TokenJsonWriter::sort_and_collapse(std::vector<Token>& tokens) {
    std::sort(tokens.begin(), tokens.end(), [](const Token& a, const Token& b){
        if (a.line != b.line) return a.line < b.line;
        if (a.col  != b.col ) return a.col  < b.col;
        return a.len > b.len;  // longer span wins on ties
    });
    std::vector<Token> merged;
    merged.reserve(tokens.size());
    for (auto& t : tokens) {
        if (!merged.empty()) {
            auto& prev = merged.back();
            // Adjacent: prev ends exactly where t starts (same line, col matches),
            // and (color, mods) match → merge.
            bool adjacent =
                prev.line == t.line &&
                prev.col + prev.len == t.col &&
                prev.color == t.color &&
                prev.mods  == t.mods;
            if (adjacent) {
                prev.len += t.len;
                continue;
            }
        }
        merged.push_back(t);
    }
    tokens.swap(merged);
}

inline std::string TokenJsonWriter::mods_array(uint8_t modifiers) {
    using namespace wlx::core::theme;
    std::vector<const char*> names;
    if (modifiers & MOD_BOLD)          names.push_back("bold");
    if (modifiers & MOD_ITALIC)        names.push_back("italic");
    if (modifiers & MOD_STRIKETHROUGH) names.push_back("strikethrough");
    if (modifiers & MOD_UNDERLINE)     names.push_back("underline");
    // names are inserted in alphabetical order by construction.
    std::string out = "[";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) out += ", ";
        out += "\"";
        out += names[i];
        out += "\"";
    }
    out += "]";
    return out;
}

inline std::string TokenJsonWriter::color_hex(uint32_t rgb) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06X", rgb & 0x00FFFFFFu);
    return std::string(buf);
}

inline std::string TokenJsonWriter::write(
    const wlx::core::colorizer::ColorizeResult& result,
    std::string_view source_utf8,
    const TokenJsonOptions& opts)
{
    auto sites  = index_source(source_utf8);
    auto tokens = build_tokens(result, sites, source_utf8);
    sort_and_collapse(tokens);

    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"source\": \""       << opts.source_name << "\",\n";
    ss << "  \"language\": \""     << opts.language    << "\",\n";
    ss << "  \"theme\": \""        << opts.theme_name  << "\",\n";
    ss << "  \"config_hash\": \""  << opts.config_hash << "\",\n";
    ss << "  \"token_count\": "    << tokens.size()    << ",\n";
    if (tokens.empty()) {
        ss << "  \"tokens\": []\n";
    } else {
        ss << "  \"tokens\": [\n";
        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& t = tokens[i];
            ss << "    { \"line\": " << t.line
               << ", \"col\": "      << t.col
               << ", \"len\": "      << t.len
               << ", \"color\": \""  << color_hex(t.color)
               << "\", \"mods\": "   << mods_array(t.mods)
               << " }";
            if (i + 1 != tokens.size()) ss << ",";
            ss << "\n";
        }
        ss << "  ]\n";
    }
    ss << "}\n";
    return ss.str();
}

}  // namespace wlx::tools::screenshot
```

Note: `text_modifiers.h` already lives at `src/text_modifiers.h` (per CLAUDE.md, defining `MOD_BOLD`/`MOD_ITALIC`/`MOD_UNDERLINE`/`MOD_STRIKETHROUGH` in `wlx::core::theme`). Verify the namespace by reading that file once before this task; if the namespace differs, use whatever the file declares.

- [ ] **Step 2: Run the tests**

```bash
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe
```

Expected: all 7 `TokenJsonWriter:` tests PASS plus existing tests still PASS.

- [ ] **Step 3: Commit**

```bash
git add src/tools/screenshot/token_json_writer.h \
        tests/tools/test_token_json_writer.cpp \
        tests/CMakeLists.txt
git -c commit.gpgsign=false commit -m "feat(tools/screenshot): add deterministic TokenJsonWriter

Header-only serializer for ColorizeResult. Sorts by (line, col, -len),
collapses adjacent identical-style spans, alphabetizes modifiers, emits
canonical key order. Handles UTF-8 byte→wchar offset conversion via a
single source-walk index. 7 doctest cases in colorizer-tests.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Stage 2 — Wiring-alive smoke (TDD)

A trivial doctest that proves the colorizer can load `sample.cpp` and produce a non-empty token stream — catches "core DLL won't load" / "grammar dir empty" before `visual-test.sh` ever tries.

### Task 2.1: Write the failing wiring-alive smoke

**Files:**
- Create: `tests/plugin_colorizer/test_colorizer_smoke.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the test**

Write `tests/plugin_colorizer/test_colorizer_smoke.cpp`:

```cpp
#include <doctest/doctest.h>

#include "core_dll/colorizer/colorizer.h"
#include "runtime/io/file_service.h"

#include <filesystem>

using wlx::core::colorizer::Colorizer;
using wlx::runtime::io::FileService;
namespace fs = std::filesystem;

static const bool grammars_present = fs::exists("grammars/cpp/tree-sitter-cpp.dll");
static const bool sample_present   = fs::exists("test_data/grammar_samples/sample.cpp");

TEST_CASE("smoke: colorize sample.cpp produces a non-empty token stream"
    * doctest::skip(!grammars_present || !sample_present)) {

    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("cpp"));

    FileService fs_svc;
    auto content = fs_svc.read(L"test_data/grammar_samples/sample.cpp");
    REQUIRE(content.has_value());
    REQUIRE_FALSE(content->raw_utf8.empty());

    auto result = c.colorize(content->raw_utf8, "cpp", /*dark_mode=*/true);
    CHECK(result.spans.size() > 100);  // sample.cpp is ~300 LOC; expect plenty of spans

    // At least one span must have a non-zero color (otherwise the theme
    // didn't resolve anything → silent breakage).
    bool any_colored = false;
    for (const auto& s : result.spans) {
        if (s.color != 0) { any_colored = true; break; }
    }
    CHECK(any_colored);
}
```

- [ ] **Step 2: Wire into `colorizer-tests`**

Add `plugin_colorizer/test_colorizer_smoke.cpp` to the `add_executable(colorizer-tests …)` list in `tests/CMakeLists.txt` (alphabetically near `plugin_colorizer/language/test_colorizer_routing.cpp`):

```cmake
plugin_colorizer/language/test_colorizer_routing.cpp
plugin_colorizer/test_colorizer_smoke.cpp
```

- [ ] **Step 3: Build + run, confirm PASS**

```bash
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe
```

Expected: the new test runs (not skipped, since grammars and sample.cpp are present after `BUILD_OK`) and PASSES. If it's skipped, your build's working dir doesn't have `grammars/` next to it — investigate the POST_BUILD step in `src/core_dll/CMakeLists.txt`. If it FAILS, the colorizer is genuinely broken on master — stop and report.

- [ ] **Step 4: Commit**

```bash
git add tests/plugin_colorizer/test_colorizer_smoke.cpp tests/CMakeLists.txt
git -c commit.gpgsign=false commit -m "test(colorizer): add wiring-alive smoke for sample.cpp

Catches \"core DLL won't load\" / \"grammar dir empty\" before
visual-test.sh ever tries. Skipped (not failed) when grammars/ or
sample.cpp are absent from the working directory.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Stage 3 — `screenshot_tool` extraction + colorizer pipeline

Refactor first (extract the existing markdown body into a peer file), then add the colorizer pipeline next to it. Each task is atomic — a refactor task is a refactor task; behavior changes are separate.

### Task 3.1: Extract markdown pipeline (refactor, no behavior change)

**Files:**
- Create: `src/tools/screenshot/markdown_pipeline.h`
- Create: `src/tools/screenshot/markdown_pipeline.cpp`
- Modify: `src/tools/screenshot/main.cpp` (move body, retain arg parsing + dispatch)
- Modify: `src/tools/screenshot/CMakeLists.txt` (add new source)

- [ ] **Step 1: Read the current pipeline**

Open `src/tools/screenshot/main.cpp`. The `run_pipeline` lambda inside `main()` (~lines 206–400) is the markdown rendering logic. The `Options` struct (~lines 40–53) is the arg state. Note that the lambda captures `opts`, `mem_before`, and `out_path` by reference.

- [ ] **Step 2: Create the header**

Write `src/tools/screenshot/markdown_pipeline.h`:

```cpp
#pragma once

#include <string>

namespace wlx::tools::screenshot {

struct Options;  // defined in options.h (next task)

// Run the markdown rendering pipeline (read → parse → layout → paint → save).
// Returns the path of the written PNG on success, empty string on failure
// (error already printed to stderr). All COM objects are created and released
// inside this function — caller is responsible for CoInitialize/CoUninitialize.
std::wstring run_markdown_pipeline(const Options& opts);

}  // namespace wlx::tools::screenshot
```

- [ ] **Step 3: Extract `Options` into its own header**

Create `src/tools/screenshot/options.h` and move the `Options` struct from `main.cpp` (lines ~40–53) into it:

```cpp
#pragma once

#include <string>

namespace wlx::tools::screenshot {

struct Options {
    std::wstring input_path;
    std::wstring config_path = L"config/wlx-listerine-md.toml";
    int   width  = 800;
    int   height = 600;
    float scroll = 0;
    bool  full   = false;
    bool  dark   = false;
    bool  bench  = false;

    std::wstring search;     // empty == no search
    int          search_step = 0;

    // Colorizer-mode fields (added in Task 3.3 — reserved here to keep the
    // header stable across the refactor stages).
    bool         colorizer        = false;
    std::wstring lang;            // empty = infer from extension
    std::wstring cpp_grammar;     // "standard" | "unreal" | empty
    bool         dump_tokens      = false;
    std::wstring display_config;  // optional TOML override path
};

}  // namespace wlx::tools::screenshot
```

- [ ] **Step 4: Create `markdown_pipeline.cpp`**

Move the contents of the current `run_pipeline` lambda body (lines 206–400 of `main.cpp`) into `src/tools/screenshot/markdown_pipeline.cpp`, wrapped as a free function:

```cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tools/screenshot/markdown_pipeline.h"
#include "tools/screenshot/options.h"

#include <windows.h>
#include <psapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

#include "runtime/io/file_service.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/layout/layout_engine.h"
#include "runtime/render/render_engine.h"
#include "runtime/theme/theme_service.h"
#include "wlx_core/abi.h"
#include "runtime/search/search_index.h"
#include "runtime/search/search_hud_painter.h"

using namespace wlx::runtime::io;
using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;
using namespace wlx::runtime::render;
using namespace wlx::runtime::search;
using namespace wlx::runtime::theme;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace wlx::tools::screenshot {

namespace {

// Move the existing now_ms / timer_freq / process_working_set /
// estimate_document_memory / estimate_layout_memory helpers here unchanged
// from main.cpp.

// ... (same body as the current main.cpp lines 56–103) ...

}  // namespace

std::wstring run_markdown_pipeline(const Options& opts) {
    // Build out_path (was previously done before the lambda in main.cpp).
    fs::path input(opts.input_path);
    std::wstring stem = input.stem().wstring();
    std::wstring out_name = stem + (opts.dark ? L"_dark.png" : L".png");
    fs::path out_path = input.parent_path().empty()
        ? fs::path(out_name)
        : input.parent_path() / out_name;

    std::error_code ec;
    fs::create_directories(out_path.parent_path(), ec);
    if (ec) {
        std::fprintf(stderr, "Cannot create output directory: %s\n", ec.message().c_str());
        return {};
    }

    size_t mem_before = opts.bench ? process_working_set() : 0;

    double t0 = now_ms();

    // ... paste the entire body of the existing run_pipeline lambda from
    //     main.cpp here, replacing the lambda's `opts.` and `mem_before`
    //     captures with the function's parameters. The lambda's `return
    //     out_path.wstring();` becomes the function return.
    // The existing benchmark-printing block stays as-is.

    return out_path.wstring();
}

}  // namespace wlx::tools::screenshot
```

Replace the `// ...` blocks with verbatim code moved from `main.cpp`. Do not change any logic; this is a pure extraction.

- [ ] **Step 5: Slim `main.cpp` to arg parsing + dispatch**

Replace the body of `main.cpp` so it parses args (using `Options` from `options.h`) and calls `run_markdown_pipeline`:

```cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "tools/screenshot/options.h"
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
        "  --search-step N  Advance the search cursor by N steps (default 0)\n");
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

    std::wstring result = run_markdown_pipeline(opts);

    CoUninitialize();

    if (result.empty()) return 1;
    std::fprintf(stdout, "%ls\n", result.c_str());
    return 0;
}
```

- [ ] **Step 6: Update `src/tools/screenshot/CMakeLists.txt`**

```cmake
add_executable(screenshot_tool
    main.cpp
    markdown_pipeline.cpp
)
```

(The `target_include_directories`, `target_link_libraries`, and `set_target_properties` blocks stay unchanged.)

- [ ] **Step 7: BUILD_OK + SMOKE_TOOL**

Verify that the markdown path still works and produces an identical PNG to before the refactor:

```bash
cmake --build --preset conan-release
./build/Release/screenshot_tool.exe test_data/cases/01_headings_atx.md --full
./scripts/visual-test.sh
```

Expected: stage 1 of `visual-test.sh` is still all PASS. (No new stages exist yet.)

- [ ] **Step 8: Commit**

```bash
git add src/tools/screenshot/main.cpp \
        src/tools/screenshot/markdown_pipeline.h \
        src/tools/screenshot/markdown_pipeline.cpp \
        src/tools/screenshot/options.h \
        src/tools/screenshot/CMakeLists.txt
git -c commit.gpgsign=false commit -m "refactor(tools/screenshot): extract markdown pipeline + Options header

Pure refactor — no behavior change. Splits the run_pipeline lambda out
of main.cpp into a free function in markdown_pipeline.cpp. main.cpp now
parses args (via Options in options.h) and dispatches. visual-test.sh
stage 1 unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task 3.2: Add new flags + dispatch shell

**Files:**
- Create: `src/tools/screenshot/colorizer_pipeline.h`
- Create: `src/tools/screenshot/colorizer_pipeline.cpp` (stub returning empty)
- Modify: `src/tools/screenshot/main.cpp` (parse new flags, dispatch on extension)
- Modify: `src/tools/screenshot/CMakeLists.txt`

- [ ] **Step 1: Create the colorizer pipeline header**

Write `src/tools/screenshot/colorizer_pipeline.h`:

```cpp
#pragma once

#include <string>

namespace wlx::tools::screenshot {

struct Options;

// Run the colorizer pipeline. In paint mode, writes a PNG and returns its path.
// In --dump-tokens mode, writes a JSON file and returns its path.
// Returns empty string on failure (error already printed to stderr).
std::wstring run_colorizer_pipeline(const Options& opts);

}  // namespace wlx::tools::screenshot
```

- [ ] **Step 2: Create the stub `.cpp`**

Write `src/tools/screenshot/colorizer_pipeline.cpp`:

```cpp
#include "tools/screenshot/colorizer_pipeline.h"
#include "tools/screenshot/options.h"

#include <cstdio>

namespace wlx::tools::screenshot {

std::wstring run_colorizer_pipeline(const Options& /*opts*/) {
    std::fprintf(stderr, "Colorizer pipeline not implemented yet (Task 3.3 / 3.4)\n");
    return {};
}

}  // namespace wlx::tools::screenshot
```

- [ ] **Step 3: Extend arg parser + dispatch in `main.cpp`**

Update `parse_args` in `main.cpp` to accept the new flags. After the existing `--search-step` branch, add:

```cpp
        else if (std::strcmp(argv[i], "--colorizer")     == 0) opts.colorizer = true;
        else if (std::strcmp(argv[i], "--lang")          == 0 && i + 1 < argc) opts.lang = to_wstring(argv[++i]);
        else if (std::strcmp(argv[i], "--cpp-grammar")   == 0 && i + 1 < argc) opts.cpp_grammar = to_wstring(argv[++i]);
        else if (std::strcmp(argv[i], "--dump-tokens")   == 0) opts.dump_tokens = true;
        else if (std::strcmp(argv[i], "--display-config")== 0 && i + 1 < argc) opts.display_config = to_wstring(argv[++i]);
```

Update `print_usage` to document them:

```cpp
        "  --colorizer           Force colorizer mode (else inferred from extension)\n"
        "  --lang <id>           Override grammar language (else inferred from extension)\n"
        "  --cpp-grammar <kind>  \"standard\" or \"unreal\" — selects cpp grammar variant\n"
        "  --dump-tokens         Write resolved-style token JSON instead of painting\n"
        "  --display-config <p>  TOML overrides for ColorizerDisplayConfig\n"
```

Replace the `main()` body's call to `run_markdown_pipeline` with a routing decision:

```cpp
#include "tools/screenshot/colorizer_pipeline.h"
// ... in main() ...

    auto ends_with = [](const std::wstring& s, const std::wstring& suf) {
        return s.size() >= suf.size() &&
               std::equal(suf.begin(), suf.end(), s.end() - suf.size());
    };

    bool is_md = !opts.colorizer && ends_with(opts.input_path, L".md");

    std::wstring result = is_md ? run_markdown_pipeline(opts)
                                : run_colorizer_pipeline(opts);
```

Add `#include <algorithm>` near the other headers if not present.

- [ ] **Step 4: Update CMakeLists**

```cmake
add_executable(screenshot_tool
    main.cpp
    markdown_pipeline.cpp
    colorizer_pipeline.cpp
)
```

- [ ] **Step 5: BUILD_OK + verify dispatch**

```bash
cmake --build --preset conan-release
./build/Release/screenshot_tool.exe test_data/cases/01_headings_atx.md --full
echo "---"
./build/Release/screenshot_tool.exe test_data/grammar_samples/sample.cpp --colorizer --dump-tokens
```

Expected: first command writes the PNG and exits 0. Second command prints "Colorizer pipeline not implemented yet" and exits 1. Existing markdown visual tests still pass.

- [ ] **Step 6: Commit**

```bash
git add src/tools/screenshot/main.cpp \
        src/tools/screenshot/colorizer_pipeline.h \
        src/tools/screenshot/colorizer_pipeline.cpp \
        src/tools/screenshot/options.h \
        src/tools/screenshot/CMakeLists.txt
git -c commit.gpgsign=false commit -m "feat(tools/screenshot): add colorizer-mode flags + dispatch (stub pipeline)

Adds --colorizer, --lang, --cpp-grammar, --dump-tokens, --display-config.
Extension-based routing (.md → markdown, else → colorizer). Colorizer
pipeline is a stub at this stage — implementation lands in 3.3 (paint)
and 3.4 (dump-tokens).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task 3.3: Implement colorizer paint mode

**Files:**
- Modify: `src/tools/screenshot/colorizer_pipeline.cpp`

- [ ] **Step 1: Implement paint mode**

Replace `colorizer_pipeline.cpp` with the real implementation. The structure mirrors `markdown_pipeline.cpp`: COM factories → ThemeService → core acquire → file read → colorize → layout_source → render → save_to_png.

```cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tools/screenshot/colorizer_pipeline.h"
#include "tools/screenshot/options.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "runtime/io/file_service.h"
#include "runtime/render/render_engine.h"
#include "runtime/theme/theme_service.h"
#include "core_dll/colorizer/colorizer.h"
#include "plugin_colorizer/layout/colorizer_layout.h"
#include "plugin_colorizer/language/routing.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

using wlx::core::colorizer::Colorizer;
using wlx::core::colorizer::ColorizeResult;
using wlx::plugin_colorizer::layout::ColorizerDisplayConfig;
using wlx::plugin_colorizer::layout::CppGrammar;
using wlx::plugin_colorizer::layout::layout_source;
using wlx::plugin_colorizer::language::apply_cpp_variant;
using wlx::runtime::io::FileService;
using wlx::runtime::render::RenderEngine;
using wlx::runtime::theme::ThemeService;

namespace wlx::tools::screenshot {

namespace {

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::string infer_language(const std::wstring& path, const std::wstring& override_lang) {
    if (!override_lang.empty()) return narrow(override_lang);
    fs::path p(path);
    std::wstring ext = p.extension().wstring();
    if (!ext.empty() && ext.front() == L'.') ext.erase(0, 1);
    // Map a few common extensions to grammar names. The colorizer routing
    // primitive does the unreal-cpp swap; this map is just extension → id.
    static const std::pair<std::wstring, std::string> kMap[] = {
        {L"c",   "c"},   {L"cpp", "cpp"}, {L"cc",  "cpp"}, {L"h",   "cpp"},
        {L"hpp", "cpp"}, {L"cs",  "c-sharp"}, {L"go",  "go"}, {L"py",  "python"},
        {L"rs",  "rust"}, {L"js",  "javascript"}, {L"ts",  "typescript"},
        {L"json","json"}, {L"toml","toml"}, {L"yaml","yaml"}, {L"yml", "yaml"},
        {L"sh",  "bash"}, {L"ps1", "powershell"}, {L"lua", "lua"},
        {L"html","html"}, {L"css", "css"}, {L"php", "php"}, {L"java","java"},
        {L"vim", "vim"}, {L"cmake","cmake"},
    };
    for (auto& [k, v] : kMap) if (k == ext) return v;
    return narrow(ext);  // fall back to the extension itself
}

CppGrammar parse_cpp_variant(const std::wstring& s) {
    if (s == L"unreal") return CppGrammar::Unreal;
    return CppGrammar::Standard;
}

std::wstring out_path_for(const Options& opts, const wchar_t* suffix_dark, const wchar_t* suffix_light) {
    fs::path input(opts.input_path);
    std::wstring stem = input.filename().wstring();  // includes extension, e.g. sample.cpp
    std::wstring out_name = stem + (opts.dark ? suffix_dark : suffix_light);
    return input.parent_path().empty()
        ? fs::path(out_name).wstring()
        : (input.parent_path() / out_name).wstring();
}

}  // namespace

std::wstring run_colorizer_pipeline(const Options& opts) {
    // --- Factories ---
    ComPtr<ID2D1Factory> d2d_factory;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   d2d_factory.GetAddressOf());
    if (FAILED(hr)) {
        std::fprintf(stderr, "D2D factory failed: 0x%08lx\n", hr);
        return {};
    }

    ComPtr<IDWriteFactory> dwrite_factory;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(dwrite_factory.GetAddressOf()));
    if (FAILED(hr)) {
        std::fprintf(stderr, "DWrite factory failed: 0x%08lx\n", hr);
        return {};
    }

    ComPtr<IWICImagingFactory> wic_factory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(wic_factory.GetAddressOf()));
    if (FAILED(hr)) {
        std::fprintf(stderr, "WIC factory failed: 0x%08lx\n", hr);
        return {};
    }

    // --- Theme ---
    ThemeService theme;
    theme.load(opts.config_path);

    // --- Colorizer (constructed directly — same pattern as test_colorizer_routing) ---
    Colorizer colorizer(L"grammars", L"config/themes");

    // --- Read source ---
    FileService file_service;
    auto content = file_service.read(opts.input_path.c_str());
    if (!content) {
        std::fprintf(stderr, "Failed to read: %ls\n", opts.input_path.c_str());
        return {};
    }

    // --- Resolve language + cpp variant ---
    std::string base_lang = infer_language(opts.input_path, opts.lang);
    if (base_lang.empty()) {
        std::fprintf(stderr, "Could not infer language for %ls (use --lang)\n",
                     opts.input_path.c_str());
        return {};
    }
    CppGrammar variant = parse_cpp_variant(opts.cpp_grammar);
    std::string lang = apply_cpp_variant(base_lang, variant, &colorizer);
    if (!colorizer.supports(lang)) {
        std::fprintf(stderr,
            "No grammar registered for \"%s\" — check grammars/%s/\n",
            lang.c_str(), lang.c_str());
        return {};
    }

    // --- Colorize ---
    ColorizeResult colors = colorizer.colorize(content->raw_utf8, lang, opts.dark);

    // --- Token-dump branch handled in Task 3.4 ---
    if (opts.dump_tokens) {
        std::fprintf(stderr, "--dump-tokens not yet wired (Task 3.4)\n");
        return {};
    }

    // --- Layout ---
    ColorizerDisplayConfig display;  // defaults; --display-config wired in Task 3.5
    auto layout = layout_source(dwrite_factory.Get(),
                                content->normalized_utf16,
                                content->raw_utf8,
                                colors,
                                theme,
                                opts.dark,
                                static_cast<float>(opts.width),
                                display);

    // --- Render to bitmap ---
    int bmp_width = opts.width;
    int bmp_height = opts.full
        ? std::max(1, static_cast<int>(std::ceil(layout.total_height)))
        : opts.height;
    float scroll_y = opts.full ? 0.0f : opts.scroll;

    RenderEngine renderer(d2d_factory.Get(), dwrite_factory.Get(), theme, opts.dark);
    hr = renderer.create_bitmap_resources(wic_factory.Get(), bmp_width, bmp_height);
    if (FAILED(hr)) {
        std::fprintf(stderr, "Bitmap target failed: 0x%08lx\n", hr);
        return {};
    }
    renderer.paint(layout, scroll_y);
    if (renderer.needs_recreate()) {
        std::fprintf(stderr, "Render target lost during paint\n");
        return {};
    }

    // --- Save ---
    std::wstring out = out_path_for(opts, L"_dark.png", L".png");
    hr = renderer.save_to_png(wic_factory.Get(), out.c_str());
    if (FAILED(hr)) {
        std::fprintf(stderr, "Failed to save PNG: 0x%08lx\n", hr);
        return {};
    }
    return out;
}

}  // namespace wlx::tools::screenshot
```

Note: the field name `content->normalized_utf16` may differ — check the actual `FileService::read` return type before relying on it. If the field is named differently (e.g. `wide`, `utf16`, `text_w`), update the call.

- [ ] **Step 2: Verify against `FileService` actual API**

```bash
grep -n "raw_utf8\|utf16\|wide" src/runtime/io/file_service.h
```

Use the actual member name in the call above; do not guess.

- [ ] **Step 3: BUILD_OK + manual smoke**

```bash
cmake --build --preset conan-release
./build/Release/screenshot_tool.exe test_data/grammar_samples/sample.cpp --full --dark
ls test_data/grammar_samples/sample.cpp_dark.png
```

Expected: PNG written, file is non-empty, no error printed. Open the PNG visually — should show colorized C++ code on a dark background. (If it's all-black or unreadable, the layout-engine call has wrong parameters.)

- [ ] **Step 4: Markdown side still works**

```bash
./scripts/visual-test.sh
```

Expected: stage 1 (markdown) all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/tools/screenshot/colorizer_pipeline.cpp
git -c commit.gpgsign=false commit -m "feat(tools/screenshot): implement colorizer paint pipeline

Mirrors markdown_pipeline structure: factories → theme → core → file →
colorize → layout_source → render → PNG. Extension→language inference
covers the 26 grammars present in test_data/grammar_samples; --lang
overrides. cpp variant routing via apply_cpp_variant.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task 3.4: Implement `--dump-tokens` mode

**Files:**
- Modify: `src/tools/screenshot/colorizer_pipeline.cpp`

- [ ] **Step 1: Wire TokenJsonWriter into the pipeline**

Add to the `#include` block:

```cpp
#include "tools/screenshot/token_json_writer.h"
#include <fstream>
```

Add a `compute_config_hash` helper inside the anonymous namespace. It hashes the resolved theme + display config; SHA1 in pure C++ is a small dependency you don't need — use a stable FNV-1a 64-bit hash rendered as 16 hex chars (the spec calls for "SHA1 hex digest", but the goal is *stable change detection*, not cryptographic strength; FNV-1a meets the goal and ships with no dependencies):

```cpp
std::string fnv1a_hex(const std::string& bytes) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : bytes) { h ^= c; h *= 0x100000001b3ull; }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return std::string(buf);
}

std::string compute_config_hash(const std::string& theme_name,
                                bool dark_mode,
                                const ColorizerDisplayConfig& d) {
    // Canonical, stable serialization: theme_name + dark_mode + display fields,
    // each on its own line with key=value. Order is fixed by construction.
    std::ostringstream s;
    s << "theme=" << theme_name << '\n';
    s << "dark="  << (dark_mode ? "1" : "0") << '\n';
    s << "line_numbers="    << (d.line_numbers ? "1" : "0") << '\n';
    s << "word_wrap="       << (d.word_wrap ? "1" : "0") << '\n';
    s << "tab_width="       << d.tab_width << '\n';
    s << "line_height="     << d.line_height_factor << '\n';
    s << "show_whitespace=" << static_cast<int>(d.show_whitespace) << '\n';
    s << "indent_guides="   << (d.show_indent_guides ? "1" : "0") << '\n';
    s << "highlight_trail=" << (d.highlight_trailing ? "1" : "0") << '\n';
    s << "cpp_grammar="     << static_cast<int>(d.cpp_grammar) << '\n';
    return fnv1a_hex(s.str());
}
```

(Note for the spec: the spec calls for "SHA1 hex"; we're using FNV-1a hex of the same canonical input. Same property — stable hash that changes when input changes — without pulling in a crypto dependency. If a SHA1 dependency lands later, swap `fnv1a_hex` for `sha1_hex` and regenerate goldens once; the schema doesn't care which algorithm produced the digest. Document this deviation in the commit message.)

Replace the placeholder in `run_colorizer_pipeline`:

```cpp
    if (opts.dump_tokens) {
        ColorizerDisplayConfig display;  // same defaults the paint path uses
        std::string theme_name_str = opts.dark ? "default_dark" : "default_light";
        TokenJsonOptions json_opts;
        json_opts.source_name = fs::path(opts.input_path).filename().string();
        json_opts.language    = lang;
        json_opts.theme_name  = theme_name_str;
        json_opts.config_hash = compute_config_hash(theme_name_str, opts.dark, display);

        std::string json = TokenJsonWriter::write(colors, content->raw_utf8, json_opts);

        std::wstring suffix = opts.dark ? L"_tokens.dark.json" : L"_tokens.light.json";
        std::wstring out = out_path_for(opts, suffix.c_str(), suffix.c_str());
        std::ofstream f(out, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "Failed to open %ls for writing\n", out.c_str());
            return {};
        }
        f.write(json.data(), static_cast<std::streamsize>(json.size()));
        return out;
    }
```

(`out_path_for` was added in 3.3 with two suffixes; for the JSON case we pass the same suffix for both dark and light because the suffix string itself encodes the theme. The `dark`-aware branch in `out_path_for` becomes a no-op for JSON output, which is fine.)

- [ ] **Step 2: BUILD_OK + manual smoke**

```bash
cmake --build --preset conan-release
./build/Release/screenshot_tool.exe test_data/grammar_samples/sample.cpp --colorizer --dump-tokens --dark
cat test_data/grammar_samples/sample.cpp_tokens.dark.json | head -10
```

Expected: file is well-formed JSON, header keys appear in order (`source`, `language`, `theme`, `config_hash`, `token_count`, `tokens`), `token_count` is well over 100. Run a second time:

```bash
./build/Release/screenshot_tool.exe test_data/grammar_samples/sample.cpp --colorizer --dump-tokens --dark
sha256sum test_data/grammar_samples/sample.cpp_tokens.dark.json
./build/Release/screenshot_tool.exe test_data/grammar_samples/sample.cpp --colorizer --dump-tokens --dark
sha256sum test_data/grammar_samples/sample.cpp_tokens.dark.json
```

Expected: identical sha256 across the two runs (determinism). If they differ, fix the writer before proceeding.

- [ ] **Step 3: Commit**

```bash
git add src/tools/screenshot/colorizer_pipeline.cpp
git -c commit.gpgsign=false commit -m "feat(tools/screenshot): implement --dump-tokens via TokenJsonWriter

Writes <stem>.<ext>_tokens.<theme>.json next to the source. config_hash
is FNV-1a hex over a canonical (theme, mode, display-config) line-format
— same change-detection property as SHA1 with no extra dependency. The
schema doesn't pin the digest algorithm; swapping later is a one-time
golden regen.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task 3.5: Wire `--display-config` TOML overrides

**Files:**
- Modify: `src/tools/screenshot/colorizer_pipeline.cpp`

- [ ] **Step 1: Add a TOML loader for `ColorizerDisplayConfig`**

Inside the anonymous namespace in `colorizer_pipeline.cpp`, add:

```cpp
#include <toml++/toml.h>

ColorizerDisplayConfig load_display_config(const std::wstring& path) {
    ColorizerDisplayConfig d;  // defaults
    if (path.empty()) return d;
    try {
        auto tbl = toml::parse_file(narrow(path));
        if (auto v = tbl["line_numbers"].value<bool>())  d.line_numbers = *v;
        if (auto v = tbl["word_wrap"].value<bool>())     d.word_wrap = *v;
        if (auto v = tbl["tab_width"].value<int64_t>())  d.tab_width = static_cast<int>(*v);
        if (auto v = tbl["line_height"].value<double>()) d.line_height_factor = static_cast<float>(*v);
        if (auto v = tbl["show_indent_guides"].value<bool>())  d.show_indent_guides = *v;
        if (auto v = tbl["highlight_trailing"].value<bool>())  d.highlight_trailing = *v;
        if (auto v = tbl["show_whitespace"].value<std::string>()) {
            using wlx::plugin_colorizer::layout::ShowWhitespace;
            if      (*v == "all")      d.show_whitespace = ShowWhitespace::All;
            else if (*v == "boundary") d.show_whitespace = ShowWhitespace::Boundary;
            else                       d.show_whitespace = ShowWhitespace::None;
        }
    } catch (const toml::parse_error& e) {
        std::fprintf(stderr, "Failed to parse --display-config %ls: %s\n",
                     path.c_str(), e.description().data());
    }
    return d;
}
```

- [ ] **Step 2: Use it in both pipeline branches**

Replace the two `ColorizerDisplayConfig display;` declarations with:

```cpp
    ColorizerDisplayConfig display = load_display_config(opts.display_config);
    display.cpp_grammar = variant;
```

(The `cpp_grammar` field was already on the config struct; setting it post-load keeps `--cpp-grammar` authoritative over any TOML value.)

- [ ] **Step 3: BUILD_OK + smoke**

```bash
cmake --build --preset conan-release

cat > /tmp/disp.toml <<'EOF'
line_numbers = false
word_wrap = true
tab_width = 2
EOF

./build/Release/screenshot_tool.exe test_data/grammar_samples/sample.cpp --full --dark --display-config /tmp/disp.toml
```

Expected: PNG renders without crash. Visually it should differ from the no-config run (no gutter, narrower indent).

- [ ] **Step 4: Verify config_hash actually changes**

```bash
./build/Release/screenshot_tool.exe test_data/grammar_samples/sample.cpp --colorizer --dump-tokens --dark
grep config_hash test_data/grammar_samples/sample.cpp_tokens.dark.json

./build/Release/screenshot_tool.exe test_data/grammar_samples/sample.cpp --colorizer --dump-tokens --dark --display-config /tmp/disp.toml
grep config_hash test_data/grammar_samples/sample.cpp_tokens.dark.json
```

Expected: the two `config_hash` values differ. If identical, the override isn't being applied — debug.

- [ ] **Step 5: Commit**

```bash
git add src/tools/screenshot/colorizer_pipeline.cpp
git -c commit.gpgsign=false commit -m "feat(tools/screenshot): --display-config TOML overrides for ColorizerDisplayConfig

Loads line_numbers/word_wrap/tab_width/line_height/show_whitespace/
indent_guides/highlight_trailing from a TOML file. cpp_grammar stays
authoritative from --cpp-grammar (post-load). Override flows into both
paint and dump-tokens paths and is included in config_hash.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Stage 4 — `compare.py` `--subdir` flag

Trivial additive change so stage 3 of `visual-test.sh` can point `compare.py` at `colorizer_smokes/` without breaking the existing default.

### Task 4.1: Add `--subdir` to compare.py

**Files:**
- Modify: `test_data/compare.py`

- [ ] **Step 1: Read `compare.py`**

Confirm the current shape: `CASES_DIR = Path(__file__).parent / "cases"` and `filter_name = sys.argv[1] if len(sys.argv) > 1 else None`. The new flag must not break the existing positional filter.

- [ ] **Step 2: Edit**

Replace the `import sys` / `from pathlib import Path` / constant / `main()` shape with:

```python
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

DEFAULT_SUBDIR = "cases"
TEST_DATA = Path(__file__).parent

# (compare_images function unchanged)

def main():
    p = argparse.ArgumentParser(description="Compare screenshot_tool output vs golden PNGs.")
    p.add_argument("filter", nargs="?", default=None,
                   help="Substring filter on case name (optional)")
    p.add_argument("--subdir", default=DEFAULT_SUBDIR,
                   help=f"Subdirectory under test_data/ to walk (default: {DEFAULT_SUBDIR})")
    args = p.parse_args()

    cases_dir = TEST_DATA / args.subdir
    if not cases_dir.is_dir():
        print(f"  ERROR  subdir does not exist: {cases_dir}")
        sys.exit(2)

    cases = sorted(list(cases_dir.glob("*.md")) + list(cases_dir.glob("*.cpp"))
                   + list(cases_dir.glob("*.py")) + list(cases_dir.glob("*.txt")))
    # The file extensions above cover existing markdown cases plus the
    # smokes added in Stage 6. Subdirs that contain none of these extensions
    # produce an empty case list and exit 2 (consistent with prior behavior).
    results = []

    for src_file in cases:
        name = src_file.stem if src_file.suffix == ".md" else src_file.name
        if args.filter and args.filter not in name:
            continue

        ours_path = cases_dir / f"{name}.png"
        golden_path = cases_dir / f"{name}_golden.png"
        chrome_path = cases_dir / f"{name}_chrome.png"

        if not ours_path.exists():
            print(f"  SKIP  {name} (no tool screenshot)")
            continue

        if golden_path.exists():
            ref_path = golden_path
            threshold = 99.5
        elif chrome_path.exists():
            ref_path = chrome_path
            threshold = 95.0
        else:
            print(f"  SKIP  {name} (no reference image)")
            continue

        similarity, diff_img = compare_images(ours_path, ref_path)
        diff_path = cases_dir / f"{name}_diff.png"
        diff_img.save(diff_path)

        status = "PASS" if similarity >= threshold else "WARN" if similarity >= threshold - 15 else "FAIL"
        results.append((name, similarity, status))
        print(f"  {status}  {similarity:5.1f}%  {name}")

    if not results:
        print("\n  No cases compared — nothing to validate")
        sys.exit(2)

    avg = sum(s for _, s, _ in results) / len(results)
    passes = sum(1 for _, _, st in results if st == "PASS")
    fails = sum(1 for _, _, st in results if st == "FAIL")
    print(f"\n  {passes}/{len(results)} pass, avg similarity: {avg:.1f}%")

    if fails > 0:
        sys.exit(1)
```

(Keep the existing `compare_images` function above `main()` exactly as it is.)

Note: cases for the markdown stage are still `*.md` (each producing `sample.png` under the same stem), so `name = src_file.stem` for `.md` keeps the existing markdown filename convention. For non-`.md` cases the colorizer mode writes `<full_filename>.png` (e.g. `sample.cpp.png`), so `name = src_file.name` matches.

- [ ] **Step 3: Smoke-test the existing path**

```bash
uv run --with Pillow python test_data/compare.py
```

Expected: same output as before — 28 markdown cases, all PASS. Then:

```bash
uv run --with Pillow python test_data/compare.py 01_headings
```

Expected: just the `01_headings_atx` case is compared. Both invocations match pre-change behavior.

- [ ] **Step 4: Smoke-test `--subdir` against a non-existent dir**

```bash
uv run --with Pillow python test_data/compare.py --subdir colorizer_smokes
```

Expected: exit code 2, error message "subdir does not exist". (The directory will be created in Stage 6.)

- [ ] **Step 5: Commit**

```bash
git add test_data/compare.py
git -c commit.gpgsign=false commit -m "feat(test_data): add --subdir flag to compare.py

Additive — existing positional filter and default \"cases\" subdir
unchanged. Enables compare.py to walk test_data/colorizer_smokes/ in
stage 3 of visual-test.sh.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Stage 5 — `compare_tokens.py` (Python TDD)

### Task 5.1: Write the failing comparator self-tests

**Files:**
- Create: `test_data/test_compare_tokens.py`

- [ ] **Step 1: Write the tests**

```python
"""Self-tests for compare_tokens.py — run via:
    uv run --with pytest pytest test_data/test_compare_tokens.py -v
"""
import json
import subprocess
import sys
from pathlib import Path

import pytest

THIS_DIR = Path(__file__).parent
COMPARE = THIS_DIR / "compare_tokens.py"


def _write_pair(tmp_path, name, ours, golden, theme="dark"):
    """Write ours and golden JSON files under tmp_path/grammar_samples/."""
    samples_dir = tmp_path / "grammar_samples"
    samples_dir.mkdir(parents=True, exist_ok=True)
    src = samples_dir / name
    src.write_text("// placeholder source for tests\n", encoding="utf-8")
    (samples_dir / f"{name}_tokens.{theme}.json").write_text(json.dumps(ours), encoding="utf-8")
    (samples_dir / f"{name}_tokens.{theme}.golden.json").write_text(json.dumps(golden), encoding="utf-8")
    return samples_dir


def _run(samples_dir):
    """Invoke compare_tokens.py against samples_dir; return (rc, stdout)."""
    proc = subprocess.run(
        [sys.executable, str(COMPARE), "--samples-dir", str(samples_dir)],
        capture_output=True, text=True, check=False,
    )
    return proc.returncode, proc.stdout + proc.stderr


def _doc(tokens, *, source="sample.cpp", language="cpp", theme="default_dark", config_hash="h"):
    return {
        "source": source, "language": language, "theme": theme,
        "config_hash": config_hash, "token_count": len(tokens), "tokens": tokens
    }


def test_identical_passes(tmp_path):
    doc = _doc([{"line": 1, "col": 1, "len": 5, "color": "#FF0000", "mods": []}])
    samples = _write_pair(tmp_path, "sample.cpp", doc, doc)
    rc, out = _run(samples)
    assert rc == 0
    assert "PASS" in out


def test_color_diff_fails_at_correct_line_col(tmp_path):
    a = _doc([{"line": 7, "col": 4, "len": 5, "color": "#FF0000", "mods": []}])
    b = _doc([{"line": 7, "col": 4, "len": 5, "color": "#00FF00", "mods": []}])
    samples = _write_pair(tmp_path, "sample.cpp", a, b)
    rc, out = _run(samples)
    assert rc == 1
    assert "7:4" in out or "line 7" in out
    assert "#FF0000" in out and "#00FF00" in out


def test_token_count_mismatch_fails_with_delta(tmp_path):
    a = _doc([
        {"line": 1, "col": 1, "len": 1, "color": "#111111", "mods": []},
        {"line": 2, "col": 1, "len": 1, "color": "#222222", "mods": []},
        {"line": 3, "col": 1, "len": 1, "color": "#333333", "mods": []},
    ])
    b = _doc([
        {"line": 1, "col": 1, "len": 1, "color": "#111111", "mods": []},
        {"line": 3, "col": 1, "len": 1, "color": "#333333", "mods": []},
    ])
    samples = _write_pair(tmp_path, "sample.cpp", a, b)
    rc, out = _run(samples)
    assert rc == 1
    assert "token_count" in out.lower()


def test_config_hash_mismatch_exits_2_no_per_token_diff(tmp_path):
    a = _doc([{"line": 1, "col": 1, "len": 5, "color": "#FF0000", "mods": []}],
             config_hash="aaaa")
    b = _doc([{"line": 1, "col": 1, "len": 5, "color": "#00FF00", "mods": []}],
             config_hash="bbbb")
    samples = _write_pair(tmp_path, "sample.cpp", a, b)
    rc, out = _run(samples)
    assert rc == 2
    assert "config_hash" in out.lower()
    # Per-token diff must NOT be emitted (color difference would normally fire).
    assert "#FF0000" not in out
    assert "#00FF00" not in out


def test_empty_tokens_both_sides_passes(tmp_path):
    doc = _doc([])
    samples = _write_pair(tmp_path, "sample.cpp", doc, doc)
    rc, out = _run(samples)
    assert rc == 0
    assert "PASS" in out


def test_missing_golden_warns_does_not_fail(tmp_path):
    """A sample with a just-produced JSON but no golden warns and exits 0."""
    samples_dir = tmp_path / "grammar_samples"
    samples_dir.mkdir(parents=True)
    (samples_dir / "sample.cpp").write_text("// src\n", encoding="utf-8")
    (samples_dir / "sample.cpp_tokens.dark.json").write_text(
        json.dumps(_doc([{"line": 1, "col": 1, "len": 5, "color": "#FF0000", "mods": []}])),
        encoding="utf-8",
    )
    rc, out = _run(samples_dir)
    # No goldens at all → exit 2 ("no cases to validate"), matches compare.py behavior
    assert rc == 2
    # When *some* goldens exist, the missing-golden case should WARN. Add a paired
    # sample to test that path:
    (samples_dir / "sample.py").write_text("# src\n", encoding="utf-8")
    paired_doc = _doc([{"line": 1, "col": 1, "len": 1, "color": "#000000", "mods": []}])
    (samples_dir / "sample.py_tokens.dark.json").write_text(json.dumps(paired_doc))
    (samples_dir / "sample.py_tokens.dark.golden.json").write_text(json.dumps(paired_doc))
    rc, out = _run(samples_dir)
    assert rc == 0
    assert "WARN" in out or "SKIP" in out  # sample.cpp has no golden
    assert "PASS" in out                   # sample.py paired
```

- [ ] **Step 2: Run tests, confirm they fail (compare_tokens.py doesn't exist yet)**

```bash
uv run --with pytest pytest test_data/test_compare_tokens.py -v
```

Expected: every test fails with non-zero exit from the subprocess (script not found / `FileNotFoundError`). This is the "red" state.

- [ ] **Step 3: No commit yet** — failing tests don't get committed standalone.

### Task 5.2: Implement `compare_tokens.py`

**Files:**
- Create: `test_data/compare_tokens.py`

- [ ] **Step 1: Implement**

```python
"""Compare screenshot_tool --dump-tokens output vs golden token JSON.

Usage: python compare_tokens.py [--samples-dir <path>] [filter]

Walks <samples-dir> for *_tokens.<theme>.golden.json. For each, loads the
sibling *_tokens.<theme>.json (just produced) and diffs.

Exit codes:
  0 — all PASS
  1 — at least one FAIL (token diff or token_count mismatch)
  2 — at least one config_hash mismatch, OR no goldens found at all
"""
import argparse
import json
import re
import sys
from pathlib import Path

DEFAULT_SAMPLES = Path(__file__).parent / "grammar_samples"
GOLDEN_RE = re.compile(r"^(?P<stem>.+)_tokens\.(?P<theme>dark|light)\.golden\.json$")
CONTEXT = 10  # tokens of context above/below divergence in the diff file


def load_json(path: Path):
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def first_divergent_index(ours_tokens, golden_tokens):
    n = min(len(ours_tokens), len(golden_tokens))
    for i in range(n):
        if ours_tokens[i] != golden_tokens[i]:
            return i
    if len(ours_tokens) != len(golden_tokens):
        return n
    return None


def write_diff_file(path: Path, idx, ours, golden):
    lo = max(0, idx - CONTEXT)
    hi_o = min(len(ours["tokens"]), idx + CONTEXT + 1)
    hi_g = min(len(golden["tokens"]), idx + CONTEXT + 1)
    lines = [f"# Diff at token index {idx}", "",
             f"# Ours ({ours['source']} / {ours['theme']})", ""]
    for i in range(lo, hi_o):
        marker = ">>>" if i == idx else "   "
        lines.append(f"{marker} [{i:5d}] {json.dumps(ours['tokens'][i])}")
    lines += ["", f"# Golden ({golden['source']} / {golden['theme']})", ""]
    for i in range(lo, hi_g):
        marker = ">>>" if i == idx else "   "
        lines.append(f"{marker} [{i:5d}] {json.dumps(golden['tokens'][i])}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def diff_one(stem, theme, samples_dir):
    """Return (status, message). status ∈ {'PASS', 'WARN', 'FAIL', 'CONFIG'}."""
    ours_path   = samples_dir / f"{stem}_tokens.{theme}.json"
    golden_path = samples_dir / f"{stem}_tokens.{theme}.golden.json"

    if not ours_path.exists():
        return ("WARN", f"no just-produced JSON at {ours_path.name} "
                        f"(did you run screenshot_tool first?)")

    ours   = load_json(ours_path)
    golden = load_json(golden_path)

    if ours.get("config_hash") != golden.get("config_hash"):
        return ("CONFIG", f"config_hash drift in {ours_path.name} "
                          f"(got {ours.get('config_hash')}, golden {golden.get('config_hash')}) "
                          f"— regenerate goldens (bun run update-goldens)")

    if ours.get("token_count") != golden.get("token_count"):
        idx = first_divergent_index(ours.get("tokens", []), golden.get("tokens", []))
        msg = (f"token_count mismatch (got {ours.get('token_count')}, "
               f"golden {golden.get('token_count')})")
        if idx is not None:
            diff_path = samples_dir / f"{stem}_tokens.{theme}_diff.txt"
            write_diff_file(diff_path, idx, ours, golden)
            msg += f"; first divergence at index {idx}; see {diff_path.name}"
        return ("FAIL", msg)

    idx = first_divergent_index(ours["tokens"], golden["tokens"])
    if idx is None:
        return ("PASS", f"{ours['token_count']} tokens")

    diff_path = samples_dir / f"{stem}_tokens.{theme}_diff.txt"
    write_diff_file(diff_path, idx, ours, golden)
    o = ours["tokens"][idx]
    g = golden["tokens"][idx]
    return ("FAIL", f"first divergence at index {idx} "
                    f"(line {o['line']} col {o['col']}): "
                    f"expected color={g['color']} mods={g['mods']}, "
                    f"got color={o['color']} mods={o['mods']}; "
                    f"see {diff_path.name}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("filter", nargs="?", default=None,
                   help="Substring filter on sample stem (optional)")
    p.add_argument("--samples-dir", default=str(DEFAULT_SAMPLES),
                   help="Directory to walk for *_tokens.<theme>.golden.json")
    args = p.parse_args()

    samples_dir = Path(args.samples_dir)
    if not samples_dir.is_dir():
        print(f"  ERROR  samples-dir does not exist: {samples_dir}")
        sys.exit(2)

    pairs = []  # list of (stem, theme)
    for f in sorted(samples_dir.iterdir()):
        m = GOLDEN_RE.match(f.name)
        if not m:
            continue
        stem = m.group("stem")
        theme = m.group("theme")
        if args.filter and args.filter not in stem:
            continue
        pairs.append((stem, theme))

    if not pairs:
        print("  No goldens found — nothing to validate")
        sys.exit(2)

    fail_count = 0
    config_mismatch = 0
    for stem, theme in pairs:
        status, msg = diff_one(stem, theme, samples_dir)
        print(f"  {status:6s} {stem} ({theme})  {msg}")
        if status == "FAIL":
            fail_count += 1
        elif status == "CONFIG":
            config_mismatch += 1

    if config_mismatch:
        sys.exit(2)
    if fail_count:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run self-tests, confirm PASS**

```bash
uv run --with pytest pytest test_data/test_compare_tokens.py -v
```

Expected: all 6 tests pass.

- [ ] **Step 3: Commit**

```bash
git add test_data/compare_tokens.py test_data/test_compare_tokens.py
git -c commit.gpgsign=false commit -m "feat(test_data): compare_tokens.py + 6 self-tests

First-divergence diff with ~10-token context written to
*_tokens.<theme>_diff.txt. Exit codes: 0 PASS, 1 token regression,
2 config_hash drift / no goldens. Self-tests run via
\`uv run --with pytest pytest test_data/test_compare_tokens.py\`.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Stage 6 — Pixel smoke files

Five minimal source files designed to exercise specific render-engine axes. Each file is small enough that a one-line change to the file produces a focused, reviewable PNG diff.

### Task 6.1: Create the smokes directory and 5 source files

**Files:**
- Create: `test_data/colorizer_smokes/smoke_modifiers.cpp`
- Create: `test_data/colorizer_smokes/smoke_whitespace_indent_guides.py`
- Create: `test_data/colorizer_smokes/smoke_line_numbers_wrap.cpp`
- Create: `test_data/colorizer_smokes/smoke_dark.cpp`
- Create: `test_data/colorizer_smokes/smoke_unreal_cpp_variant.cpp`
- Create: `test_data/colorizer_smokes/smoke_unreal_cpp_variant.flags`

- [ ] **Step 1: Create the directory and write each file**

```bash
mkdir -p test_data/colorizer_smokes
```

`smoke_modifiers.cpp` — exercises bold (keyword) + italic (comment, set by `make_default` themes) on visible tokens:

```cpp
// This comment must render in italic if MOD_ITALIC is wired correctly.
#include <string>           // keyword.directive must render bold

int main() {
    auto greeting = "hello"; // strings, comments, identifiers all visible
    return 0;
}
```

`smoke_whitespace_indent_guides.py` — exercises tabs, trailing spaces, deep indent:

```python
def example():
	x = 1                                  # tab-indented; trailing spaces above
	if x:
		if x > 0:
			if x > 1:
				return "deep nesting"   # 4 levels of indent for guide rendering
	return None
```

(Use a single literal `\t` per indent level; don't expand tabs to spaces in the file.)

`smoke_line_numbers_wrap.cpp` — long line forcing wrap, gutter visible:

```cpp
// Line numbers must align across wrapped lines.
int main() {
    auto very_long_string = "lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod tempor incididunt ut labore et dolore magna aliqua";
    return 0;
}
```

`smoke_dark.cpp` — minimal cpp; baseline that the renderer paints on dark background:

```cpp
int answer() {
    return 42;
}
```

`smoke_unreal_cpp_variant.cpp` — exercises the unreal-cpp grammar:

```cpp
#include "GameFramework/Actor.h"

UCLASS()
class MYGAME_API AMyActor : public AActor {
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable)
    void DoSomething();
};
```

`smoke_unreal_cpp_variant.flags` (single line, no trailing newline issues — the existing `.flags` reader strips CR):

```
--colorizer --cpp-grammar unreal --dark
```

- [ ] **Step 2: Manual render of each smoke**

```bash
for f in test_data/colorizer_smokes/*.cpp test_data/colorizer_smokes/*.py; do
    case "$f" in
        *unreal*) ./build/Release/screenshot_tool.exe "$f" --full --colorizer --cpp-grammar unreal --dark ;;
        *)        ./build/Release/screenshot_tool.exe "$f" --full --dark ;;
    esac
done
ls test_data/colorizer_smokes/*.png
```

Expected: 5 PNGs produced. Open each visually:
- `smoke_modifiers_dark.png` → visible italic comment + bold `#include`.
- `smoke_whitespace_indent_guides_dark.png` → tab markers and indent guides at 4 levels.
- `smoke_line_numbers_wrap_dark.png` → line numbers in gutter, long line wrapped.
- `smoke_dark_dark.png` → simple cpp on dark background.
- `smoke_unreal_cpp_variant_dark.png` → `UCLASS` / `UFUNCTION` highlighted differently from a standard cpp run (verify in Step 3).

- [ ] **Step 3: Verify the unreal smoke is non-tautological**

Render the same file through the *standard* cpp grammar:

```bash
./build/Release/screenshot_tool.exe test_data/colorizer_smokes/smoke_unreal_cpp_variant.cpp --full --colorizer --cpp-grammar standard --dark -o /tmp/std.png 2>/dev/null || \
  cp test_data/colorizer_smokes/smoke_unreal_cpp_variant_dark.png /tmp/unreal_render.png
./build/Release/screenshot_tool.exe test_data/colorizer_smokes/smoke_unreal_cpp_variant.cpp --full --colorizer --cpp-grammar standard --dark
mv test_data/colorizer_smokes/smoke_unreal_cpp_variant_dark.png /tmp/standard_render.png

./build/Release/screenshot_tool.exe test_data/colorizer_smokes/smoke_unreal_cpp_variant.cpp --full --colorizer --cpp-grammar unreal --dark
mv test_data/colorizer_smokes/smoke_unreal_cpp_variant_dark.png /tmp/unreal_render.png
```

Then compare visually or with a quick checksum:

```bash
sha256sum /tmp/standard_render.png /tmp/unreal_render.png
```

Expected: **different** sha256s. If they're identical, the unreal grammar isn't being used — investigate before committing.

After verification, regenerate the unreal-variant render in place (it was overwritten):

```bash
./build/Release/screenshot_tool.exe test_data/colorizer_smokes/smoke_unreal_cpp_variant.cpp --full --colorizer --cpp-grammar unreal --dark
```

- [ ] **Step 4: Commit smoke source files only (not yet the goldens)**

```bash
git add test_data/colorizer_smokes/smoke_modifiers.cpp \
        test_data/colorizer_smokes/smoke_whitespace_indent_guides.py \
        test_data/colorizer_smokes/smoke_line_numbers_wrap.cpp \
        test_data/colorizer_smokes/smoke_dark.cpp \
        test_data/colorizer_smokes/smoke_unreal_cpp_variant.cpp \
        test_data/colorizer_smokes/smoke_unreal_cpp_variant.flags
git -c commit.gpgsign=false commit -m "test_data: add 5 colorizer pixel smoke source files

Targets render-engine axes (modifiers, whitespace+indent guides,
line numbers+wrap, dark baseline, unreal cpp variant). Goldens land
in stage 7 once update-goldens.ts is wired.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Stage 7 — `update-goldens.ts` colorizer branch + first golden generation

### Task 7.1: Extend `update-goldens.ts`

**Files:**
- Modify: `scripts/update-goldens.ts`

- [ ] **Step 1: Read the current script**

It currently walks `test_data/cases/*.md` and either runs Playwright (for the Chrome reference) or `screenshot_tool` (for `.flags` cases). The new branches add walks over `test_data/grammar_samples/*` and `test_data/colorizer_smokes/*`.

- [ ] **Step 2: Add the colorizer walks**

Edit `scripts/update-goldens.ts`. Below the existing `CASES_DIR` constant, add:

```typescript
const GRAMMAR_SAMPLES_DIR = join(TEST_DATA, "grammar_samples");
const COLORIZER_SMOKES_DIR = join(TEST_DATA, "colorizer_smokes");
const TOOL = join(ROOT, "build", "Release", "screenshot_tool.exe");
const LIGHT_SPOT_CHECK = "sample.py";  // the one light-theme spot-check sample
```

Inside `main()`, after the existing per-case loop and before the `await browser.close()`, add:

```typescript
  // --- Colorizer token snapshots ---
  if (existsSync(GRAMMAR_SAMPLES_DIR)) {
    console.log(`\n=== Colorizer token snapshots ===`);
    const samples = (await readdir(GRAMMAR_SAMPLES_DIR))
      .filter((f) => f.startsWith("sample.")
                  && !f.endsWith(".json")
                  && !f.endsWith(".png"))
      .filter((f) => !filterName || f.includes(filterName))
      .sort();

    for (const sample of samples) {
      // Always regenerate the dark snapshot.
      runDumpTokens(sample, /*dark=*/true);
      copyGolden(sample, "dark");

      // Light spot-check: only the configured sample
      if (sample === LIGHT_SPOT_CHECK) {
        runDumpTokens(sample, /*dark=*/false);
        copyGolden(sample, "light");
      }
    }
  }

  // --- Colorizer pixel smokes ---
  if (existsSync(COLORIZER_SMOKES_DIR)) {
    console.log(`\n=== Colorizer pixel smokes ===`);
    const smokes = (await readdir(COLORIZER_SMOKES_DIR))
      .filter((f) => !f.endsWith(".png") && !f.endsWith(".flags"))
      .filter((f) => !filterName || f.includes(filterName))
      .sort();

    for (const smoke of smokes) {
      const flagsPath = join(COLORIZER_SMOKES_DIR, `${smoke}.flags`);
      const flags = existsSync(flagsPath)
        ? readFileSync(flagsPath, "utf8").trim().split(/\s+/).filter(Boolean)
        : ["--full", "--dark"];
      const src = join(COLORIZER_SMOKES_DIR, smoke);
      const result = spawnSync(TOOL, [src, ...flags], { stdio: "inherit" });
      if (result.status !== 0) {
        console.error(`  FAIL  ${smoke}`);
        continue;
      }
      // Tool wrote either <smoke>_dark.png (paint mode) — copy to <smoke>_golden.png
      const stem = smoke;
      const ours = join(COLORIZER_SMOKES_DIR, `${stem}_dark.png`);
      const golden = join(COLORIZER_SMOKES_DIR, `${stem}_golden.png`);
      if (existsSync(ours)) {
        copyFileSync(ours, golden);
        console.log(`  OK    ${stem} -> ${stem}_golden.png`);
      } else {
        console.error(`  FAIL  ${stem} (no PNG produced)`);
      }
    }
  }
```

Add the helper functions above `main()`:

```typescript
function runDumpTokens(sample: string, dark: boolean) {
  const args = [
    join(GRAMMAR_SAMPLES_DIR, sample),
    "--colorizer", "--dump-tokens",
    ...(dark ? ["--dark"] : []),
  ];
  const result = spawnSync(TOOL, args, { stdio: "inherit" });
  if (result.status !== 0) {
    throw new Error(`screenshot_tool --dump-tokens failed for ${sample}`);
  }
}

function copyGolden(sample: string, theme: "dark" | "light") {
  const ours   = join(GRAMMAR_SAMPLES_DIR, `${sample}_tokens.${theme}.json`);
  const golden = join(GRAMMAR_SAMPLES_DIR, `${sample}_tokens.${theme}.golden.json`);
  copyFileSync(ours, golden);
  console.log(`  OK    ${sample} (${theme}) -> ${path.basename(golden)}`);
}
```

(Add `import { basename } from "path";` to the imports at the top — actually the file already imports `basename`. Confirm by reading the current import block.)

Update the closing log message:

```typescript
  console.log(`\nDone. Review the updated goldens, then commit:`);
  console.log(`  git add test_data/cases/*_chrome.png \\`);
  console.log(`          test_data/grammar_samples/*_tokens.*.golden.json \\`);
  console.log(`          test_data/colorizer_smokes/*_golden.png`);
```

- [ ] **Step 3: Test-run with a single-file filter**

```bash
bun run update-goldens -- sample.cpp
```

Expected: regenerates the markdown step (skipped — `sample.cpp` doesn't match any markdown case), then walks `grammar_samples/`, regenerates `sample.cpp_tokens.dark.golden.json`. Verify:

```bash
ls test_data/grammar_samples/sample.cpp_tokens.dark.golden.json
head -5 test_data/grammar_samples/sample.cpp_tokens.dark.golden.json
```

- [ ] **Step 4: Generate all goldens**

```bash
bun run update-goldens
```

Expected: 26 token JSON goldens (dark) + 1 light spot-check + 5 PNG smoke goldens written. Verify counts:

```bash
ls test_data/grammar_samples/*_tokens.dark.golden.json | wc -l    # → 26
ls test_data/grammar_samples/*_tokens.light.golden.json | wc -l   # → 1
ls test_data/colorizer_smokes/*_golden.png | wc -l                # → 5
```

If any count is off, find the missing sample/smoke and figure out why it didn't generate.

- [ ] **Step 5: Commit script + goldens together**

```bash
git add scripts/update-goldens.ts \
        test_data/grammar_samples/*_tokens.dark.golden.json \
        test_data/grammar_samples/*_tokens.light.golden.json \
        test_data/colorizer_smokes/*_golden.png
git -c commit.gpgsign=false commit -m "feat(scripts): update-goldens generates colorizer JSON + PNG goldens

Walks test_data/grammar_samples/ for token JSON (dark theme + sample.py
light spot-check) and test_data/colorizer_smokes/ for pixel PNGs. Both
walks respect the existing single-file filter (e.g. \`bun run
update-goldens -- sample.cpp\`).

Includes the first complete generation: 27 token goldens + 5 pixel
smoke goldens.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Stage 8 — `visual-test.sh` extensions

### Task 8.1: Add stages 2 and 3, accumulate-then-exit

**Files:**
- Modify: `scripts/visual-test.sh`

- [ ] **Step 1: Read current script**

It uses `set -euo pipefail`; the `set -e` short-circuits the script on the first non-zero exit. The new structure preserves fail-fast *within* a stage but accumulates across stages.

- [ ] **Step 2: Restructure**

Replace the contents of `scripts/visual-test.sh` with:

```bash
#!/usr/bin/env bash
set -uo pipefail   # NB: no -e; we accumulate stage failures

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CASES_DIR="$ROOT_DIR/test_data/cases"
SAMPLES_DIR="$ROOT_DIR/test_data/grammar_samples"
SMOKES_DIR="$ROOT_DIR/test_data/colorizer_smokes"
SCREENSHOT_TOOL="$ROOT_DIR/build/Release/screenshot_tool.exe"

if [[ ! -f "$SCREENSHOT_TOOL" ]]; then
    echo "ERROR: screenshot_tool.exe not found."
    echo "Build first: cmake --build --preset conan-release"
    exit 1
fi
if ! command -v uv &> /dev/null; then
    echo "ERROR: uv not found. Install: https://docs.astral.sh/uv/"
    exit 1
fi

# Track stage statuses so we surface every failure in one run.
stage1_rc=0
stage2_rc=0
stage3_rc=0

# ===== Stage 1: Markdown cases =====
echo "=== Stage 1: Markdown cases ==="
gen_fail=0
gen_ok=0
for md_file in "$CASES_DIR"/*.md; do
    name="$(basename "$md_file" .md)"
    flags_file="$CASES_DIR/${name}.flags"
    extra_args=()
    if [[ -f "$flags_file" ]]; then
        extra_args=($(tr -d '\r' < "$flags_file"))
    fi
    if "$SCREENSHOT_TOOL" "$md_file" --full "${extra_args[@]}" > /dev/null 2>&1; then
        echo "  OK   $name"
        gen_ok=$((gen_ok + 1))
    else
        echo "  ERR  $name"
        gen_fail=$((gen_fail + 1))
    fi
done
echo "  Generated: $gen_ok OK, $gen_fail errors"
if [[ $gen_fail -gt 0 ]]; then
    echo "  Stage 1 generation: FAIL"
    stage1_rc=1
else
    echo ""
    uv run --with Pillow python "$ROOT_DIR/test_data/compare.py"
    stage1_rc=$?
fi

# ===== Stage 2: Colorizer token snapshots =====
echo ""
echo "=== Stage 2: Colorizer token snapshots ==="
if [[ ! -d "$SAMPLES_DIR" ]]; then
    echo "  SKIP  $SAMPLES_DIR not present"
else
    s2_ok=0
    s2_fail=0
    for sample in "$SAMPLES_DIR"/sample.*; do
        # Skip the just-produced and golden JSON, the PNGs, and the diff files.
        case "$sample" in
            *.json|*.png|*_diff.txt) continue ;;
        esac
        name="$(basename "$sample")"
        if "$SCREENSHOT_TOOL" "$sample" --colorizer --dump-tokens --dark > /dev/null 2>&1; then
            s2_ok=$((s2_ok + 1))
        else
            echo "  ERR  $name (--dump-tokens failed)"
            s2_fail=$((s2_fail + 1))
        fi
        # Light spot-check (mirror update-goldens.ts: only sample.py)
        if [[ "$name" == "sample.py" ]]; then
            "$SCREENSHOT_TOOL" "$sample" --colorizer --dump-tokens > /dev/null 2>&1 \
                || { echo "  ERR  $name (--dump-tokens light failed)"; s2_fail=$((s2_fail + 1)); }
        fi
    done
    echo "  Generated: $s2_ok OK, $s2_fail errors"
    if [[ $s2_fail -gt 0 ]]; then
        stage2_rc=1
    fi
    echo ""
    uv run --with Pillow python "$ROOT_DIR/test_data/compare_tokens.py" \
        --samples-dir "$SAMPLES_DIR"
    s2_compare_rc=$?
    if [[ $stage2_rc -eq 0 ]]; then
        stage2_rc=$s2_compare_rc
    fi
fi

# ===== Stage 3: Colorizer pixel smokes =====
echo ""
echo "=== Stage 3: Colorizer pixel smokes ==="
if [[ ! -d "$SMOKES_DIR" ]]; then
    echo "  SKIP  $SMOKES_DIR not present"
else
    s3_ok=0
    s3_fail=0
    for smoke in "$SMOKES_DIR"/*; do
        case "$smoke" in
            *.flags|*.png|*_diff.txt) continue ;;
        esac
        name="$(basename "$smoke")"
        flags_file="$SMOKES_DIR/${name}.flags"
        extra_args=("--full" "--dark")
        if [[ -f "$flags_file" ]]; then
            extra_args=($(tr -d '\r' < "$flags_file"))
        fi
        if "$SCREENSHOT_TOOL" "$smoke" "${extra_args[@]}" > /dev/null 2>&1; then
            s3_ok=$((s3_ok + 1))
        else
            echo "  ERR  $name"
            s3_fail=$((s3_fail + 1))
        fi
    done
    echo "  Generated: $s3_ok OK, $s3_fail errors"
    if [[ $s3_fail -gt 0 ]]; then
        stage3_rc=1
    fi
    echo ""
    # Smokes write <name>_dark.png; rename to <name>.png so compare.py picks them up
    # against <name>_golden.png. Symmetric with the markdown side's <stem>.png convention.
    for png in "$SMOKES_DIR"/*_dark.png; do
        [[ -f "$png" ]] || continue
        target="${png%_dark.png}.png"
        cp "$png" "$target"
    done
    uv run --with Pillow python "$ROOT_DIR/test_data/compare.py" --subdir colorizer_smokes
    s3_compare_rc=$?
    if [[ $stage3_rc -eq 0 ]]; then
        stage3_rc=$s3_compare_rc
    fi
fi

# ===== Summary =====
echo ""
echo "=== Summary ==="
[[ $stage1_rc -eq 0 ]] && echo "  Stage 1 (markdown):       PASS" || echo "  Stage 1 (markdown):       FAIL"
[[ $stage2_rc -eq 0 ]] && echo "  Stage 2 (colorizer tok):  PASS" || echo "  Stage 2 (colorizer tok):  FAIL ($stage2_rc)"
[[ $stage3_rc -eq 0 ]] && echo "  Stage 3 (colorizer pix):  PASS" || echo "  Stage 3 (colorizer pix):  FAIL"

if [[ $stage1_rc -ne 0 || $stage2_rc -ne 0 || $stage3_rc -ne 0 ]]; then
    exit 1
fi
exit 0
```

- [ ] **Step 3: Run the full pipeline**

```bash
./scripts/visual-test.sh
```

Expected: every stage PASS, exit 0. Each stage's "ERR" line should be empty (zero generation failures), each `compare*.py` should report all PASS.

If a stage fails, do not move on — debug. Common causes:
- "compare.py: No cases compared" in stage 3 → the `_dark.png` → `.png` rename loop didn't run, or the smokes directory has unexpected file extensions.
- "config_hash drift" in stage 2 → the goldens were regenerated under different display config than the tool now uses; rerun `bun run update-goldens` and re-stage.

- [ ] **Step 4: Commit**

```bash
git add scripts/visual-test.sh
git -c commit.gpgsign=false commit -m "feat(scripts): visual-test.sh adds stages 2 and 3, accumulate-then-exit

Stage 2 walks grammar_samples/ via screenshot_tool --dump-tokens then
compare_tokens.py. Stage 3 walks colorizer_smokes/ via screenshot_tool
--full then compare.py --subdir colorizer_smokes. Drops set -e
short-circuit between stages so one push surfaces all failures; within
a stage, fail-fast on tool errors stays.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Stage 9 — Pre-commit hook globs + `.gitignore`

### Task 9.1: Extend pre-commit RELEVANT regex

**Files:**
- Modify: `.githooks/pre-commit`

- [ ] **Step 1: Edit the regex**

Open `.githooks/pre-commit`. Replace:

```bash
RELEVANT="\.cpp$|\.h$|\.toml$|test_data/cases/.*\.md$"
```

with:

```bash
RELEVANT="\.cpp$|\.h$|\.toml$|test_data/cases/.*\.md$|test_data/grammar_samples/sample\.[^.]+$|test_data/colorizer_smokes/.*|config/themes/.*\.toml$|grammars/.*/queries/.*\.scm$"
```

The new alternations:

- `test_data/grammar_samples/sample\.[^.]+$` — fires on the source files but not on the goldens (`*_tokens.dark.golden.json` and `*_tokens.light.golden.json` both have a second `.` after the stem, so `[^.]+$` excludes them).
- `test_data/colorizer_smokes/.*` — fires on smoke source files and `.flags` sidecars. Goldens (`*_golden.png`) match too — but the hook running on a regen-only commit re-validates and passes, so this is harmless. (Acceptable per the spec: regen-only commits *can* be triggered, but the cost is one extra `visual-test.sh` run that always passes against fresh goldens.)
- `config/themes/.*\.toml$` — theme TOML changes.
- `grammars/.*/queries/.*\.scm$` — tree-sitter query changes.

- [ ] **Step 2: Smoke-test the hook**

```bash
# Stage a no-op change to a relevant file
touch test_data/grammar_samples/sample.cpp
git add test_data/grammar_samples/sample.cpp
git diff --cached --name-only

# Manually invoke the hook
.githooks/pre-commit
```

Expected: hook runs `visual-test.sh` (you'll see all three stages). Reset:

```bash
git reset HEAD test_data/grammar_samples/sample.cpp
git checkout -- test_data/grammar_samples/sample.cpp
```

Then test the *negative* case — staging only a golden:

```bash
touch test_data/grammar_samples/sample.cpp_tokens.dark.golden.json
git add test_data/grammar_samples/sample.cpp_tokens.dark.golden.json
.githooks/pre-commit
```

Expected: hook prints nothing and exits 0 (the regex doesn't match goldens). Reset:

```bash
git reset HEAD test_data/grammar_samples/sample.cpp_tokens.dark.golden.json
git checkout -- test_data/grammar_samples/sample.cpp_tokens.dark.golden.json
```

- [ ] **Step 3: Commit**

```bash
git add .githooks/pre-commit
git -c commit.gpgsign=false commit -m "ci(hooks): pre-commit fires on grammar samples, themes, queries, smokes

Regex extended to cover test_data/grammar_samples/sample.*,
test_data/colorizer_smokes/*, config/themes/*.toml,
grammars/*/queries/*.scm. Token-JSON goldens (which carry a second .
in their filename) do not trigger by construction.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task 9.2: Update `.gitignore`

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: Append the new ignore rules**

Add to `.gitignore`:

```
# Colorizer visual regression — just-produced files (goldens are committed)
test_data/grammar_samples/*_tokens.dark.json
test_data/grammar_samples/*_tokens.light.json
test_data/grammar_samples/*_tokens.*_diff.txt
test_data/grammar_samples/*.png
test_data/colorizer_smokes/*_dark.png
test_data/colorizer_smokes/*.png
!test_data/colorizer_smokes/*_golden.png
test_data/colorizer_smokes/*_diff.png
```

The `!test_data/colorizer_smokes/*_golden.png` re-includes the goldens after the broader `*.png` exclusion. Make sure the markdown side's existing exclusions (under `test_data/cases/`) aren't disturbed — read the current `.gitignore` first and place these new rules in a coherent block.

- [ ] **Step 2: Verify ignored set is right**

```bash
# Trigger a render to produce just-output files, then check git status
./scripts/visual-test.sh > /dev/null
git status --short
```

Expected: only goldens that are intentionally tracked appear; no `*_tokens.dark.json` (without `.golden`) or stray `*_dark.png` shows up. If they do, the `.gitignore` patterns aren't matching — add `**/` prefix or revisit pattern syntax.

- [ ] **Step 3: Commit**

```bash
git add .gitignore
git -c commit.gpgsign=false commit -m "chore(.gitignore): exclude colorizer just-produced JSON, PNG, and diff files

Goldens (*_tokens.<theme>.golden.json, *_golden.png) stay tracked; the
intermediate files written by screenshot_tool / compare_tokens.py do
not. Mirrors the markdown-side .gitignore conventions.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Stage 10 — Bring-up validation

This stage produces no commits. It exists so you do not ship a "regression suite" that's actually a tautology.

### Task 10.1: Spot-check three samples manually

**Files:** none (visual inspection)

- [ ] **Step 1: Open each in TC**

Open `test_data/grammar_samples/sample.cpp`, `sample.py`, and `sample.rs` in Total Commander with the `wlx-listerine-colorizer` plugin enabled (or any host that loads it). Visually verify:

- `sample.cpp` — `int`, `void`, `class`, `return` rendered as keywords; string literals visibly distinct; comments italic.
- `sample.py` — `def`, `import`, `return` keyword-styled; `"…"` strings distinct; `#` comments italic.
- `sample.rs` — `fn`, `let`, `mut`, `pub` keyword-styled; `//` comments italic; lifetime annotations (`'a`) at minimum visible.

If any sample looks wrong (e.g. all-white, no italic on comments), the colorizer is broken on master — file a bug, do not commit a frozen-broken golden.

- [ ] **Step 2: No commit** — visual verification only.

### Task 10.2: Verify the unreal smoke catches its variant

**Files:** none

- [ ] **Step 1: Render with each grammar, diff**

```bash
./build/Release/screenshot_tool.exe test_data/colorizer_smokes/smoke_unreal_cpp_variant.cpp \
    --full --colorizer --cpp-grammar standard --dark
mv test_data/colorizer_smokes/smoke_unreal_cpp_variant_dark.png /tmp/std.png

./build/Release/screenshot_tool.exe test_data/colorizer_smokes/smoke_unreal_cpp_variant.cpp \
    --full --colorizer --cpp-grammar unreal --dark
mv test_data/colorizer_smokes/smoke_unreal_cpp_variant_dark.png /tmp/unreal.png

sha256sum /tmp/std.png /tmp/unreal.png
```

Expected: **different** sha256s. If identical, the unreal grammar isn't actually being routed. Restore the unreal-variant render before continuing:

```bash
./build/Release/screenshot_tool.exe test_data/colorizer_smokes/smoke_unreal_cpp_variant.cpp \
    --full --colorizer --cpp-grammar unreal --dark
```

(After this restore, the smoke's `_dark.png` is freshly produced and matches the committed `_golden.png`.)

### Task 10.3: Plant a regression, confirm the suite catches it

**Files:** transient edit to `config/themes/<active_dark_theme>.toml`

- [ ] **Step 1: Find the active dark theme**

```bash
grep -n "^theme" config/wlx-listerine-md.toml || true
grep -n "^theme" config/wlx-listerine-colorizer.toml || true
ls config/themes/
```

Identify the active dark theme TOML (e.g. `default_dark.toml`).

- [ ] **Step 2: Swap two scopes' colors**

In the active dark theme file, find two prominent scopes and swap their colors. Example (your scope/color names will differ; adapt):

```toml
"keyword" = "#FF7B72"     # was, e.g., "#FF7B72"
"string"  = "#A5D6FF"     # was, e.g., "#A5D6FF"
```

Swap to:

```toml
"keyword" = "#A5D6FF"
"string"  = "#FF7B72"
```

- [ ] **Step 3: Run the suite, expect FAIL**

```bash
./scripts/visual-test.sh
```

Expected: stage 2 reports many `FAIL` lines (every cpp/py/rs/etc. sample with keywords or strings). Stage 3 also fails on at least `smoke_modifiers` and `smoke_dark`.

If the suite passes, the comparator is broken — do not proceed; investigate.

- [ ] **Step 4: Revert the theme edit**

```bash
git checkout -- config/themes/
./scripts/visual-test.sh
```

Expected: every stage PASS again.

- [ ] **Step 5: No commit** — validation only.

### Task 10.4: Final green confirmation + record results

- [ ] **Step 1: One last clean run from a quiet state**

```bash
git status   # must show clean working tree
./scripts/visual-test.sh
```

Expected: every stage PASS, summary section shows three PASS lines, exit 0.

- [ ] **Step 2: Append a short "validation performed" note to the most recent merge commit / PR description**

When the PR for this work is opened (or the branch is merged), include in the description:

> **Bring-up validation performed:**
> - Three samples spot-checked in TC: `sample.cpp`, `sample.py`, `sample.rs` — keywords, strings, comments render as expected.
> - Unreal smoke verified non-tautological: `sha256(std)` ≠ `sha256(unreal)`.
> - Planted-regression test: swapped `keyword` / `string` colors in the dark theme, suite reported FAIL on (N) samples + 2 smokes; reverted, suite green.

This proves the goldens aren't a self-fulfilling prophecy. The note is a one-time artifact for the introducing PR.

---

## Self-Review

### Spec coverage

Walking the spec section by section:

- **Architecture (3 stages, 1 pipeline)** — Stage 8 (visual-test.sh restructure).
- **Reused without modification** — `compare.py` (Stage 4), `RenderEngine` etc. (Stages 3.3, 3.4), `update-goldens` (Stage 7), pre-commit hook (Stage 9.1).
- **New artifacts:** screenshot_tool flags + extracted pipelines (Stages 3.1–3.5), `compare_tokens.py` (Stage 5), smokes dir (Stage 6), token JSON goldens (Stage 7), pixel goldens (Stage 7), JSON-writer test (Stage 1.1) and wiring-alive smoke (Stage 2.1).
- **Components / 1. screenshot_tool extensions** — Stages 3.1–3.5.
- **Components / 2. Token JSON schema** — Stage 1 (writer + tests).
- **Components / 3. compare_tokens.py** — Stage 5.
- **Components / 4. Pre-commit hook trigger globs** — Stage 9.1.
- **Components / 5. Doctest wiring smoke** — Stage 2.1.
- **Components / 6. JSON writer determinism unit test** — Stage 1.1 (covered by 7 sub-tests).
- **Scope coverage (27 token + 5 pixel)** — Stages 6 + 7.
- **Regen flow** — Stage 7.
- **Failure modes** table — covered by Stages 5 (compare_tokens.py) + 8 (visual-test.sh) + 9 (hook globs).
- **Determinism / stability hazards** — sort/collapse covered by Stage 1 tests; palette resolution unaffected (resolved hex carried in JSON); pixel float drift handled by existing `compare.py` thresholds.
- **Testing strategy / what gates merge** — every gate listed in the spec is exercised by Stages 0/8/10 runs.
- **Out of scope** — explicitly not tasked.

No spec gaps detected.

### Placeholder scan

Searched plan text for "TBD", "TODO", "FIXME", "implement later", "fill in details", "Add appropriate error handling", "Similar to Task". None present. The one phrase "the field name `content->normalized_utf16` may differ" is followed by an explicit verification step (Task 3.3 Step 2) — that's a defensive instruction, not a placeholder, since the implementer has a concrete grep command and a clear "use what the file declares" rule.

### Type / signature consistency

- `Options` struct: declared once in `options.h` (Stage 3.1), referenced by both pipelines and by `parse_args`. New colorizer fields added in Stage 3.1 are used in Stage 3.2 (parser), Stage 3.3 (paint), Stage 3.4 (dump-tokens), Stage 3.5 (display config). Field names consistent: `colorizer`, `lang`, `cpp_grammar`, `dump_tokens`, `display_config`.
- `TokenJsonWriter::write(...)` signature: `(const ColorizeResult&, std::string_view, const TokenJsonOptions&) -> std::string` — same in test (Task 1.1), header (Task 1.2), and pipeline call (Task 3.4).
- `ColorizerDisplayConfig` field names match `colorizer_layout.h`: `line_numbers`, `word_wrap`, `tab_width`, `line_height_factor`, `show_whitespace`, `show_indent_guides`, `highlight_trailing`, `cpp_grammar` — all present in 3.5 TOML loader and verified against the header read at the start of writing.
- `compare_tokens.py` exit codes: 0 / 1 / 2 — consistent across tests (Task 5.1) and implementation (Task 5.2) and visual-test.sh (Stage 8).
- `_tokens.<theme>.json` filename convention: written by tool (Task 3.4), copied to golden by update-goldens (Task 7.1), walked by compare_tokens.py (Task 5.2), gitignored (Task 9.2). All four agree on the `.<theme>.` infix.
- Smoke output PNGs: tool writes `<name>_dark.png` (Task 3.3), update-goldens copies to `<name>_golden.png` (Task 7.1), visual-test.sh renames `<name>_dark.png` → `<name>.png` so compare.py picks it up (Stage 8). compare.py looks for `<stem>.png` vs `<stem>_golden.png` (consistent with the markdown side's existing convention).

No drift detected.
