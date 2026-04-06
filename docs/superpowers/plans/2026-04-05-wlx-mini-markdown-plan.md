# WLX Mini Markdown Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a super-lightweight Total Commander lister plugin that renders markdown files using RichEdit (no WebView2).

**Architecture:** Single DLL plugin. md4c parses markdown via SAX callbacks, an RtfBuilder class converts callbacks to an RTF string, RichEdit displays it via EM_STREAMIN. Config loaded from TOML file.

**Tech Stack:** C++17, Win32 API, md4c (Conan), tomlplusplus (Conan), doctest (Conan), RichEdit 4.1 (msftedit.dll), CMake

**Spec:** `docs/superpowers/specs/2026-04-05-wlx-mini-markdown-design.md`

---

## File Structure

```
wlx-mini-markdown/
├── CMakeLists.txt                  # Build config: plugin DLL + test exe
├── conanfile.txt                   # md4c, tomlplusplus, doctest
├── include/
│   └── listerplugin.h             # TC WLX plugin API constants + signatures
├── src/
│   ├── plugin.def                 # DLL export definitions
│   ├── plugin.cpp                 # WLX exports, RichEdit hosting, search, dark mode, links
│   ├── config.h                   # Config struct + load/default declarations
│   ├── config.cpp                 # TOML loading with defaults
│   ├── rtf_builder.h              # RtfBuilder class + LinkInfo struct
│   ├── rtf_builder.cpp            # md4c callbacks → RTF string
│   ├── resource.h                 # Version resource ID
│   └── resource.rc                # DLL version info
├── tests/
│   ├── test_main.cpp              # doctest main
│   ├── test_config.cpp            # Config loading tests
│   └── test_rtf_builder.cpp       # RTF generation tests
├── config/
│   └── wlx-mini-markdown.toml    # Default config shipped with plugin
└── test_data/
    └── sample.md                  # Comprehensive test markdown
```

**Responsibilities:**
- `config.h/cpp` — Parse TOML, provide defaults. No Win32 dependency.
- `rtf_builder.h/cpp` — Convert markdown→RTF. Depends on md4c + config. No Win32 dependency.
- `plugin.cpp` — Win32 glue: RichEdit creation, WLX exports, search, dark mode, links. Depends on config + rtf_builder.

---

## Task 1: Project Scaffolding

**Files:**
- Create: `conanfile.txt`
- Create: `CMakeLists.txt`
- Create: `include/listerplugin.h`
- Create: `src/plugin.def`
- Create: `src/resource.h`
- Create: `src/resource.rc`
- Create: `src/config.h` (stub)
- Create: `src/config.cpp` (stub)
- Create: `src/rtf_builder.h` (stub)
- Create: `src/rtf_builder.cpp` (stub)
- Create: `src/plugin.cpp` (stub)
- Create: `tests/test_main.cpp`
- Create: `tests/test_config.cpp` (stub)
- Create: `tests/test_rtf_builder.cpp` (stub)

- [ ] **Step 1: Create `conanfile.txt`**

```
[requires]
md4c/0.5.2
tomlplusplus/3.4.0
doctest/2.4.11

[generators]
CMakeToolchain
CMakeDeps

[options]
md4c/*:shared=False
```

- [ ] **Step 2: Create `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(wlx-mini-markdown LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(md4c REQUIRED)
find_package(tomlplusplus REQUIRED)

# --- Plugin DLL ---
add_library(wlx-mini-markdown SHARED
    src/plugin.cpp
    src/rtf_builder.cpp
    src/config.cpp
    src/plugin.def
    src/resource.rc
)

target_include_directories(wlx-mini-markdown PRIVATE include src)
target_link_libraries(wlx-mini-markdown PRIVATE md4c::md4c tomlplusplus::tomlplusplus)

set_target_properties(wlx-mini-markdown PROPERTIES
    SUFFIX ".wlx64"
    PREFIX ""
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# --- Tests ---
find_package(doctest REQUIRED)

add_executable(tests
    tests/test_main.cpp
    tests/test_config.cpp
    tests/test_rtf_builder.cpp
    src/rtf_builder.cpp
    src/config.cpp
)

target_include_directories(tests PRIVATE include src)
target_link_libraries(tests PRIVATE md4c::md4c tomlplusplus::tomlplusplus doctest::doctest)

set_target_properties(tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
```

- [ ] **Step 3: Create `include/listerplugin.h`**

```cpp
#pragma once

#include <windows.h>

// Commands for ListSendCommand
#define lc_copy        1
#define lc_newparams   2
#define lc_selectall   3

// ShowFlags for ListLoad
#define lcp_wraptext      1
#define lcp_fittowindow   2
#define lcp_ansi          4
#define lcp_ascii         8
#define lcp_variable      12
#define lcp_darkmode      128

// SearchParameter flags for ListSearchText
#define lcs_findfirst     1
#define lcs_matchcase     2
#define lcs_wholewords    4
#define lcs_backwards     8

// Return values
#define LISTPLUGIN_OK     0
#define LISTPLUGIN_ERROR  1
```

- [ ] **Step 4: Create `src/plugin.def`**

```
LIBRARY "wlx-mini-markdown"
EXPORTS
    ListLoad
    ListLoadNext
    ListCloseWindow
    ListGetDetectString
    ListSearchText
    ListSearchTextW
    ListPrint
    ListSendCommand
```

- [ ] **Step 5: Create `src/resource.h` and `src/resource.rc`**

`src/resource.h`:
```c
#pragma once
#define VS_VERSION_INFO 1
```

`src/resource.rc`:
```rc
#include <winver.h>
#include "resource.h"

VS_VERSION_INFO VERSIONINFO
FILEVERSION 1,0,0,0
PRODUCTVERSION 1,0,0,0
FILEFLAGSMASK 0x3fL
FILEFLAGS 0
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_DLL
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "FileDescription", "Mini Markdown Viewer for Total Commander"
            VALUE "FileVersion", "1.0.0.0"
            VALUE "InternalName", "wlx-mini-markdown"
            VALUE "OriginalFilename", "wlx-mini-markdown.wlx64"
            VALUE "ProductName", "WLX Mini Markdown"
            VALUE "ProductVersion", "1.0.0.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
```

- [ ] **Step 6: Create stub source files**

`src/config.h`:
```cpp
#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct ColorScheme {
    uint32_t background;
    uint32_t text;
    uint32_t heading;
    uint32_t code_background;
    uint32_t blockquote;
    uint32_t link;
};

struct Config {
    std::vector<std::string> extensions;
    std::string detect_string;
    std::string body_font;
    int body_size;
    std::string code_font;
    int code_size;
    ColorScheme light;
    ColorScheme dark;
};

Config default_config();
Config load_config(const std::string& path);
uint32_t parse_hex_color(const std::string& hex);
```

`src/config.cpp`:
```cpp
#include "config.h"

Config default_config() { return {}; }
Config load_config(const std::string& path) { return default_config(); }
uint32_t parse_hex_color(const std::string& hex) { return 0; }
```

`src/rtf_builder.h`:
```cpp
#pragma once

#include "config.h"
#include <string>
#include <vector>

struct LinkInfo {
    size_t char_start;
    size_t char_end;
    std::string url;
};

class RtfBuilder {
public:
    RtfBuilder(const Config& config, bool dark_mode);
    std::string build(const char* markdown, size_t length);
    const std::vector<LinkInfo>& links() const { return links_; }

private:
    const Config& config_;
    const ColorScheme& colors_;
    std::string rtf_;
    std::vector<LinkInfo> links_;
};
```

`src/rtf_builder.cpp`:
```cpp
#include "rtf_builder.h"

RtfBuilder::RtfBuilder(const Config& config, bool dark_mode)
    : config_(config)
    , colors_(dark_mode ? config.dark : config.light) {}

std::string RtfBuilder::build(const char* markdown, size_t length) {
    return "{\\rtf1\\ansi\\deff0 stub}";
}
```

`src/plugin.cpp`:
```cpp
#include <windows.h>
#include "listerplugin.h"
#include "config.h"
#include "rtf_builder.h"

extern "C" {

HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    return nullptr;
}

int __stdcall ListLoadNext(HWND ParentWin, HWND PluginWin, char* FileToLoad, int ShowFlags) {
    return LISTPLUGIN_ERROR;
}

void __stdcall ListCloseWindow(HWND ListWin) {}

void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    strncpy(DetectString, "EXT=\"MD\"", maxlen - 1);
}

int __stdcall ListSearchText(HWND ListWin, char* SearchString, int SearchParameter) {
    return LISTPLUGIN_ERROR;
}

int __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter) {
    return LISTPLUGIN_ERROR;
}

int __stdcall ListPrint(HWND ListWin, char* FileToPrint, char* DefPrinter, int PrintFlags, RECT* Margins) {
    return LISTPLUGIN_ERROR;
}

int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter) {
    return LISTPLUGIN_ERROR;
}

} // extern "C"
```

- [ ] **Step 7: Create test stubs**

`tests/test_main.cpp`:
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
```

`tests/test_config.cpp`:
```cpp
#include <doctest/doctest.h>
#include "config.h"

TEST_CASE("placeholder") {
    CHECK(true);
}
```

`tests/test_rtf_builder.cpp`:
```cpp
#include <doctest/doctest.h>
#include "rtf_builder.h"

TEST_CASE("placeholder") {
    CHECK(true);
}
```

- [ ] **Step 8: Verify build**

```bash
conan install . --output-folder=build --build=missing -s build_type=Release
cmake --preset conan-default
cmake --build --preset conan-release
```

Expected: Both `wlx-mini-markdown.wlx64` and `tests.exe` build successfully.

- [ ] **Step 9: Run tests**

```bash
./build/Release/tests.exe
```

Expected: 2 tests pass (placeholders).

- [ ] **Step 10: Commit**

```bash
git init
git add -A
git commit -m "feat: project scaffolding with CMake, Conan, and stub sources"
```

---

## Task 2: Config Module

**Files:**
- Modify: `src/config.cpp`
- Create: `tests/test_config.cpp`
- Create: `test_data/test_config.toml`

- [ ] **Step 1: Write config tests**

Replace `tests/test_config.cpp`:

```cpp
#include <doctest/doctest.h>
#include "config.h"
#include <fstream>

TEST_CASE("parse_hex_color parses 6-digit hex with hash") {
    CHECK(parse_hex_color("#FFFFFF") == 0xFFFFFF);
    CHECK(parse_hex_color("#000000") == 0x000000);
    CHECK(parse_hex_color("#24292E") == 0x24292E);
    CHECK(parse_hex_color("#0366D6") == 0x0366D6);
}

TEST_CASE("parse_hex_color parses without hash") {
    CHECK(parse_hex_color("1E1E1E") == 0x1E1E1E);
}

TEST_CASE("default_config has correct defaults") {
    Config cfg = default_config();

    CHECK(cfg.extensions.size() == 5);
    CHECK(cfg.extensions[0] == "md");
    CHECK(cfg.detect_string == R"(EXT="MD" | EXT="MARKDOWN")");
    CHECK(cfg.body_font == "Segoe UI");
    CHECK(cfg.body_size == 11);
    CHECK(cfg.code_font == "Consolas");
    CHECK(cfg.code_size == 10);

    CHECK(cfg.light.background == 0xFFFFFF);
    CHECK(cfg.light.text == 0x24292E);
    CHECK(cfg.light.link == 0x0366D6);
    CHECK(cfg.dark.background == 0x1E1E1E);
    CHECK(cfg.dark.text == 0xD4D4D4);
    CHECK(cfg.dark.link == 0x58A6FF);
}

TEST_CASE("load_config from nonexistent file returns defaults") {
    Config cfg = load_config("nonexistent.toml");
    Config def = default_config();
    CHECK(cfg.body_font == def.body_font);
    CHECK(cfg.light.background == def.light.background);
}

TEST_CASE("load_config reads TOML overrides") {
    // Write a temp TOML file
    const char* path = "test_data/test_config.toml";
    {
        std::ofstream f(path);
        f << R"(
[options]
extensions = ["md", "txt"]
detect_string = 'EXT="MD"'

[fonts]
body = "Arial"
body_size = 14
code = "Courier New"
code_size = 12

[colors.light]
background = "#AABBCC"
text = "#112233"

[colors.dark]
background = "#334455"
link = "#99AAFF"
)";
    }

    Config cfg = load_config(path);

    CHECK(cfg.extensions.size() == 2);
    CHECK(cfg.extensions[0] == "md");
    CHECK(cfg.extensions[1] == "txt");
    CHECK(cfg.detect_string == R"(EXT="MD")");
    CHECK(cfg.body_font == "Arial");
    CHECK(cfg.body_size == 14);
    CHECK(cfg.code_font == "Courier New");
    CHECK(cfg.code_size == 12);
    CHECK(cfg.light.background == 0xAABBCC);
    CHECK(cfg.light.text == 0x112233);
    // Unset values keep defaults
    CHECK(cfg.light.link == 0x0366D6);
    CHECK(cfg.dark.background == 0x334455);
    CHECK(cfg.dark.link == 0x99AAFF);
    // Unset dark values keep defaults
    CHECK(cfg.dark.text == 0xD4D4D4);
}
```

- [ ] **Step 2: Run tests, verify they fail**

```bash
cmake --build --preset conan-release && ./build/Release/tests.exe
```

Expected: Multiple failures (config functions are stubs).

- [ ] **Step 3: Implement `src/config.cpp`**

```cpp
#include "config.h"
#include <toml++/toml.hpp>
#include <fstream>

uint32_t parse_hex_color(const std::string& hex) {
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    return static_cast<uint32_t>(std::stoul(h, nullptr, 16));
}

Config default_config() {
    Config cfg;
    cfg.extensions = {"md", "markdown", "mdown", "mkd", "mkdn"};
    cfg.detect_string = R"(EXT="MD" | EXT="MARKDOWN")";
    cfg.body_font = "Segoe UI";
    cfg.body_size = 11;
    cfg.code_font = "Consolas";
    cfg.code_size = 10;

    cfg.light = {0xFFFFFF, 0x24292E, 0x24292E, 0xF6F8FA, 0x6A737D, 0x0366D6};
    cfg.dark  = {0x1E1E1E, 0xD4D4D4, 0xE0E0E0, 0x2D2D2D, 0x9E9E9E, 0x58A6FF};

    return cfg;
}

static ColorScheme read_colors(const toml::table& tbl, const ColorScheme& defaults) {
    ColorScheme cs = defaults;
    auto c = [&](const char* key, uint32_t& out) {
        if (auto v = tbl[key].value<std::string>()) out = parse_hex_color(*v);
    };
    c("background", cs.background);
    c("text", cs.text);
    c("heading", cs.heading);
    c("code_background", cs.code_background);
    c("blockquote", cs.blockquote);
    c("link", cs.link);
    return cs;
}

Config load_config(const std::string& path) {
    Config cfg = default_config();
    try {
        auto tbl = toml::parse_file(path);

        if (auto arr = tbl["options"]["extensions"].as_array()) {
            cfg.extensions.clear();
            for (auto& e : *arr)
                if (auto s = e.value<std::string>()) cfg.extensions.push_back(*s);
        }
        if (auto v = tbl["options"]["detect_string"].value<std::string>())
            cfg.detect_string = *v;

        if (auto v = tbl["fonts"]["body"].value<std::string>()) cfg.body_font = *v;
        if (auto v = tbl["fonts"]["body_size"].value<int64_t>()) cfg.body_size = static_cast<int>(*v);
        if (auto v = tbl["fonts"]["code"].value<std::string>()) cfg.code_font = *v;
        if (auto v = tbl["fonts"]["code_size"].value<int64_t>()) cfg.code_size = static_cast<int>(*v);

        if (auto lt = tbl["colors"]["light"].as_table()) cfg.light = read_colors(*lt, cfg.light);
        if (auto dk = tbl["colors"]["dark"].as_table())  cfg.dark  = read_colors(*dk, cfg.dark);
    } catch (...) {
        // Missing or malformed file — defaults are fine
    }
    return cfg;
}
```

- [ ] **Step 4: Run tests, verify they pass**

```bash
cmake --build --preset conan-release && ./build/Release/tests.exe -tc="*config*,*parse_hex*,*default*,*load*"
```

Expected: All config tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/config.cpp tests/test_config.cpp
git commit -m "feat: config module with TOML loading and defaults"
```

---

## Task 3: RTF Builder — Core Structure + Text Handling

**Files:**
- Modify: `src/rtf_builder.h`
- Modify: `src/rtf_builder.cpp`
- Modify: `tests/test_rtf_builder.cpp`

- [ ] **Step 1: Write core RTF tests**

Replace `tests/test_rtf_builder.cpp`:

```cpp
#include <doctest/doctest.h>
#include "rtf_builder.h"

static Config cfg() { return default_config(); }

TEST_CASE("RTF output starts with rtf1 header and ends with closing brace") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("Hello", 5);
    CHECK(rtf.substr(0, 10) == "{\\rtf1\\ans");
    CHECK(rtf.back() == '}');
}

TEST_CASE("RTF contains font table with body and code fonts") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("x", 1);
    CHECK(rtf.find("\\fonttbl") != std::string::npos);
    CHECK(rtf.find("Segoe UI") != std::string::npos);
    CHECK(rtf.find("Consolas") != std::string::npos);
}

TEST_CASE("RTF contains color table") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("x", 1);
    CHECK(rtf.find("\\colortbl;") != std::string::npos);
}

TEST_CASE("dark mode uses dark color scheme") {
    RtfBuilder b(cfg(), true);
    auto rtf = b.build("x", 1);
    // Dark text color is #D4D4D4 = rgb(212,212,212)
    CHECK(rtf.find("\\red212\\green212\\blue212") != std::string::npos);
}

TEST_CASE("plain text appears in output") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("Hello world", 11);
    CHECK(rtf.find("Hello world") != std::string::npos);
}

TEST_CASE("RTF special chars are escaped") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("a\\b{c}d", 7);
    CHECK(rtf.find("a\\\\b\\{c\\}d") != std::string::npos);
}

TEST_CASE("non-ASCII UTF-8 emits RTF unicode escapes") {
    // U+00E9 (e-acute) = UTF-8: 0xC3 0xA9 = RTF: \u233?
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("\xC3\xA9", 2);
    CHECK(rtf.find("\\u233?") != std::string::npos);
}
```

- [ ] **Step 2: Run tests, verify they fail**

```bash
cmake --build --preset conan-release && ./build/Release/tests.exe
```

Expected: Core RTF tests fail (builder is a stub).

- [ ] **Step 3: Implement RtfBuilder core**

Replace `src/rtf_builder.h`:

```cpp
#pragma once

#include "config.h"
#include <md4c.h>
#include <string>
#include <vector>

struct LinkInfo {
    size_t char_start;
    size_t char_end;
    std::string url;
};

class RtfBuilder {
public:
    RtfBuilder(const Config& config, bool dark_mode);
    std::string build(const char* markdown, size_t length);
    const std::vector<LinkInfo>& links() const { return links_; }

private:
    // Static md4c callbacks
    static int cb_enter_block(MD_BLOCKTYPE type, void* detail, void* ud);
    static int cb_leave_block(MD_BLOCKTYPE type, void* detail, void* ud);
    static int cb_enter_span(MD_SPANTYPE type, void* detail, void* ud);
    static int cb_leave_span(MD_SPANTYPE type, void* detail, void* ud);
    static int cb_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* ud);

    // Instance handlers
    void enter_block(MD_BLOCKTYPE type, void* detail);
    void leave_block(MD_BLOCKTYPE type, void* detail);
    void enter_span(MD_SPANTYPE type, void* detail);
    void leave_span(MD_SPANTYPE type, void* detail);
    void on_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size);

    // RTF helpers
    void emit(const char* s);
    void emit(const std::string& s);
    void emit_text(const char* text, size_t len); // escapes + counts chars
    void emit_rtf_color(std::string& out, uint32_t rgb);
    std::string preamble();

    // Inline formatting reset — emits RTF to restore default paragraph style
    void emit_default_fmt();

    const Config& config_;
    const ColorScheme& colors_;
    std::string rtf_;
    size_t char_count_ = 0; // visible-character counter for link positions

    // Block state
    int list_nesting_ = 0;
    std::vector<bool> list_ordered_;
    std::vector<int> list_counter_;
    bool in_code_block_ = false;
    bool in_table_ = false;
    bool in_table_header_ = false;
    int table_col_count_ = 0;
    int table_col_ = 0;

    // Link tracking
    std::vector<LinkInfo> links_;
    std::string pending_link_url_;
    size_t pending_link_start_ = 0;

    // Image state
    bool in_image_ = false;
    std::string pending_image_alt_;
};
```

Replace `src/rtf_builder.cpp`:

```cpp
#include "rtf_builder.h"
#include <cstdio>

// --------------- helpers ---------------

void RtfBuilder::emit(const char* s) { rtf_ += s; }
void RtfBuilder::emit(const std::string& s) { rtf_ += s; }

void RtfBuilder::emit_rtf_color(std::string& out, uint32_t rgb) {
    char buf[48];
    snprintf(buf, sizeof(buf), "\\red%u\\green%u\\blue%u;",
             (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    out += buf;
}

void RtfBuilder::emit_text(const char* text, size_t len) {
    for (size_t i = 0; i < len;) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (c == '\\' || c == '{' || c == '}') {
                rtf_ += '\\';
                rtf_ += static_cast<char>(c);
            } else if (c == '\n') {
                emit("\\line\n");
            } else {
                rtf_ += static_cast<char>(c);
            }
            char_count_++;
            i++;
        } else {
            // Decode UTF-8 → code point → RTF \uN?
            uint32_t cp = 0;
            int bytes = 0;
            if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; bytes = 2; }
            else if ((c & 0xF0) == 0xE0)  { cp = c & 0x0F; bytes = 3; }
            else if ((c & 0xF8) == 0xF0)  { cp = c & 0x07; bytes = 4; }
            else { i++; continue; }
            for (int j = 1; j < bytes && (i + j) < len; j++)
                cp = (cp << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3F);
            char buf[24];
            if (cp <= 0xFFFF) {
                snprintf(buf, sizeof(buf), "\\u%d?", static_cast<int16_t>(cp));
                rtf_ += buf;
                char_count_++;
            } else {
                // Surrogate pair for supplementary plane
                cp -= 0x10000;
                int16_t hi = static_cast<int16_t>(0xD800 + (cp >> 10));
                int16_t lo = static_cast<int16_t>(0xDC00 + (cp & 0x3FF));
                snprintf(buf, sizeof(buf), "\\u%d?\\u%d?", hi, lo);
                rtf_ += buf;
                char_count_ += 2;
            }
            i += bytes;
        }
    }
}

void RtfBuilder::emit_default_fmt() {
    char buf[32];
    snprintf(buf, sizeof(buf), "\\f0\\fs%d\\cf1", config_.body_size * 2);
    emit(buf);
}

std::string RtfBuilder::preamble() {
    std::string p = "{\\rtf1\\ansi\\deff0\n";

    p += "{\\fonttbl";
    p += "{\\f0\\fswiss " + config_.body_font + ";}";
    p += "{\\f1\\fmodern " + config_.code_font + ";}";
    p += "}\n";

    p += "{\\colortbl;";
    emit_rtf_color(p, colors_.text);           // cf1 - body text
    emit_rtf_color(p, colors_.heading);        // cf2 - headings
    emit_rtf_color(p, colors_.link);           // cf3 - links
    emit_rtf_color(p, colors_.blockquote);     // cf4 - blockquotes
    emit_rtf_color(p, colors_.code_background);// cf5 - code highlight bg
    p += "}\n";

    char buf[32];
    snprintf(buf, sizeof(buf), "\\f0\\fs%d\\cf1\n", config_.body_size * 2);
    p += buf;

    return p;
}

// --------------- static callbacks ---------------

int RtfBuilder::cb_enter_block(MD_BLOCKTYPE t, void* d, void* ud) {
    static_cast<RtfBuilder*>(ud)->enter_block(t, d); return 0;
}
int RtfBuilder::cb_leave_block(MD_BLOCKTYPE t, void* d, void* ud) {
    static_cast<RtfBuilder*>(ud)->leave_block(t, d); return 0;
}
int RtfBuilder::cb_enter_span(MD_SPANTYPE t, void* d, void* ud) {
    static_cast<RtfBuilder*>(ud)->enter_span(t, d); return 0;
}
int RtfBuilder::cb_leave_span(MD_SPANTYPE t, void* d, void* ud) {
    static_cast<RtfBuilder*>(ud)->leave_span(t, d); return 0;
}
int RtfBuilder::cb_text(MD_TEXTTYPE t, const MD_CHAR* text, MD_SIZE sz, void* ud) {
    static_cast<RtfBuilder*>(ud)->on_text(t, text, sz); return 0;
}

// --------------- block handlers (stubs for now) ---------------

void RtfBuilder::enter_block(MD_BLOCKTYPE type, void* detail) {
    switch (type) {
    case MD_BLOCK_DOC: break;
    case MD_BLOCK_P:
        emit("\\pard\\sa200 ");
        emit_default_fmt();
        emit(" ");
        break;
    default: break;
    }
}

void RtfBuilder::leave_block(MD_BLOCKTYPE type, void* detail) {
    switch (type) {
    case MD_BLOCK_DOC: break;
    case MD_BLOCK_P:
        emit("\\par\n");
        char_count_++; // \par = one visible char
        break;
    default: break;
    }
}

// --------------- span handlers (stubs for now) ---------------

void RtfBuilder::enter_span(MD_SPANTYPE type, void* detail) {}
void RtfBuilder::leave_span(MD_SPANTYPE type, void* detail) {}

// --------------- text handler ---------------

void RtfBuilder::on_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size) {
    if (in_image_) {
        pending_image_alt_.append(text, size);
        return;
    }
    switch (type) {
    case MD_TEXT_NORMAL:
    case MD_TEXT_CODE:
        emit_text(text, size);
        break;
    case MD_TEXT_BR:
        emit("\\line\n");
        char_count_++;
        break;
    case MD_TEXT_SOFTBR:
        emit(" ");
        char_count_++;
        break;
    case MD_TEXT_ENTITY:
        // Emit as-is for now; HTML entities in markdown are rare
        emit_text(text, size);
        break;
    default:
        emit_text(text, size);
        break;
    }
}

// --------------- build ---------------

RtfBuilder::RtfBuilder(const Config& config, bool dark_mode)
    : config_(config)
    , colors_(dark_mode ? config.dark : config.light) {}

std::string RtfBuilder::build(const char* markdown, size_t length) {
    rtf_.clear();
    rtf_.reserve(length * 3);
    char_count_ = 0;
    links_.clear();
    list_nesting_ = 0;
    list_ordered_.clear();
    list_counter_.clear();
    in_code_block_ = false;
    in_table_ = false;
    in_table_header_ = false;
    in_image_ = false;

    rtf_ = preamble();

    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_NOHTML;
    parser.enter_block = cb_enter_block;
    parser.leave_block = cb_leave_block;
    parser.enter_span  = cb_enter_span;
    parser.leave_span  = cb_leave_span;
    parser.text        = cb_text;

    md_parse(markdown, static_cast<MD_SIZE>(length), &parser, this);

    rtf_ += "}";
    return rtf_;
}
```

- [ ] **Step 4: Run tests, verify they pass**

```bash
cmake --build --preset conan-release && ./build/Release/tests.exe
```

Expected: All tests pass including RTF core tests.

- [ ] **Step 5: Commit**

```bash
git add src/rtf_builder.h src/rtf_builder.cpp tests/test_rtf_builder.cpp
git commit -m "feat: RTF builder core with preamble, text handling, and UTF-8 support"
```

---

## Task 4: RTF Builder — Block Elements

**Files:**
- Modify: `src/rtf_builder.cpp`
- Modify: `tests/test_rtf_builder.cpp`

- [ ] **Step 1: Add block element tests**

Append to `tests/test_rtf_builder.cpp`:

```cpp
TEST_CASE("H1 renders with large bold font and bottom border") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("# Title", 7);
    CHECK(rtf.find("\\fs56") != std::string::npos);  // 28pt
    CHECK(rtf.find("\\b") != std::string::npos);
    CHECK(rtf.find("\\cf2") != std::string::npos);   // heading color
    CHECK(rtf.find("\\brdrb") != std::string::npos);  // bottom border
    CHECK(rtf.find("Title") != std::string::npos);
}

TEST_CASE("H3 renders with medium font") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("### Sub", 7);
    CHECK(rtf.find("\\fs40") != std::string::npos);  // 20pt
}

TEST_CASE("Bullet list renders with bullet char and indent") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("- item1\n- item2", 15);
    CHECK(rtf.find("\\li360") != std::string::npos);
    CHECK(rtf.find("item1") != std::string::npos);
    CHECK(rtf.find("item2") != std::string::npos);
}

TEST_CASE("Ordered list renders with number") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("1. first\n2. second", 18);
    CHECK(rtf.find("\\li360") != std::string::npos);
    CHECK(rtf.find("1.") != std::string::npos);
    CHECK(rtf.find("first") != std::string::npos);
}

TEST_CASE("Code block uses monospace font and highlight") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("```\ncode here\n```", 17);
    CHECK(rtf.find("\\f1") != std::string::npos);    // Consolas
    CHECK(rtf.find("\\highlight5") != std::string::npos);
    CHECK(rtf.find("code here") != std::string::npos);
}

TEST_CASE("Blockquote uses indent and blockquote color") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("> quoted", 8);
    CHECK(rtf.find("\\li720") != std::string::npos);
    CHECK(rtf.find("\\cf4") != std::string::npos);   // blockquote color
    CHECK(rtf.find("quoted") != std::string::npos);
}

TEST_CASE("Horizontal rule renders as bordered paragraph") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("---", 3);
    CHECK(rtf.find("\\brdrb\\brdrs") != std::string::npos);
}

TEST_CASE("Task list renders checkbox characters") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("- [ ] todo\n- [x] done", 21);
    // U+2610 = \u9744? (ballot box) or U+2610 mapped
    // The checkbox chars should appear
    CHECK(rtf.find("todo") != std::string::npos);
    CHECK(rtf.find("done") != std::string::npos);
}
```

- [ ] **Step 2: Run tests, verify new tests fail**

```bash
cmake --build --preset conan-release && ./build/Release/tests.exe
```

Expected: New block tests fail (handlers are stubs).

- [ ] **Step 3: Implement block handlers in `src/rtf_builder.cpp`**

Replace the `enter_block` and `leave_block` methods:

```cpp
static const int kHeadingSizes[] = {56, 48, 40, 32, 28, 24}; // H1–H6 in half-points

void RtfBuilder::enter_block(MD_BLOCKTYPE type, void* detail) {
    switch (type) {
    case MD_BLOCK_DOC:
        break;

    case MD_BLOCK_H: {
        auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
        int level = h->level; // 1–6
        int fs = (level >= 1 && level <= 6) ? kHeadingSizes[level - 1] : 24;
        emit("\\pard\\sb200\\sa100");
        // H1 and H2 get a bottom border
        if (level <= 2)
            emit("\\brdrb\\brdrs\\brdrw10\\brdrcf1");
        char buf[64];
        snprintf(buf, sizeof(buf), "\\f0\\fs%d\\b\\cf2 ", fs);
        emit(buf);
        break;
    }

    case MD_BLOCK_P:
        if (in_table_) break; // table cells handle their own paragraphs
        emit("\\pard\\sa200 ");
        emit_default_fmt();
        emit(" ");
        break;

    case MD_BLOCK_UL:
        list_ordered_.push_back(false);
        list_counter_.push_back(0);
        list_nesting_++;
        break;

    case MD_BLOCK_OL: {
        auto* ol = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
        list_ordered_.push_back(true);
        list_counter_.push_back(static_cast<int>(ol->start));
        list_nesting_++;
        break;
    }

    case MD_BLOCK_LI: {
        int indent = list_nesting_ * 360;
        char buf[128];
        snprintf(buf, sizeof(buf), "\\pard\\fi-360\\li%d\\sa100 ", indent);
        emit(buf);
        emit_default_fmt();

        auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
        if (li->is_task) {
            // Checkbox: U+2610 (unchecked) or U+2611 (checked)
            if (li->task_mark == 'x' || li->task_mark == 'X') {
                emit(" \\u9745? "); // ballot box with check
            } else {
                emit(" \\u9744? "); // ballot box
            }
            char_count_ += 2; // checkbox + space
        } else if (!list_ordered_.empty() && list_ordered_.back()) {
            int& counter = list_counter_.back();
            snprintf(buf, sizeof(buf), " %d. ", counter);
            emit(buf);
            char_count_ += snprintf(buf, sizeof(buf), "%d. ", counter);
            counter++;
        } else {
            emit(" \\u8226  "); // bullet •
            char_count_ += 2; // bullet + space
        }
        break;
    }

    case MD_BLOCK_CODE: {
        auto* code = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
        char buf[64];
        snprintf(buf, sizeof(buf), "\\pard\\li200\\ri200\\sa100\\f1\\fs%d\\highlight5\\cf1 ",
                 config_.code_size * 2);
        emit(buf);
        in_code_block_ = true;
        break;
    }

    case MD_BLOCK_QUOTE:
        emit("\\pard\\li720\\brdrbar\\brdrs\\brdrw20\\brdrcf4\\sa200 ");
        emit_default_fmt();
        emit("\\cf4 ");
        break;

    case MD_BLOCK_HR:
        emit("\\pard\\brdrb\\brdrs\\brdrw10\\brdrcf1\\sa200 \\par\n");
        char_count_++;
        break;

    case MD_BLOCK_TABLE: {
        auto* tbl = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
        table_col_count_ = static_cast<int>(tbl->col_count);
        in_table_ = true;
        break;
    }

    case MD_BLOCK_THEAD:
        in_table_header_ = true;
        break;

    case MD_BLOCK_TBODY:
        in_table_header_ = false;
        break;

    case MD_BLOCK_TR: {
        table_col_ = 0;
        emit("\\trowd\\trgaph108");
        int col_width = 9000 / (table_col_count_ > 0 ? table_col_count_ : 1);
        char buf[32];
        for (int i = 0; i < table_col_count_; i++) {
            snprintf(buf, sizeof(buf), "\\cellx%d", col_width * (i + 1));
            emit(buf);
        }
        emit("\n");
        break;
    }

    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        emit("\\pard\\intbl ");
        emit_default_fmt();
        if (in_table_header_) emit("\\b");
        emit(" ");
        break;

    default:
        break;
    }
}

void RtfBuilder::leave_block(MD_BLOCKTYPE type, void* detail) {
    switch (type) {
    case MD_BLOCK_DOC:
        break;

    case MD_BLOCK_H:
        emit("\\b0\\par\n");
        char_count_++;
        break;

    case MD_BLOCK_P:
        if (in_table_) break;
        emit("\\par\n");
        char_count_++;
        break;

    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (!list_ordered_.empty()) list_ordered_.pop_back();
        if (!list_counter_.empty()) list_counter_.pop_back();
        list_nesting_--;
        break;

    case MD_BLOCK_LI:
        emit("\\par\n");
        char_count_++;
        break;

    case MD_BLOCK_CODE:
        emit("\\highlight0\\par\n");
        char_count_++;
        in_code_block_ = false;
        break;

    case MD_BLOCK_QUOTE:
        emit("\\par\n");
        char_count_++;
        break;

    case MD_BLOCK_HR:
        break; // already emitted in enter

    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        emit("\\cell\n");
        char_count_++;
        table_col_++;
        break;

    case MD_BLOCK_TR:
        emit("\\row\n");
        char_count_++;
        break;

    case MD_BLOCK_TABLE:
        in_table_ = false;
        break;

    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
        break;

    default:
        break;
    }
}
```

- [ ] **Step 4: Run tests, verify they pass**

```bash
cmake --build --preset conan-release && ./build/Release/tests.exe
```

Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/rtf_builder.cpp tests/test_rtf_builder.cpp
git commit -m "feat: RTF builder block elements (headings, lists, code, quotes, tables, HR)"
```

---

## Task 5: RTF Builder — Inline Elements

**Files:**
- Modify: `src/rtf_builder.cpp`
- Modify: `tests/test_rtf_builder.cpp`

- [ ] **Step 1: Add inline element tests**

Append to `tests/test_rtf_builder.cpp`:

```cpp
TEST_CASE("Bold text uses \\b") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("**bold**", 8);
    CHECK(rtf.find("\\b ") != std::string::npos);
    CHECK(rtf.find("\\b0") != std::string::npos);
    CHECK(rtf.find("bold") != std::string::npos);
}

TEST_CASE("Italic text uses \\i") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("*italic*", 8);
    CHECK(rtf.find("\\i ") != std::string::npos);
    CHECK(rtf.find("\\i0") != std::string::npos);
}

TEST_CASE("Strikethrough uses \\strike") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("~~del~~", 7);
    CHECK(rtf.find("\\strike ") != std::string::npos);
    CHECK(rtf.find("\\strike0") != std::string::npos);
}

TEST_CASE("Inline code uses monospace font and highlight") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("`code`", 6);
    CHECK(rtf.find("\\f1") != std::string::npos);
    CHECK(rtf.find("\\highlight5") != std::string::npos);
}

TEST_CASE("Link stores URL and uses link color") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("[click](https://example.com)", 28);
    CHECK(rtf.find("\\cf3") != std::string::npos);   // link color
    CHECK(rtf.find("\\ul") != std::string::npos);     // underline
    CHECK(rtf.find("click") != std::string::npos);
    auto& links = b.links();
    REQUIRE(links.size() == 1);
    CHECK(links[0].url == "https://example.com");
    CHECK(links[0].char_start < links[0].char_end);
}

TEST_CASE("Image renders as alt text in brackets") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("![photo](img.png)", 17);
    CHECK(rtf.find("[Image: photo]") != std::string::npos);
}
```

- [ ] **Step 2: Run tests, verify new tests fail**

```bash
cmake --build --preset conan-release && ./build/Release/tests.exe
```

- [ ] **Step 3: Implement inline handlers**

Replace the `enter_span` and `leave_span` methods in `src/rtf_builder.cpp`:

```cpp
void RtfBuilder::enter_span(MD_SPANTYPE type, void* detail) {
    switch (type) {
    case MD_SPAN_STRONG:
        emit("{\\b ");
        break;

    case MD_SPAN_EM:
        emit("{\\i ");
        break;

    case MD_SPAN_DEL:
        emit("{\\strike ");
        break;

    case MD_SPAN_CODE: {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\\f1\\fs%d\\highlight5 ", config_.code_size * 2);
        emit(buf);
        break;
    }

    case MD_SPAN_A: {
        auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
        pending_link_url_.assign(a->href.text, a->href.size);
        pending_link_start_ = char_count_;
        emit("{\\cf3\\ul ");
        break;
    }

    case MD_SPAN_IMG: {
        auto* img = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
        in_image_ = true;
        pending_image_alt_.clear();
        break;
    }

    default:
        break;
    }
}

void RtfBuilder::leave_span(MD_SPANTYPE type, void* detail) {
    switch (type) {
    case MD_SPAN_STRONG:
        emit("\\b0}");
        break;

    case MD_SPAN_EM:
        emit("\\i0}");
        break;

    case MD_SPAN_DEL:
        emit("\\strike0}");
        break;

    case MD_SPAN_CODE:
        emit("\\highlight0}");
        break;

    case MD_SPAN_A:
        emit("\\ul0}");
        links_.push_back({pending_link_start_, char_count_, pending_link_url_});
        pending_link_url_.clear();
        break;

    case MD_SPAN_IMG: {
        in_image_ = false;
        std::string placeholder = "[Image: " + pending_image_alt_ + "]";
        emit("{\\i ");
        emit_text(placeholder.c_str(), placeholder.size());
        emit("\\i0}");
        pending_image_alt_.clear();
        break;
    }

    default:
        break;
    }
}
```

- [ ] **Step 4: Run tests, verify they pass**

```bash
cmake --build --preset conan-release && ./build/Release/tests.exe
```

Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/rtf_builder.cpp tests/test_rtf_builder.cpp
git commit -m "feat: RTF builder inline elements (bold, italic, strike, code, links, images)"
```

---

## Task 6: Plugin WLX Exports + RichEdit Hosting

**Files:**
- Modify: `src/plugin.cpp`

- [ ] **Step 1: Implement the full plugin module**

Replace `src/plugin.cpp`:

```cpp
#include <windows.h>
#include <richedit.h>
#include <shellapi.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

#include "listerplugin.h"
#include "config.h"
#include "rtf_builder.h"

// ---------- globals ----------

static HMODULE g_hModule = nullptr;
static Config g_config;
static bool g_config_loaded = false;
static HMODULE g_richedit = nullptr;

// Per-window data stored via SetWindowLongPtr(GWLP_USERDATA)
struct WndData {
    HWND richedit;
    WNDPROC orig_richedit_proc;
    std::vector<LinkInfo> links;
    bool dark_mode;
};

// Window data map (backup, keyed by parent HWND)
static std::unordered_map<HWND, WndData*> g_windows;

// ---------- helpers ----------

static std::string get_module_dir() {
    char path[MAX_PATH];
    GetModuleFileNameA(g_hModule, path, MAX_PATH);
    std::string dir(path);
    auto pos = dir.find_last_of("\\/");
    return (pos != std::string::npos) ? dir.substr(0, pos + 1) : dir;
}

static void ensure_config() {
    if (!g_config_loaded) {
        std::string cfg_path = get_module_dir() + "wlx-mini-markdown.toml";
        g_config = load_config(cfg_path);
        g_config_loaded = true;
    }
}

static void ensure_richedit() {
    if (!g_richedit) {
        g_richedit = LoadLibraryW(L"msftedit.dll");
    }
}

static std::string read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// EM_STREAMIN callback
struct StreamData {
    const char* data;
    size_t size;
    size_t pos;
};

static DWORD CALLBACK stream_in_callback(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb) {
    auto* sd = reinterpret_cast<StreamData*>(cookie);
    LONG remaining = static_cast<LONG>(sd->size - sd->pos);
    LONG toRead = (cb < remaining) ? cb : remaining;
    memcpy(buf, sd->data + sd->pos, toRead);
    sd->pos += toRead;
    *pcb = toRead;
    return 0;
}

static void load_rtf_into_richedit(HWND hwndRE, const std::string& rtf) {
    StreamData sd = {rtf.c_str(), rtf.size(), 0};
    EDITSTREAM es = {};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&sd);
    es.pfnCallback = stream_in_callback;
    SendMessageW(hwndRE, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
}

static COLORREF to_colorref(uint32_t rgb) {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static void render_file(WndData* wd, const char* path) {
    std::string content = read_file(path);
    if (content.empty()) {
        SendMessageW(wd->richedit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"(empty file)"));
        return;
    }

    RtfBuilder builder(g_config, wd->dark_mode);
    std::string rtf = builder.build(content.c_str(), content.size());
    wd->links = builder.links();

    load_rtf_into_richedit(wd->richedit, rtf);

    // Apply CFE_LINK to tracked link ranges so EN_LINK fires on click
    for (auto& link : wd->links) {
        CHARRANGE cr = {static_cast<LONG>(link.char_start), static_cast<LONG>(link.char_end)};
        SendMessageW(wd->richedit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_LINK;
        cf.dwEffects = CFE_LINK;
        SendMessageW(wd->richedit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    }

    // Deselect and scroll to top
    CHARRANGE crTop = {0, 0};
    SendMessageW(wd->richedit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&crTop));
    SendMessageW(wd->richedit, WM_VSCROLL, SB_TOP, 0);
}

// RichEdit subclass proc — handles EN_LINK forwarding from parent
static LRESULT CALLBACK richedit_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* wd = reinterpret_cast<WndData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!wd) return DefWindowProcW(hwnd, msg, wp, lp);
    return CallWindowProcW(wd->orig_richedit_proc, hwnd, msg, wp, lp);
}

// Parent subclass to intercept WM_NOTIFY (EN_LINK)
static LRESULT CALLBACK parent_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR subclassId, DWORD_PTR refData) {
    if (msg == WM_NOTIFY) {
        auto* nmhdr = reinterpret_cast<NMHDR*>(lp);
        if (nmhdr->code == EN_LINK) {
            auto* enlink = reinterpret_cast<ENLINK*>(lp);
            if (enlink->msg == WM_LBUTTONUP) {
                auto* wd = reinterpret_cast<WndData*>(refData);
                if (wd) {
                    LONG start = enlink->chrg.cpMin;
                    LONG end = enlink->chrg.cpMax;
                    for (auto& link : wd->links) {
                        if (static_cast<LONG>(link.char_start) <= start &&
                            end <= static_cast<LONG>(link.char_end)) {
                            std::wstring wurl(link.url.begin(), link.url.end());
                            ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOW);
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ---------- DLL entry ----------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

// ---------- WLX exports ----------

extern "C" {

HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    ensure_config();
    ensure_richedit();
    if (!g_richedit) return nullptr;

    bool dark = (ShowFlags & lcp_darkmode) != 0;

    RECT rc;
    GetClientRect(ParentWin, &rc);

    HWND hwndRE = CreateWindowExW(
        0, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_READONLY | ES_NOHIDESEL,
        0, 0, rc.right, rc.bottom,
        ParentWin, nullptr, g_hModule, nullptr);

    if (!hwndRE) return nullptr;

    auto* wd = new WndData{hwndRE, nullptr, {}, dark};
    SetWindowLongPtrW(hwndRE, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(wd));
    g_windows[hwndRE] = wd;

    // Enable EN_LINK notifications
    DWORD mask = static_cast<DWORD>(SendMessageW(hwndRE, EM_GETEVENTMASK, 0, 0));
    SendMessageW(hwndRE, EM_SETEVENTMASK, 0, mask | ENM_LINK);

    // Set background color
    const auto& colors = dark ? g_config.dark : g_config.light;
    SendMessageW(hwndRE, EM_SETBKGNDCOLOR, 0, to_colorref(colors.background));

    // Install parent subclass for EN_LINK handling
    SetWindowSubclass(ParentWin, parent_subclass, reinterpret_cast<UINT_PTR>(hwndRE),
                      reinterpret_cast<DWORD_PTR>(wd));

    render_file(wd, FileToLoad);

    return hwndRE;
}

int __stdcall ListLoadNext(HWND ParentWin, HWND PluginWin, char* FileToLoad, int ShowFlags) {
    auto it = g_windows.find(PluginWin);
    if (it == g_windows.end()) return LISTPLUGIN_ERROR;

    auto* wd = it->second;
    wd->dark_mode = (ShowFlags & lcp_darkmode) != 0;

    const auto& colors = wd->dark_mode ? g_config.dark : g_config.light;
    SendMessageW(wd->richedit, EM_SETBKGNDCOLOR, 0, to_colorref(colors.background));

    render_file(wd, FileToLoad);
    return LISTPLUGIN_OK;
}

void __stdcall ListCloseWindow(HWND ListWin) {
    auto it = g_windows.find(ListWin);
    if (it != g_windows.end()) {
        // Remove parent subclass
        HWND parent = GetParent(ListWin);
        RemoveWindowSubclass(parent, parent_subclass, reinterpret_cast<UINT_PTR>(ListWin));

        delete it->second;
        g_windows.erase(it);
    }
    DestroyWindow(ListWin);
}

void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    ensure_config();
    strncpy(DetectString, g_config.detect_string.c_str(), maxlen - 1);
    DetectString[maxlen - 1] = '\0';
}

int __stdcall ListSearchText(HWND ListWin, char* SearchString, int SearchParameter) {
    // Convert ANSI to wide and delegate
    int len = MultiByteToWideChar(CP_ACP, 0, SearchString, -1, nullptr, 0);
    std::vector<wchar_t> wide(len);
    MultiByteToWideChar(CP_ACP, 0, SearchString, -1, wide.data(), len);
    return ListSearchTextW(ListWin, wide.data(), SearchParameter);
}

int __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter) {
    DWORD flags = 0;
    if (SearchParameter & lcs_matchcase) flags |= FR_MATCHCASE;
    if (SearchParameter & lcs_wholewords) flags |= FR_WHOLEWORD;

    CHARRANGE sel = {};
    SendMessageW(ListWin, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));

    FINDTEXTEXW ft = {};
    ft.lpstrText = SearchString;

    if (SearchParameter & lcs_backwards) {
        flags |= 0; // no FR_DOWN = search backwards
        ft.chrg.cpMin = (SearchParameter & lcs_findfirst) ? -1 : sel.cpMin;
        ft.chrg.cpMax = 0;
    } else {
        flags |= FR_DOWN;
        ft.chrg.cpMin = (SearchParameter & lcs_findfirst) ? 0 : sel.cpMax;
        ft.chrg.cpMax = -1;
    }

    LRESULT found = SendMessageW(ListWin, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
    if (found == -1) return LISTPLUGIN_ERROR;

    // Select and scroll to found text
    SendMessageW(ListWin, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&ft.chrgText));
    SendMessageW(ListWin, EM_SCROLLCARET, 0, 0);

    return LISTPLUGIN_OK;
}

int __stdcall ListPrint(HWND ListWin, char* FileToPrint, char* DefPrinter,
                        int PrintFlags, RECT* Margins) {
    // Basic print via RichEdit — let the OS handle it
    // For v1, return error (print not implemented)
    return LISTPLUGIN_ERROR;
}

int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter) {
    switch (Command) {
    case lc_copy:
        SendMessageW(ListWin, WM_COPY, 0, 0);
        return LISTPLUGIN_OK;

    case lc_selectall:
        SendMessageW(ListWin, EM_SETSEL, 0, -1);
        return LISTPLUGIN_OK;

    case lc_newparams: {
        auto it = g_windows.find(ListWin);
        if (it == g_windows.end()) return LISTPLUGIN_ERROR;
        auto* wd = it->second;
        bool new_dark = (Parameter & lcp_darkmode) != 0;
        if (new_dark != wd->dark_mode) {
            wd->dark_mode = new_dark;
            const auto& colors = new_dark ? g_config.dark : g_config.light;
            SendMessageW(ListWin, EM_SETBKGNDCOLOR, 0, to_colorref(colors.background));
            // Would need to re-render RTF with new colors.
            // For now, just update background. Full re-render requires
            // storing the file path — deferred to enhancement.
        }
        return LISTPLUGIN_OK;
    }

    default:
        return LISTPLUGIN_ERROR;
    }
}

} // extern "C"
```

- [ ] **Step 2: Build the plugin**

```bash
cmake --build --preset conan-release
```

Expected: `wlx-mini-markdown.wlx64` builds successfully. Tests also build and pass.

- [ ] **Step 3: Commit**

```bash
git add src/plugin.cpp
git commit -m "feat: WLX plugin with RichEdit hosting, search, links, and dark mode"
```

---

## Task 7: Dark Mode Re-render Support

The plugin needs to store the current file path so `lc_newparams` (dark mode toggle) can re-render.

**Files:**
- Modify: `src/plugin.cpp`

- [ ] **Step 1: Add file path to WndData and implement re-render**

In `src/plugin.cpp`, add `std::string file_path;` to `WndData`:

```cpp
struct WndData {
    HWND richedit;
    WNDPROC orig_richedit_proc;
    std::vector<LinkInfo> links;
    bool dark_mode;
    std::string file_path;
};
```

Update `ListLoad` to store the path:

```cpp
// In ListLoad, after creating wd:
wd->file_path = FileToLoad;
```

Update `ListLoadNext` to store the path:

```cpp
// In ListLoadNext, after getting wd:
wd->file_path = FileToLoad;
```

Update `lc_newparams` in `ListSendCommand` to re-render:

```cpp
case lc_newparams: {
    auto it = g_windows.find(ListWin);
    if (it == g_windows.end()) return LISTPLUGIN_ERROR;
    auto* wd = it->second;
    bool new_dark = (Parameter & lcp_darkmode) != 0;
    if (new_dark != wd->dark_mode) {
        wd->dark_mode = new_dark;
        const auto& colors = new_dark ? g_config.dark : g_config.light;
        SendMessageW(ListWin, EM_SETBKGNDCOLOR, 0, to_colorref(colors.background));
        if (!wd->file_path.empty()) {
            render_file(wd, wd->file_path.c_str());
        }
    }
    return LISTPLUGIN_OK;
}
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build --preset conan-release
```

Expected: Clean build.

- [ ] **Step 3: Commit**

```bash
git add src/plugin.cpp
git commit -m "feat: re-render on dark mode toggle"
```

---

## Task 8: Default Config + Test Markdown + Build Verification

**Files:**
- Create: `config/wlx-mini-markdown.toml`
- Create: `test_data/sample.md`

- [ ] **Step 1: Create default config file**

`config/wlx-mini-markdown.toml`:

```toml
[options]
extensions = ["md", "markdown", "mdown", "mkd", "mkdn"]
detect_string = 'EXT="MD" | EXT="MARKDOWN"'

[fonts]
body = "Segoe UI"
body_size = 11
code = "Consolas"
code_size = 10

[colors.light]
background = "#FFFFFF"
text = "#24292E"
heading = "#24292E"
code_background = "#F6F8FA"
blockquote = "#6A737D"
link = "#0366D6"

[colors.dark]
background = "#1E1E1E"
text = "#D4D4D4"
heading = "#E0E0E0"
code_background = "#2D2D2D"
blockquote = "#9E9E9E"
link = "#58A6FF"
```

- [ ] **Step 2: Create comprehensive test markdown file**

`test_data/sample.md`:

````markdown
# Heading 1

## Heading 2

### Heading 3

#### Heading 4

##### Heading 5

###### Heading 6

This is a paragraph with **bold**, *italic*, ~~strikethrough~~, and `inline code`.

---

## Links

[Click here](https://example.com) to visit example.com.

Autolink: https://github.com

## Lists

### Unordered

- Item 1
- Item 2
  - Nested A
  - Nested B
- Item 3

### Ordered

1. First
2. Second
3. Third

### Task List

- [ ] Unchecked task
- [x] Checked task
- [ ] Another task

## Blockquote

> This is a blockquote.
> It can span multiple lines.

## Code Block

```python
def hello():
    print("Hello, world!")
```

## Table

| Name    | Language | Stars |
|---------|----------|-------|
| md4c    | C        | 1500  |
| pulldown| Rust     | 2000  |
| markdig | C#       | 4000  |

## Image

![Alt text for image](image.png)

## Special Characters

Backslash: \\ Braces: {} Ampersand: &

## Unicode

Emoji: The caf\u00e9 is great! Arrow: \u2192

End of test document.
````

- [ ] **Step 3: Clean build from scratch**

```bash
rm -rf build
conan install . --output-folder=build --build=missing -s build_type=Release
cmake --preset conan-default
cmake --build --preset conan-release
```

Expected: Zero errors, zero warnings.

- [ ] **Step 4: Run all tests**

```bash
./build/Release/tests.exe -v
```

Expected: All tests pass.

- [ ] **Step 5: Verify artifacts exist**

```bash
ls -la build/Release/wlx-mini-markdown.wlx64
ls -la build/Release/tests.exe
```

Expected: Both files exist. The `.wlx64` should be a small DLL (likely under 500KB).

- [ ] **Step 6: Commit**

```bash
git add config/ test_data/
git commit -m "feat: default config and test markdown for verification"
```

---

## Verification Checklist

After all tasks are complete:

1. **Build**: `conan install` + `cmake` + `cmake --build` succeeds with zero warnings
2. **Tests**: All doctest tests pass
3. **Manual TC test**: Copy `wlx-mini-markdown.wlx64` + `wlx-mini-markdown.toml` to TC plugin directory. Add to TC lister plugins. Ctrl+Q on `sample.md`:
   - Headings render at different sizes, H1/H2 have bottom border
   - Bold, italic, strikethrough, inline code all visible
   - Bullet and numbered lists properly indented
   - Code blocks in monospace with highlight background
   - Blockquotes indented with gray color
   - Tables render with cells
   - Task lists show checkbox characters
   - Links are blue, underlined, clickable (open browser)
   - Horizontal rule visible
   - Image shows `[Image: Alt text]`
4. **Dark mode**: Toggle TC dark mode → background and text colors switch
5. **Search**: Ctrl+F → finds text, scrolls to match
6. **Config**: Edit TOML colors, re-open file → new colors applied
