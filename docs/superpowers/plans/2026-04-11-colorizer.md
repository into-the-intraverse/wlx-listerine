# wlx-listerine-colorizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a tree-sitter-based syntax colorizer as a reusable static library (`colorizer-core`), a standalone WLX lister plugin (`wlx-listerine-colorizer`), and integrate it into the existing markdown renderer for code fence highlighting.

**Architecture:** `colorizer-core` is a static lib with no UI dependencies — it takes source text + language name and returns colored token spans. The standalone WLX plugin wraps it with D2D rendering. The md plugin links it to colorize fenced code blocks. Tree-sitter grammar DLLs are loaded on demand via `LoadLibrary()`.

**Tech Stack:** C++17, tree-sitter (Conan), toml++ (existing), Direct2D/DirectWrite (existing), doctest (existing)

---

### Task 1: Add tree-sitter dependency and `colorizer-core` CMake target

**Files:**
- Modify: `conanfile.txt`
- Modify: `CMakeLists.txt`
- Create: `src/colorizer/colorizer.h`
- Create: `src/colorizer/colorizer.cpp`

This task sets up the build infrastructure and the public API header with stub implementations so everything compiles.

- [ ] **Step 1: Add tree-sitter to conanfile.txt**

```
[requires]
md4c/0.5.2
tomlplusplus/3.4.0
doctest/2.4.11
tree-sitter/0.24.7

[generators]
CMakeToolchain
CMakeDeps

[options]
md4c/*:shared=False
```

Note: check `conan search tree-sitter` for the latest available version. Use whatever 0.24.x is available.

- [ ] **Step 2: Create the public API header `src/colorizer/colorizer.h`**

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

struct ColorSpan {
    uint32_t start = 0;   // byte offset in UTF-8 source
    uint32_t length = 0;
    uint32_t color = 0;   // 0x00RRGGBB
};

struct ColorizeResult {
    std::vector<ColorSpan> spans;  // sorted by start, non-overlapping
};

class GrammarRegistry;
class ThemeLoader;

class Colorizer {
public:
    Colorizer(const std::wstring& grammar_dir, const std::wstring& theme_dir);
    ~Colorizer();

    ColorizeResult colorize(const std::string& source,
                            const std::string& language,
                            bool dark_mode) const;

    bool supports(const std::string& language) const;
    std::vector<std::string> available_languages() const;

private:
    std::unique_ptr<GrammarRegistry> grammar_registry_;
    std::unique_ptr<ThemeLoader> theme_loader_;
};
```

- [ ] **Step 3: Create stub implementation `src/colorizer/colorizer.cpp`**

```cpp
#include "colorizer.h"

Colorizer::Colorizer(const std::wstring& grammar_dir, const std::wstring& theme_dir) {
    // Will be populated in subsequent tasks
}

Colorizer::~Colorizer() = default;

ColorizeResult Colorizer::colorize(const std::string& source,
                                   const std::string& language,
                                   bool dark_mode) const {
    return {};
}

bool Colorizer::supports(const std::string& language) const {
    return false;
}

std::vector<std::string> Colorizer::available_languages() const {
    return {};
}
```

- [ ] **Step 4: Add `colorizer-core` static library and `colorizer-tests` to CMakeLists.txt**

Add after the existing `wlx-core` target:

```cmake
# --- Colorizer core static library ---
find_package(tree-sitter REQUIRED)

add_library(colorizer-core STATIC
    src/colorizer/colorizer.cpp
)

target_include_directories(colorizer-core PUBLIC src/colorizer src)
target_link_libraries(colorizer-core
    PUBLIC
        tree-sitter::tree-sitter
    PRIVATE
        tomlplusplus::tomlplusplus
)
```

Add after the existing `tests` target:

```cmake
# --- Colorizer tests ---
add_executable(colorizer-tests
    tests/test_main.cpp
    tests/test_colorizer.cpp
)

target_link_libraries(colorizer-tests PRIVATE
    colorizer-core
    doctest::doctest
)
```

- [ ] **Step 5: Create minimal test file `tests/test_colorizer.cpp`**

```cpp
#include <doctest/doctest.h>
#include "colorizer.h"

TEST_CASE("colorizer stub returns empty result") {
    Colorizer c(L"nonexistent", L"nonexistent");
    auto result = c.colorize("int x = 1;", "c", false);
    CHECK(result.spans.empty());
}

TEST_CASE("colorizer stub supports returns false") {
    Colorizer c(L"nonexistent", L"nonexistent");
    CHECK_FALSE(c.supports("c"));
}
```

- [ ] **Step 6: Run conan install and build**

```bash
conan install . --output-folder=build --build=missing -s build_type=Release
cmake --preset conan-default
cmake --build --preset conan-release
```

Expected: builds successfully. If tree-sitter is not found on Conan, fall back to vendoring — download `tree_sitter/api.h` and `lib/src/*.c` from the tree-sitter GitHub release and add them as sources directly to the `colorizer-core` target instead of using `find_package`.

- [ ] **Step 7: Run colorizer tests**

```bash
./build/Release/colorizer-tests.exe
```

Expected: 2 tests pass.

- [ ] **Step 8: Run existing tests to verify no regressions**

```bash
./build/Release/tests.exe
```

Expected: all 86+ existing tests pass.

- [ ] **Step 9: Commit**

```bash
git add conanfile.txt CMakeLists.txt src/colorizer/colorizer.h src/colorizer/colorizer.cpp tests/test_colorizer.cpp
git commit -m "feat: add colorizer-core static lib with tree-sitter dependency and stub API"
```

---

### Task 2: Implement ThemeLoader

**Files:**
- Create: `src/colorizer/theme_loader.h`
- Create: `src/colorizer/theme_loader.cpp`
- Create: `config/themes/default.toml`
- Create: `tests/test_colorizer_theme.cpp`
- Modify: `CMakeLists.txt` (add source files)

ThemeLoader parses per-language TOML theme files containing light/dark scope-to-color mappings.

- [ ] **Step 1: Create `config/themes/default.toml`**

```toml
[light]
keyword = "#AF00DB"
keyword2 = "#0000FF"
function = "#795E26"
string = "#A31515"
number = "#098658"
comment = "#008000"
operator = "#000000"
type = "#267F99"
preprocessor = "#AF00DB"
namespace = "#267F99"
variable = "#001080"
punctuation = "#000000"
plain = "#1F2328"

[dark]
keyword = "#C586C0"
keyword2 = "#569CD6"
function = "#DCDCAA"
string = "#CE9178"
number = "#B5CEA8"
comment = "#6A9955"
operator = "#D4D4D4"
type = "#4EC9B0"
preprocessor = "#C586C0"
namespace = "#4EC9B0"
variable = "#9CDCFE"
punctuation = "#D4D4D4"
plain = "#D4D4D4"
```

- [ ] **Step 2: Write failing tests `tests/test_colorizer_theme.cpp`**

```cpp
#include <doctest/doctest.h>
#include "theme_loader.h"
#include <fstream>
#include <cstdio>

static std::string create_temp_theme(const std::string& content) {
    char path[L_tmpnam];
    std::tmpnam(path);
    std::string p = std::string(path) + ".toml";
    std::ofstream f(p);
    f << content;
    return p;
}

TEST_CASE("SyntaxPalette default has correct plain color") {
    SyntaxPalette pal = SyntaxPalette::defaults(false);
    CHECK(pal.plain == 0x1F2328);

    SyntaxPalette dark = SyntaxPalette::defaults(true);
    CHECK(dark.plain == 0xD4D4D4);
}

TEST_CASE("ThemeLoader loads default theme") {
    // Assumes config/themes/default.toml exists relative to working dir
    ThemeLoader loader(L"config/themes");
    auto pal = loader.palette_for("c", false);
    CHECK(pal.keyword == 0xAF00DB);
    CHECK(pal.comment == 0x008000);
    CHECK(pal.string == 0xA31515);
}

TEST_CASE("ThemeLoader dark mode returns dark palette") {
    ThemeLoader loader(L"config/themes");
    auto pal = loader.palette_for("c", true);
    CHECK(pal.keyword == 0xC586C0);
    CHECK(pal.comment == 0x6A9955);
}

TEST_CASE("ThemeLoader missing theme dir uses defaults") {
    ThemeLoader loader(L"nonexistent_dir");
    auto pal = loader.palette_for("c", false);
    CHECK(pal.plain == 0x1F2328);  // default light
}

TEST_CASE("ThemeLoader set_language_theme maps language to theme file") {
    ThemeLoader loader(L"config/themes");
    loader.set_language_theme("python", "default");
    auto pal = loader.palette_for("python", false);
    CHECK(pal.keyword == 0xAF00DB);
}
```

- [ ] **Step 3: Run tests to verify they fail**

```bash
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe
```

Expected: FAIL — `theme_loader.h` not found.

- [ ] **Step 4: Create `src/colorizer/theme_loader.h`**

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

struct SyntaxPalette {
    uint32_t keyword = 0;
    uint32_t keyword2 = 0;
    uint32_t function = 0;
    uint32_t string = 0;
    uint32_t number = 0;
    uint32_t comment = 0;
    uint32_t op = 0;       // "operator" is a C++ keyword
    uint32_t type = 0;
    uint32_t preprocessor = 0;
    uint32_t ns = 0;       // "namespace" is a C++ keyword
    uint32_t variable = 0;
    uint32_t punctuation = 0;
    uint32_t plain = 0;

    static SyntaxPalette defaults(bool dark_mode);
};

class ThemeLoader {
public:
    explicit ThemeLoader(const std::wstring& theme_dir);

    // Get palette for a language (falls back to "default" theme)
    SyntaxPalette palette_for(const std::string& language, bool dark_mode) const;

    // Map a language name to a theme file name (without .toml extension)
    void set_language_theme(const std::string& language, const std::string& theme_name);

private:
    void load_theme_file(const std::string& theme_name) const;
    SyntaxPalette read_palette_section(const std::string& theme_name, bool dark_mode) const;

    std::wstring theme_dir_;
    std::unordered_map<std::string, std::string> language_to_theme_;  // language -> theme name

    struct ThemeData {
        SyntaxPalette light;
        SyntaxPalette dark;
    };
    mutable std::unordered_map<std::string, ThemeData> loaded_themes_;  // theme name -> data
};
```

- [ ] **Step 5: Create `src/colorizer/theme_loader.cpp`**

```cpp
#include "theme_loader.h"
#include <toml++/toml.hpp>
#include <windows.h>
#include <filesystem>

namespace fs = std::filesystem;

// --- UTF conversion (same pattern as theme_service.cpp) ---

static std::string wstring_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
                        out.data(), len, nullptr, nullptr);
    return out;
}

// --- hex color parsing (reuse ThemeService::parse_hex_color pattern) ---

static uint32_t parse_hex(const std::string& hex, uint32_t fallback = 0) {
    try {
        std::string h = hex;
        if (!h.empty() && h[0] == '#') h = h.substr(1);
        if (h.empty()) return fallback;
        return static_cast<uint32_t>(std::stoul(h, nullptr, 16));
    } catch (...) {
        return fallback;
    }
}

// --- SyntaxPalette ---

SyntaxPalette SyntaxPalette::defaults(bool dark_mode) {
    SyntaxPalette pal;
    if (dark_mode) {
        pal.keyword      = 0xC586C0;
        pal.keyword2     = 0x569CD6;
        pal.function     = 0xDCDCAA;
        pal.string       = 0xCE9178;
        pal.number       = 0xB5CEA8;
        pal.comment      = 0x6A9955;
        pal.op           = 0xD4D4D4;
        pal.type         = 0x4EC9B0;
        pal.preprocessor = 0xC586C0;
        pal.ns           = 0x4EC9B0;
        pal.variable     = 0x9CDCFE;
        pal.punctuation  = 0xD4D4D4;
        pal.plain        = 0xD4D4D4;
    } else {
        pal.keyword      = 0xAF00DB;
        pal.keyword2     = 0x0000FF;
        pal.function     = 0x795E26;
        pal.string       = 0xA31515;
        pal.number       = 0x098658;
        pal.comment      = 0x008000;
        pal.op           = 0x000000;
        pal.type         = 0x267F99;
        pal.preprocessor = 0xAF00DB;
        pal.ns           = 0x267F99;
        pal.variable     = 0x001080;
        pal.punctuation  = 0x000000;
        pal.plain        = 0x1F2328;
    }
    return pal;
}

// --- ThemeLoader ---

ThemeLoader::ThemeLoader(const std::wstring& theme_dir)
    : theme_dir_(theme_dir) {}

void ThemeLoader::set_language_theme(const std::string& language, const std::string& theme_name) {
    language_to_theme_[language] = theme_name;
}

SyntaxPalette ThemeLoader::palette_for(const std::string& language, bool dark_mode) const {
    // Determine which theme file to use
    std::string theme_name = "default";
    auto it = language_to_theme_.find(language);
    if (it != language_to_theme_.end())
        theme_name = it->second;

    // Load theme if not cached
    if (loaded_themes_.find(theme_name) == loaded_themes_.end())
        load_theme_file(theme_name);

    auto dit = loaded_themes_.find(theme_name);
    if (dit != loaded_themes_.end())
        return dark_mode ? dit->second.dark : dit->second.light;

    return SyntaxPalette::defaults(dark_mode);
}

void ThemeLoader::load_theme_file(const std::string& theme_name) const {
    std::wstring file_path = theme_dir_ + L"/" +
        std::wstring(theme_name.begin(), theme_name.end()) + L".toml";

    std::string utf8_path = wstring_to_utf8(file_path);

    try {
        auto tbl = toml::parse_file(utf8_path);

        ThemeData data;
        data.light = SyntaxPalette::defaults(false);
        data.dark = SyntaxPalette::defaults(true);

        auto read_section = [&](const char* section, SyntaxPalette& pal) {
            if (auto t = tbl[section].as_table()) {
                auto read = [&](const char* key, uint32_t& out) {
                    if (auto v = (*t)[key].value<std::string>())
                        out = parse_hex(*v, out);
                };
                read("keyword",      pal.keyword);
                read("keyword2",     pal.keyword2);
                read("function",     pal.function);
                read("string",       pal.string);
                read("number",       pal.number);
                read("comment",      pal.comment);
                read("operator",     pal.op);
                read("type",         pal.type);
                read("preprocessor", pal.preprocessor);
                read("namespace",    pal.ns);
                read("variable",     pal.variable);
                read("punctuation",  pal.punctuation);
                read("plain",        pal.plain);
            }
        };

        read_section("light", data.light);
        read_section("dark", data.dark);

        loaded_themes_[theme_name] = data;
    } catch (...) {
        // File missing or parse error — defaults already used by caller
    }
}
```

- [ ] **Step 6: Add theme_loader sources to CMakeLists.txt**

Update `colorizer-core` sources:

```cmake
add_library(colorizer-core STATIC
    src/colorizer/colorizer.cpp
    src/colorizer/theme_loader.cpp
)
```

Update `colorizer-tests` sources:

```cmake
add_executable(colorizer-tests
    tests/test_main.cpp
    tests/test_colorizer.cpp
    tests/test_colorizer_theme.cpp
)
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe
```

Expected: all tests pass (stub tests + theme tests).

- [ ] **Step 8: Commit**

```bash
git add src/colorizer/theme_loader.h src/colorizer/theme_loader.cpp config/themes/default.toml tests/test_colorizer_theme.cpp CMakeLists.txt
git commit -m "feat: add ThemeLoader with TOML syntax theme parsing (light/dark)"
```

---

### Task 3: Implement GrammarRegistry

**Files:**
- Create: `src/colorizer/grammar_registry.h`
- Create: `src/colorizer/grammar_registry.cpp`
- Create: `tests/test_colorizer_grammar.cpp`
- Modify: `CMakeLists.txt` (add source files)

GrammarRegistry scans a directory for `tree-sitter-{lang}.dll` files, indexes them, and lazy-loads grammars via `LoadLibrary()` + `GetProcAddress()`.

- [ ] **Step 1: Write failing tests `tests/test_colorizer_grammar.cpp`**

```cpp
#include <doctest/doctest.h>
#include "grammar_registry.h"

TEST_CASE("GrammarRegistry with nonexistent dir has no languages") {
    GrammarRegistry reg(L"nonexistent_grammar_dir");
    CHECK(reg.available_languages().empty());
    CHECK_FALSE(reg.supports("c"));
}

TEST_CASE("GrammarRegistry get_grammar for unsupported language returns nullptr") {
    GrammarRegistry reg(L"nonexistent_grammar_dir");
    CHECK(reg.get_grammar("c") == nullptr);
}
```

Note: Tests with actual grammar DLLs will be added later once we have compiled grammars. These tests verify the "not found" paths.

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe
```

Expected: FAIL — `grammar_registry.h` not found.

- [ ] **Step 3: Create `src/colorizer/grammar_registry.h`**

```cpp
#pragma once

#include <tree_sitter/api.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

class GrammarRegistry {
public:
    explicit GrammarRegistry(const std::wstring& grammar_dir);
    ~GrammarRegistry();

    // Non-copyable, non-movable (holds HMODULE handles)
    GrammarRegistry(const GrammarRegistry&) = delete;
    GrammarRegistry& operator=(const GrammarRegistry&) = delete;

    bool supports(const std::string& language) const;
    std::vector<std::string> available_languages() const;

    // Returns nullptr if language not available or DLL fails to load.
    // Returned pointer is valid for the lifetime of this GrammarRegistry.
    const TSLanguage* get_grammar(const std::string& language);

private:
    void scan_directory();

    std::wstring grammar_dir_;

    struct GrammarEntry {
        std::wstring dll_path;
        HMODULE handle = nullptr;
        const TSLanguage* language = nullptr;
        bool load_attempted = false;
    };

    std::unordered_map<std::string, GrammarEntry> grammars_;  // lang name -> entry
};
```

- [ ] **Step 4: Create `src/colorizer/grammar_registry.cpp`**

```cpp
#include "grammar_registry.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// tree-sitter grammar DLLs export a function: const TSLanguage* tree_sitter_{lang}(void);
// The function name uses underscores for hyphens (e.g., tree_sitter_c_sharp).
using GrammarFn = const TSLanguage* (*)();

GrammarRegistry::GrammarRegistry(const std::wstring& grammar_dir)
    : grammar_dir_(grammar_dir) {
    scan_directory();
}

GrammarRegistry::~GrammarRegistry() {
    // FreeLibrary for all loaded grammars
    for (auto& [name, entry] : grammars_) {
        if (entry.handle)
            FreeLibrary(entry.handle);
    }
}

void GrammarRegistry::scan_directory() {
    std::error_code ec;
    if (!fs::is_directory(grammar_dir_, ec))
        return;

    for (auto& entry : fs::directory_iterator(grammar_dir_, ec)) {
        if (!entry.is_regular_file()) continue;

        auto filename = entry.path().filename().string();
        // Expected format: tree-sitter-{lang}.dll
        if (filename.size() < 16) continue;  // "tree-sitter-X.dll" minimum
        if (filename.substr(0, 12) != "tree-sitter-") continue;
        if (filename.substr(filename.size() - 4) != ".dll") continue;

        std::string lang = filename.substr(12, filename.size() - 16);
        GrammarEntry ge;
        ge.dll_path = entry.path().wstring();
        grammars_[lang] = ge;
    }
}

bool GrammarRegistry::supports(const std::string& language) const {
    return grammars_.find(language) != grammars_.end();
}

std::vector<std::string> GrammarRegistry::available_languages() const {
    std::vector<std::string> result;
    result.reserve(grammars_.size());
    for (auto& [name, entry] : grammars_)
        result.push_back(name);
    std::sort(result.begin(), result.end());
    return result;
}

const TSLanguage* GrammarRegistry::get_grammar(const std::string& language) {
    auto it = grammars_.find(language);
    if (it == grammars_.end()) return nullptr;

    auto& entry = it->second;
    if (entry.language) return entry.language;
    if (entry.load_attempted) return nullptr;  // already tried, failed

    entry.load_attempted = true;
    entry.handle = LoadLibraryW(entry.dll_path.c_str());
    if (!entry.handle) return nullptr;

    // Function name: tree_sitter_{lang} with hyphens replaced by underscores
    std::string fn_name = "tree_sitter_" + language;
    std::replace(fn_name.begin(), fn_name.end(), '-', '_');

    auto fn = reinterpret_cast<GrammarFn>(GetProcAddress(entry.handle, fn_name.c_str()));
    if (!fn) {
        FreeLibrary(entry.handle);
        entry.handle = nullptr;
        return nullptr;
    }

    entry.language = fn();
    return entry.language;
}
```

- [ ] **Step 5: Add grammar_registry sources to CMakeLists.txt**

Update `colorizer-core`:

```cmake
add_library(colorizer-core STATIC
    src/colorizer/colorizer.cpp
    src/colorizer/theme_loader.cpp
    src/colorizer/grammar_registry.cpp
)
```

Update `colorizer-tests`:

```cmake
add_executable(colorizer-tests
    tests/test_main.cpp
    tests/test_colorizer.cpp
    tests/test_colorizer_theme.cpp
    tests/test_colorizer_grammar.cpp
)
```

- [ ] **Step 6: Build and run tests**

```bash
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/colorizer/grammar_registry.h src/colorizer/grammar_registry.cpp tests/test_colorizer_grammar.cpp CMakeLists.txt
git commit -m "feat: add GrammarRegistry with DLL discovery and lazy loading"
```

---

### Task 4: Implement ScopeMapper

**Files:**
- Create: `src/colorizer/scope_mapper.h`
- Create: `src/colorizer/scope_mapper.cpp`
- Create: `tests/test_colorizer_scope.cpp`
- Modify: `CMakeLists.txt`

ScopeMapper maps tree-sitter node type names to semantic scope names. Each grammar has its own lookup table.

- [ ] **Step 1: Write failing tests `tests/test_colorizer_scope.cpp`**

```cpp
#include <doctest/doctest.h>
#include "scope_mapper.h"

TEST_CASE("ScopeMapper maps C comment to comment scope") {
    CHECK(ScopeMapper::map("c", "comment") == Scope::Comment);
}

TEST_CASE("ScopeMapper maps C string_literal to string scope") {
    CHECK(ScopeMapper::map("c", "string_literal") == Scope::String);
}

TEST_CASE("ScopeMapper maps C preproc_include to preprocessor scope") {
    CHECK(ScopeMapper::map("c", "preproc_include") == Scope::Preprocessor);
}

TEST_CASE("ScopeMapper maps C identifier to plain") {
    CHECK(ScopeMapper::map("c", "identifier") == Scope::Variable);
}

TEST_CASE("ScopeMapper unknown node returns plain") {
    CHECK(ScopeMapper::map("c", "totally_unknown_node_xyz") == Scope::Plain);
}

TEST_CASE("ScopeMapper unknown language returns plain") {
    CHECK(ScopeMapper::map("unknown_lang", "comment") == Scope::Plain);
}

TEST_CASE("ScopeMapper maps JSON string to string scope") {
    CHECK(ScopeMapper::map("json", "string") == Scope::String);
}

TEST_CASE("ScopeMapper maps Python def to keyword") {
    CHECK(ScopeMapper::map("python", "def") == Scope::Keyword);
}

TEST_CASE("scope_to_color returns correct color for keyword in light mode") {
    SyntaxPalette pal = SyntaxPalette::defaults(false);
    CHECK(scope_to_color(Scope::Keyword, pal) == pal.keyword);
}

TEST_CASE("scope_to_color returns correct color for comment in dark mode") {
    SyntaxPalette pal = SyntaxPalette::defaults(true);
    CHECK(scope_to_color(Scope::Comment, pal) == pal.comment);
}
```

- [ ] **Step 2: Create `src/colorizer/scope_mapper.h`**

```cpp
#pragma once

#include "theme_loader.h"
#include <string>

enum class Scope {
    Keyword,
    Keyword2,
    Function,
    String,
    Number,
    Comment,
    Operator,
    Type,
    Preprocessor,
    Namespace,
    Variable,
    Punctuation,
    Plain
};

class ScopeMapper {
public:
    // Map a tree-sitter node type to a semantic scope for a given language.
    // Falls back to a generic mapping if no language-specific one exists.
    static Scope map(const std::string& language, const std::string& node_type);
};

// Resolve a scope to a color using a SyntaxPalette.
uint32_t scope_to_color(Scope scope, const SyntaxPalette& palette);
```

- [ ] **Step 3: Create `src/colorizer/scope_mapper.cpp`**

```cpp
#include "scope_mapper.h"
#include <unordered_map>
#include <unordered_set>

// --- Generic scope mappings (shared across languages) ---

static const std::unordered_map<std::string, Scope> g_generic_map = {
    // Comments
    {"comment", Scope::Comment},
    {"line_comment", Scope::Comment},
    {"block_comment", Scope::Comment},

    // Strings
    {"string", Scope::String},
    {"string_literal", Scope::String},
    {"string_content", Scope::String},
    {"char_literal", Scope::String},
    {"raw_string_literal", Scope::String},
    {"interpreted_string_literal", Scope::String},
    {"template_string", Scope::String},
    {"heredoc_body", Scope::String},
    {"string_fragment", Scope::String},

    // Numbers
    {"number", Scope::Number},
    {"number_literal", Scope::Number},
    {"integer", Scope::Number},
    {"integer_literal", Scope::Number},
    {"float", Scope::Number},
    {"float_literal", Scope::Number},

    // Types
    {"type_identifier", Scope::Type},
    {"primitive_type", Scope::Type},
    {"sized_type_specifier", Scope::Type},
    {"type_specifier", Scope::Type},

    // Functions
    {"function_declarator", Scope::Function},
    {"call_expression", Scope::Function},

    // Preprocessor
    {"preproc_include", Scope::Preprocessor},
    {"preproc_def", Scope::Preprocessor},
    {"preproc_ifdef", Scope::Preprocessor},
    {"preproc_directive", Scope::Preprocessor},
    {"system_lib_string", Scope::String},

    // Identifiers
    {"identifier", Scope::Variable},
    {"field_identifier", Scope::Variable},

    // Namespace
    {"namespace_identifier", Scope::Namespace},

    // Punctuation
    {"{", Scope::Punctuation},
    {"}", Scope::Punctuation},
    {"(", Scope::Punctuation},
    {")", Scope::Punctuation},
    {"[", Scope::Punctuation},
    {"]", Scope::Punctuation},
    {";", Scope::Punctuation},
    {",", Scope::Punctuation},
    {".", Scope::Punctuation},
};

// --- Keywords per language ---

static const std::unordered_map<std::string, std::unordered_set<std::string>> g_keywords = {
    {"c", {"if", "else", "for", "while", "do", "switch", "case", "break",
           "continue", "return", "goto", "default", "typedef", "struct",
           "union", "enum", "sizeof", "static", "extern", "const",
           "volatile", "register", "inline", "auto"}},
    {"cpp", {"if", "else", "for", "while", "do", "switch", "case", "break",
             "continue", "return", "goto", "default", "typedef", "struct",
             "union", "enum", "sizeof", "static", "extern", "const",
             "volatile", "register", "inline", "auto", "class", "public",
             "private", "protected", "virtual", "override", "final",
             "template", "typename", "namespace", "using", "new", "delete",
             "try", "catch", "throw", "noexcept", "constexpr", "nullptr",
             "this", "operator", "friend", "mutable", "explicit",
             "static_cast", "dynamic_cast", "reinterpret_cast", "const_cast",
             "decltype", "co_await", "co_return", "co_yield", "concept",
             "requires", "consteval", "constinit", "module", "import", "export"}},
    {"python", {"def", "class", "if", "elif", "else", "for", "while",
                "return", "import", "from", "as", "with", "try", "except",
                "finally", "raise", "pass", "break", "continue", "yield",
                "lambda", "global", "nonlocal", "assert", "del", "in",
                "not", "and", "or", "is", "async", "await", "match", "case"}},
    {"javascript", {"function", "var", "let", "const", "if", "else", "for",
                     "while", "do", "switch", "case", "break", "continue",
                     "return", "throw", "try", "catch", "finally", "new",
                     "delete", "typeof", "instanceof", "in", "of", "class",
                     "extends", "super", "this", "import", "export", "default",
                     "from", "as", "async", "await", "yield", "static",
                     "get", "set"}},
    {"typescript", {"function", "var", "let", "const", "if", "else", "for",
                     "while", "do", "switch", "case", "break", "continue",
                     "return", "throw", "try", "catch", "finally", "new",
                     "delete", "typeof", "instanceof", "in", "of", "class",
                     "extends", "super", "this", "import", "export", "default",
                     "from", "as", "async", "await", "yield", "static",
                     "get", "set", "type", "interface", "enum", "implements",
                     "declare", "abstract", "readonly", "keyof", "infer",
                     "satisfies"}},
    {"json", {}},
    {"toml", {}},
};

// --- Type keywords per language ---

static const std::unordered_map<std::string, std::unordered_set<std::string>> g_type_keywords = {
    {"c", {"int", "char", "float", "double", "void", "long", "short",
           "unsigned", "signed", "bool", "size_t", "uint8_t", "uint16_t",
           "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
           "wchar_t", "ptrdiff_t", "intptr_t", "uintptr_t"}},
    {"cpp", {"int", "char", "float", "double", "void", "long", "short",
             "unsigned", "signed", "bool", "size_t", "string", "wstring",
             "true", "false", "std"}},
    {"python", {"True", "False", "None", "int", "str", "float", "list",
                "dict", "set", "tuple", "bool", "bytes", "type", "object",
                "self", "cls"}},
    {"javascript", {"true", "false", "null", "undefined", "NaN", "Infinity"}},
    {"typescript", {"true", "false", "null", "undefined", "NaN", "Infinity",
                     "string", "number", "boolean", "any", "void", "never",
                     "unknown", "object", "symbol", "bigint"}},
};

Scope ScopeMapper::map(const std::string& language, const std::string& node_type) {
    // Check language-specific keyword tables first
    auto kw_it = g_keywords.find(language);
    if (kw_it != g_keywords.end() && kw_it->second.count(node_type))
        return Scope::Keyword;

    auto ty_it = g_type_keywords.find(language);
    if (ty_it != g_type_keywords.end() && ty_it->second.count(node_type))
        return Scope::Keyword2;

    // Check generic mapping
    auto gen_it = g_generic_map.find(node_type);
    if (gen_it != g_generic_map.end())
        return gen_it->second;

    return Scope::Plain;
}

uint32_t scope_to_color(Scope scope, const SyntaxPalette& palette) {
    switch (scope) {
        case Scope::Keyword:      return palette.keyword;
        case Scope::Keyword2:     return palette.keyword2;
        case Scope::Function:     return palette.function;
        case Scope::String:       return palette.string;
        case Scope::Number:       return palette.number;
        case Scope::Comment:      return palette.comment;
        case Scope::Operator:     return palette.op;
        case Scope::Type:         return palette.type;
        case Scope::Preprocessor: return palette.preprocessor;
        case Scope::Namespace:    return palette.ns;
        case Scope::Variable:     return palette.variable;
        case Scope::Punctuation:  return palette.punctuation;
        case Scope::Plain:        return palette.plain;
    }
    return palette.plain;
}
```

Note: The keyword/type tables above cover C, C++, Python, JS, TS. Additional languages (Rust, Go, Java, C#, etc.) will need their own entries added. The generic map handles the common tree-sitter node types shared across grammars. Expand these tables for each shipped grammar as you integrate them.

- [ ] **Step 4: Add scope_mapper sources to CMakeLists.txt**

Update `colorizer-core`:

```cmake
add_library(colorizer-core STATIC
    src/colorizer/colorizer.cpp
    src/colorizer/theme_loader.cpp
    src/colorizer/grammar_registry.cpp
    src/colorizer/scope_mapper.cpp
)
```

Update `colorizer-tests`:

```cmake
add_executable(colorizer-tests
    tests/test_main.cpp
    tests/test_colorizer.cpp
    tests/test_colorizer_theme.cpp
    tests/test_colorizer_grammar.cpp
    tests/test_colorizer_scope.cpp
)
```

- [ ] **Step 5: Build and run tests**

```bash
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/colorizer/scope_mapper.h src/colorizer/scope_mapper.cpp tests/test_colorizer_scope.cpp CMakeLists.txt
git commit -m "feat: add ScopeMapper with per-language node-type-to-scope tables"
```

---

### Task 5: Implement Tokenizer

**Files:**
- Create: `src/colorizer/tokenizer.h`
- Create: `src/colorizer/tokenizer.cpp`
- Create: `tests/test_colorizer_tokenizer.cpp`
- Modify: `CMakeLists.txt`

The Tokenizer parses source with tree-sitter, walks the syntax tree, and emits `(node_type, byte_offset, byte_length)` tuples. It does NOT resolve colors — that's the Colorizer's job (combining Tokenizer + ScopeMapper + ThemeLoader).

- [ ] **Step 1: Write failing tests `tests/test_colorizer_tokenizer.cpp`**

```cpp
#include <doctest/doctest.h>
#include "tokenizer.h"

TEST_CASE("TokenSpan default construction") {
    TokenSpan span;
    CHECK(span.start == 0);
    CHECK(span.length == 0);
    CHECK(span.node_type.empty());
}

// Integration tests with real grammars will be added when grammar DLLs are available.
// For now, test the Tokenizer API with a null grammar (should return empty).
TEST_CASE("Tokenizer returns empty for null grammar") {
    auto spans = Tokenizer::tokenize(nullptr, "int x = 1;");
    CHECK(spans.empty());
}
```

- [ ] **Step 2: Create `src/colorizer/tokenizer.h`**

```cpp
#pragma once

#include <tree_sitter/api.h>
#include <string>
#include <vector>

struct TokenSpan {
    uint32_t start = 0;   // byte offset
    uint32_t length = 0;
    std::string node_type;
};

class Tokenizer {
public:
    // Parse source with the given grammar and return token spans.
    // Walks leaf nodes of the syntax tree.
    // Returns empty vector if grammar is null or parsing fails.
    static std::vector<TokenSpan> tokenize(const TSLanguage* grammar,
                                            const std::string& source);
};
```

- [ ] **Step 3: Create `src/colorizer/tokenizer.cpp`**

```cpp
#include "tokenizer.h"
#include <algorithm>

static void collect_leaves(TSNode node, const std::string& source,
                           std::vector<TokenSpan>& out) {
    uint32_t child_count = ts_node_child_count(node);

    if (child_count == 0) {
        // Leaf node — emit a token
        TokenSpan span;
        span.start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        span.length = end - span.start;
        span.node_type = ts_node_type(node);
        if (span.length > 0)
            out.push_back(span);
        return;
    }

    // Named nodes with only anonymous children: emit the parent's type
    // for the full range (e.g., string_literal that contains `"`, content, `"`)
    bool all_anonymous = true;
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_is_named(child)) {
            all_anonymous = false;
            break;
        }
    }

    if (ts_node_is_named(node) && all_anonymous) {
        // Emit parent type for full range
        TokenSpan span;
        span.start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        span.length = end - span.start;
        span.node_type = ts_node_type(node);
        if (span.length > 0)
            out.push_back(span);
        return;
    }

    // Recurse into children
    for (uint32_t i = 0; i < child_count; i++) {
        collect_leaves(ts_node_child(node, i), source, out);
    }
}

std::vector<TokenSpan> Tokenizer::tokenize(const TSLanguage* grammar,
                                             const std::string& source) {
    if (!grammar || source.empty())
        return {};

    TSParser* parser = ts_parser_new();
    if (!parser) return {};

    if (!ts_parser_set_language(parser, grammar)) {
        ts_parser_delete(parser);
        return {};
    }

    TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                           source.c_str(),
                                           static_cast<uint32_t>(source.size()));
    if (!tree) {
        ts_parser_delete(parser);
        return {};
    }

    TSNode root = ts_tree_root_node(tree);
    std::vector<TokenSpan> spans;
    collect_leaves(root, source, spans);

    // Sort by start offset (should already be in order, but ensure)
    std::sort(spans.begin(), spans.end(),
              [](const TokenSpan& a, const TokenSpan& b) {
                  return a.start < b.start;
              });

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return spans;
}
```

- [ ] **Step 4: Add tokenizer sources to CMakeLists.txt**

Update `colorizer-core`:

```cmake
add_library(colorizer-core STATIC
    src/colorizer/colorizer.cpp
    src/colorizer/theme_loader.cpp
    src/colorizer/grammar_registry.cpp
    src/colorizer/scope_mapper.cpp
    src/colorizer/tokenizer.cpp
)
```

Update `colorizer-tests`:

```cmake
add_executable(colorizer-tests
    tests/test_main.cpp
    tests/test_colorizer.cpp
    tests/test_colorizer_theme.cpp
    tests/test_colorizer_grammar.cpp
    tests/test_colorizer_scope.cpp
    tests/test_colorizer_tokenizer.cpp
)
```

- [ ] **Step 5: Build and run tests**

```bash
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/colorizer/tokenizer.h src/colorizer/tokenizer.cpp tests/test_colorizer_tokenizer.cpp CMakeLists.txt
git commit -m "feat: add Tokenizer with tree-sitter parse + leaf-node collection"
```

---

### Task 6: Wire up Colorizer (integrate all components)

**Files:**
- Modify: `src/colorizer/colorizer.h`
- Modify: `src/colorizer/colorizer.cpp`
- Modify: `tests/test_colorizer.cpp`

Replace the stub Colorizer with real implementation that wires GrammarRegistry + Tokenizer + ScopeMapper + ThemeLoader.

- [ ] **Step 1: Update `src/colorizer/colorizer.h`**

Add the includes for the components (the public API shape stays the same):

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

struct ColorSpan {
    uint32_t start = 0;
    uint32_t length = 0;
    uint32_t color = 0;
};

struct ColorizeResult {
    std::vector<ColorSpan> spans;
};

class GrammarRegistry;
class ThemeLoader;

class Colorizer {
public:
    Colorizer(const std::wstring& grammar_dir, const std::wstring& theme_dir);
    ~Colorizer();

    ColorizeResult colorize(const std::string& source,
                            const std::string& language,
                            bool dark_mode) const;

    bool supports(const std::string& language) const;
    std::vector<std::string> available_languages() const;

    // Configure language-to-theme mapping
    void set_language_theme(const std::string& language, const std::string& theme_name);

    // Access to theme loader (for md plugin integration)
    ThemeLoader& theme_loader() { return *theme_loader_; }

private:
    std::unique_ptr<GrammarRegistry> grammar_registry_;
    std::unique_ptr<ThemeLoader> theme_loader_;
};
```

- [ ] **Step 2: Update `src/colorizer/colorizer.cpp`**

```cpp
#include "colorizer.h"
#include "grammar_registry.h"
#include "tokenizer.h"
#include "scope_mapper.h"
#include "theme_loader.h"

Colorizer::Colorizer(const std::wstring& grammar_dir, const std::wstring& theme_dir)
    : grammar_registry_(std::make_unique<GrammarRegistry>(grammar_dir))
    , theme_loader_(std::make_unique<ThemeLoader>(theme_dir)) {}

Colorizer::~Colorizer() = default;

bool Colorizer::supports(const std::string& language) const {
    return grammar_registry_->supports(language);
}

std::vector<std::string> Colorizer::available_languages() const {
    return grammar_registry_->available_languages();
}

void Colorizer::set_language_theme(const std::string& language, const std::string& theme_name) {
    theme_loader_->set_language_theme(language, theme_name);
}

ColorizeResult Colorizer::colorize(const std::string& source,
                                   const std::string& language,
                                   bool dark_mode) const {
    ColorizeResult result;

    // Get grammar (lazy-loads DLL)
    auto* grammar = const_cast<GrammarRegistry*>(grammar_registry_.get())->get_grammar(language);
    if (!grammar) return result;

    // Tokenize
    auto token_spans = Tokenizer::tokenize(grammar, source);

    // Get theme palette
    auto palette = theme_loader_->palette_for(language, dark_mode);

    // Map tokens to colored spans
    result.spans.reserve(token_spans.size());
    for (auto& ts : token_spans) {
        Scope scope = ScopeMapper::map(language, ts.node_type);
        uint32_t color = scope_to_color(scope, palette);

        ColorSpan cs;
        cs.start = ts.start;
        cs.length = ts.length;
        cs.color = color;
        result.spans.push_back(cs);
    }

    return result;
}
```

- [ ] **Step 3: Update `tests/test_colorizer.cpp`**

```cpp
#include <doctest/doctest.h>
#include "colorizer.h"

TEST_CASE("Colorizer with no grammar dir returns empty result") {
    Colorizer c(L"nonexistent", L"nonexistent");
    auto result = c.colorize("int x = 1;", "c", false);
    CHECK(result.spans.empty());
}

TEST_CASE("Colorizer with no grammar dir supports returns false") {
    Colorizer c(L"nonexistent", L"nonexistent");
    CHECK_FALSE(c.supports("c"));
}

TEST_CASE("Colorizer available_languages with no grammar dir is empty") {
    Colorizer c(L"nonexistent", L"nonexistent");
    CHECK(c.available_languages().empty());
}

// Integration tests with real grammar DLLs:
// These require compiled tree-sitter grammar DLLs in a known location.
// They will be enabled once grammar compilation is set up.
// Example (uncomment when grammars are available):
//
// TEST_CASE("Colorizer end-to-end with C grammar") {
//     Colorizer c(L"grammars", L"config/themes");
//     REQUIRE(c.supports("c"));
//     auto result = c.colorize("// comment\nint x = 1;", "c", false);
//     CHECK_FALSE(result.spans.empty());
//     // First span should be a comment
//     CHECK(result.spans[0].color == 0x008000);  // comment green
// }
```

- [ ] **Step 4: Build and run tests**

```bash
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe
```

Expected: all tests pass.

- [ ] **Step 5: Run existing md tests for regression**

```bash
./build/Release/tests.exe
```

Expected: all pass unchanged.

- [ ] **Step 6: Commit**

```bash
git add src/colorizer/colorizer.h src/colorizer/colorizer.cpp tests/test_colorizer.cpp
git commit -m "feat: wire up Colorizer with GrammarRegistry + Tokenizer + ScopeMapper + ThemeLoader"
```

---

### Task 7: Build grammar DLLs and enable integration tests

**Files:**
- Create: `scripts/build-grammars.sh`
- Modify: `tests/test_colorizer.cpp`
- Modify: `tests/test_colorizer_tokenizer.cpp`
- Modify: `tests/test_colorizer_grammar.cpp`
- Modify: `CMakeLists.txt` (optional: add grammar build step)

This task compiles tree-sitter grammar repos into DLLs and enables the integration tests that need them.

- [ ] **Step 1: Create `scripts/build-grammars.sh`**

This script clones tree-sitter grammar repos and compiles each to a DLL. Start with a small set for testing (C, JSON, Python):

```bash
#!/usr/bin/env bash
set -euo pipefail

GRAMMAR_DIR="$(cd "$(dirname "$0")/.." && pwd)/grammars"
BUILD_DIR="$(cd "$(dirname "$0")/.." && pwd)/build/grammars"
mkdir -p "$GRAMMAR_DIR" "$BUILD_DIR"

# Grammar repos to build
declare -A GRAMMARS=(
    [c]="https://github.com/tree-sitter/tree-sitter-c"
    [json]="https://github.com/tree-sitter/tree-sitter-json"
    [python]="https://github.com/tree-sitter/tree-sitter-python"
    [cpp]="https://github.com/tree-sitter/tree-sitter-cpp"
    [javascript]="https://github.com/tree-sitter/tree-sitter-javascript"
    [typescript]="https://github.com/tree-sitter/tree-sitter-typescript"
    [rust]="https://github.com/tree-sitter/tree-sitter-rust"
    [go]="https://github.com/nickel-lang/tree-sitter-go"
    [java]="https://github.com/tree-sitter/tree-sitter-java"
    [bash]="https://github.com/tree-sitter/tree-sitter-bash"
    [html]="https://github.com/tree-sitter/tree-sitter-html"
    [css]="https://github.com/tree-sitter/tree-sitter-css"
    [toml]="https://github.com/tree-sitter/tree-sitter-toml"
    [yaml]="https://github.com/tree-sitter-grammars/tree-sitter-yaml"
    [markdown]="https://github.com/tree-sitter-grammars/tree-sitter-markdown"
)

for lang in "${!GRAMMARS[@]}"; do
    url="${GRAMMARS[$lang]}"
    repo_dir="$BUILD_DIR/$lang"
    dll_name="tree-sitter-${lang}.dll"

    echo "=== Building $lang ==="

    if [ ! -d "$repo_dir" ]; then
        git clone --depth 1 "$url" "$repo_dir"
    fi

    # Find the parser.c file
    src_dir="$repo_dir/src"
    if [ "$lang" = "typescript" ]; then
        src_dir="$repo_dir/typescript/src"
    fi

    if [ ! -f "$src_dir/parser.c" ]; then
        echo "  SKIP: no parser.c found in $src_dir"
        continue
    fi

    # Compile to DLL using MSVC cl.exe
    # Collect all .c files in src/ (parser.c + optional scanner.c)
    c_files=("$src_dir/parser.c")
    [ -f "$src_dir/scanner.c" ] && c_files+=("$src_dir/scanner.c")

    # Some grammars have a scanner.cc (C++ scanner)
    cc_file=""
    [ -f "$src_dir/scanner.cc" ] && cc_file="$src_dir/scanner.cc"

    pushd "$BUILD_DIR" > /dev/null

    if [ -n "$cc_file" ]; then
        cl.exe /nologo /O2 /LD /I "$src_dir" /I "$repo_dir/node_modules/tree-sitter/include" \
            "${c_files[@]}" "$cc_file" \
            /Fe:"$GRAMMAR_DIR/$dll_name" /link /DLL 2>&1 || echo "  FAILED"
    else
        cl.exe /nologo /O2 /LD /I "$src_dir" \
            "${c_files[@]}" \
            /Fe:"$GRAMMAR_DIR/$dll_name" /link /DLL 2>&1 || echo "  FAILED"
    fi

    popd > /dev/null

    if [ -f "$GRAMMAR_DIR/$dll_name" ]; then
        echo "  OK: $dll_name"
    else
        echo "  FAILED: $dll_name not produced"
    fi
done

echo ""
echo "=== Grammar DLLs in $GRAMMAR_DIR ==="
ls -la "$GRAMMAR_DIR"/*.dll 2>/dev/null || echo "  (none)"
```

Note: This script assumes MSVC `cl.exe` is on PATH (run from a VS Developer Command Prompt). The exact build command may need adjustment based on each grammar's structure. Some grammars may require `tree-sitter generate` first. Adapt as needed.

- [ ] **Step 2: Run the grammar build script**

```bash
bash scripts/build-grammars.sh
```

Expected: at least `tree-sitter-c.dll`, `tree-sitter-json.dll`, `tree-sitter-python.dll` are produced in `grammars/`.

- [ ] **Step 3: Update `tests/test_colorizer_grammar.cpp` with integration tests**

Add after existing tests:

```cpp
// --- Integration tests (require grammar DLLs in grammars/) ---

TEST_CASE("GrammarRegistry discovers grammar DLLs" * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    CHECK(reg.supports("c"));
    CHECK(reg.supports("json"));
}

TEST_CASE("GrammarRegistry loads C grammar" * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto* lang = reg.get_grammar("c");
    REQUIRE(lang != nullptr);
}
```

Add `#include <filesystem>` at the top.

- [ ] **Step 4: Update `tests/test_colorizer_tokenizer.cpp` with integration tests**

Add:

```cpp
#include <filesystem>

TEST_CASE("Tokenizer produces spans for C code" * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-c.dll"))) {
    GrammarRegistry reg(L"grammars");
    auto* grammar = reg.get_grammar("c");
    REQUIRE(grammar != nullptr);

    auto spans = Tokenizer::tokenize(grammar, "// hello\nint x = 42;");
    CHECK_FALSE(spans.empty());

    // Find comment span
    bool found_comment = false;
    for (auto& s : spans) {
        if (s.node_type == "comment") {
            found_comment = true;
            CHECK(s.start == 0);
            CHECK(s.length == 8);  // "// hello"
        }
    }
    CHECK(found_comment);
}
```

Add `#include "grammar_registry.h"` at the top.

- [ ] **Step 5: Update `tests/test_colorizer.cpp` with end-to-end integration tests**

Uncomment and refine the integration test:

```cpp
#include <filesystem>

TEST_CASE("Colorizer end-to-end with C grammar" * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-c.dll"))) {
    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("c"));

    auto result = c.colorize("// comment\nint x = 1;", "c", false);
    CHECK_FALSE(result.spans.empty());

    // Spans should be sorted by offset
    for (size_t i = 1; i < result.spans.size(); i++) {
        CHECK(result.spans[i].start >= result.spans[i - 1].start);
    }
}

TEST_CASE("Colorizer dark mode produces different colors" * doctest::skip(!std::filesystem::exists("grammars/tree-sitter-c.dll"))) {
    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("c"));

    auto light = c.colorize("int x = 1;", "c", false);
    auto dark = c.colorize("int x = 1;", "c", true);

    CHECK_FALSE(light.spans.empty());
    CHECK_FALSE(dark.spans.empty());
    // At least one color should differ between light and dark
    CHECK(light.spans[0].color != dark.spans[0].color);
}
```

- [ ] **Step 6: Build and run all tests**

```bash
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe
./build/Release/tests.exe
```

Expected: all pass. Grammar integration tests run if DLLs exist, skip otherwise.

- [ ] **Step 7: Commit**

```bash
git add scripts/build-grammars.sh tests/test_colorizer.cpp tests/test_colorizer_tokenizer.cpp tests/test_colorizer_grammar.cpp
git commit -m "feat: add grammar build script and enable integration tests with real tree-sitter grammars"
```

---

### Task 8: Integrate colorizer-core into the markdown plugin

**Files:**
- Modify: `src/layout_engine.cpp`
- Modify: `src/layout_engine.h`
- Modify: `src/host_adapter.cpp`
- Modify: `src/theme_service.h`
- Modify: `src/theme_service.cpp`
- Modify: `config/wlx-listerine-md.toml`
- Modify: `CMakeLists.txt`

This adds syntax highlighting to fenced code blocks in the markdown renderer.

- [ ] **Step 1: Add `[code]` section to `config/wlx-listerine-md.toml`**

Append to the end of the file:

```toml
[code]
grammar_dir = "grammars"
theme_dir = "themes"
default_language = ""
theme = "default"
```

- [ ] **Step 2: Add code config fields to `ThemeService`**

In `src/theme_service.h`, add to `ThemeConfig`:

```cpp
struct ThemeConfig {
    // ... existing fields ...

    // Code highlighting
    std::wstring code_grammar_dir = L"grammars";
    std::wstring code_theme_dir = L"themes";
    std::string code_default_language;  // empty = no highlighting for untagged blocks
    std::string code_theme = "default";
};
```

In `src/theme_service.cpp`, in the `load()` method, add after the `[colors.dark]` section:

```cpp
        // [code]
        if (auto v = tbl["code"]["grammar_dir"].value<std::string>())
            config_.code_grammar_dir = utf8_to_wstring(*v);
        if (auto v = tbl["code"]["theme_dir"].value<std::string>())
            config_.code_theme_dir = utf8_to_wstring(*v);
        if (auto v = tbl["code"]["default_language"].value<std::string>())
            config_.code_default_language = *v;
        if (auto v = tbl["code"]["theme"].value<std::string>())
            config_.code_theme = *v;
```

- [ ] **Step 3: Link `wlx-listerine-md` to `colorizer-core` in CMakeLists.txt**

```cmake
target_link_libraries(wlx-listerine-md PRIVATE
    wlx-core
    colorizer-core
    shell32
    dwmapi
    uxtheme
)
```

- [ ] **Step 4: Create and store a global Colorizer in `host_adapter.cpp`**

Add include at top:

```cpp
#include "colorizer.h"
```

Add global after existing globals:

```cpp
static std::unique_ptr<Colorizer> g_colorizer;
```

In `ensure_theme()`, after loading theme, initialize the colorizer:

```cpp
static void ensure_theme() {
    if (!g_theme_loaded) {
        std::wstring cfg_path = get_module_dir() + L"wlx-listerine-md.toml";
        g_theme.load(cfg_path);
        g_theme_loaded = true;

        // Initialize colorizer
        std::wstring base = get_module_dir();
        std::wstring grammar_dir = base + g_theme.config().code_grammar_dir;
        std::wstring theme_dir = base + g_theme.config().code_theme_dir;
        g_colorizer = std::make_unique<Colorizer>(grammar_dir, theme_dir);
    }
}
```

In `DllMain` `DLL_PROCESS_DETACH`, add leak-safe cleanup:

```cpp
(void)new std::unique_ptr<Colorizer>(std::move(g_colorizer));
```

- [ ] **Step 5: Pass Colorizer pointer to LayoutEngine**

In `src/layout_engine.h`, add forward declaration and modify constructor:

```cpp
class Colorizer;  // forward declaration at top of file

class LayoutEngine {
public:
    LayoutEngine(IDWriteFactory* dwrite, const ThemeService& theme, bool dark_mode,
                 const Colorizer* colorizer = nullptr);
    // ... rest unchanged ...

private:
    // ... existing members ...
    const Colorizer* colorizer_;
};
```

In `src/layout_engine.cpp`, update constructor:

```cpp
LayoutEngine::LayoutEngine(IDWriteFactory* dwrite, const ThemeService& theme, bool dark_mode,
                           const Colorizer* colorizer)
    : dwrite_(dwrite)
    , theme_(theme)
    , colors_(theme.palette(dark_mode))
    , spacing_(theme.spacing())
    , fonts_(theme.fonts())
    , colorizer_(colorizer) {
    // ... rest unchanged ...
```

In `host_adapter.cpp` `do_layout()`, pass colorizer:

```cpp
LayoutEngine engine(g_dwrite_factory.Get(), g_theme, vs->dark_mode, g_colorizer.get());
```

In `screenshot_main.cpp`, pass nullptr (no colorizer for screenshots initially):

```cpp
LayoutEngine engine(dwrite_factory.Get(), theme, dark_mode, nullptr);
```

- [ ] **Step 6: Add syntax highlighting to `layout_code_fence` in `layout_engine.cpp`**

Add include at top:

```cpp
#include "colorizer.h"
```

Replace the code fence method to apply color ranges:

```cpp
void LayoutEngine::layout_code_fence(const BlockNode& node, float& y, float left, float right) {
    float padding = spacing_.code_padding;

    // Concatenate code text
    std::wstring code_text;
    for (auto& n : node.inlines)
        code_text += n.text;

    // Remove trailing newline if present
    if (!code_text.empty() && code_text.back() == L'\n')
        code_text.pop_back();

    float max_width = right - left - padding * 2;
    ComPtr<IDWriteTextLayout> text_layout;
    dwrite_->CreateTextLayout(code_text.c_str(), static_cast<UINT32>(code_text.size()),
                              code_format_.Get(), max_width, 100000.0f,
                              text_layout.GetAddressOf());
    if (!text_layout) return;

    // --- Syntax highlighting ---
    std::vector<ColorRange> color_ranges;
    if (colorizer_) {
        // Determine language
        std::string lang;
        if (!node.code_language.empty()) {
            // Convert wstring to string
            lang.assign(node.code_language.begin(), node.code_language.end());
        } else {
            lang = theme_.config().code_default_language;
        }

        if (!lang.empty() && colorizer_->supports(lang)) {
            // Convert wstring source to UTF-8 for colorizer
            std::string utf8_source;
            for (wchar_t wc : code_text) {
                if (wc < 0x80) {
                    utf8_source += static_cast<char>(wc);
                } else if (wc < 0x800) {
                    utf8_source += static_cast<char>(0xC0 | (wc >> 6));
                    utf8_source += static_cast<char>(0x80 | (wc & 0x3F));
                } else {
                    utf8_source += static_cast<char>(0xE0 | (wc >> 12));
                    utf8_source += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
                    utf8_source += static_cast<char>(0x80 | (wc & 0x3F));
                }
            }

            bool dark = (&colors_ == &theme_.palette(true));
            auto cr = colorizer_->colorize(utf8_source, lang, dark);

            // Convert UTF-8 byte offsets to wchar_t offsets
            for (auto& span : cr.spans) {
                // Map UTF-8 byte offset -> wchar_t offset
                uint32_t wchar_start = 0;
                uint32_t byte_pos = 0;
                for (uint32_t i = 0; i < code_text.size() && byte_pos < span.start; i++) {
                    wchar_t wc = code_text[i];
                    if (wc < 0x80) byte_pos += 1;
                    else if (wc < 0x800) byte_pos += 2;
                    else byte_pos += 3;
                    wchar_start = i + 1;
                }

                uint32_t wchar_end = wchar_start;
                uint32_t target_end = span.start + span.length;
                for (uint32_t i = wchar_start; i < code_text.size() && byte_pos < target_end; i++) {
                    wchar_t wc = code_text[i];
                    if (wc < 0x80) byte_pos += 1;
                    else if (wc < 0x800) byte_pos += 2;
                    else byte_pos += 3;
                    wchar_end = i + 1;
                }

                uint32_t wchar_len = wchar_end - wchar_start;
                if (wchar_len > 0) {
                    color_ranges.push_back({wchar_start, wchar_len, span.color});
                }
            }
        }
    }

    DWRITE_TEXT_METRICS metrics;
    text_layout->GetMetrics(&metrics);

    float block_height = metrics.height + padding * 2;

    LayoutBlock lb;
    lb.type = BlockType::CodeFence;
    lb.rect = D2D1::RectF(left, y, right, y + block_height);
    lb.has_background = true;
    lb.background_color = colors_.code_bg;

    TextRun run;
    run.text = code_text;
    run.rect = D2D1::RectF(left + padding, y + padding,
                            right - padding, y + padding + metrics.height);
    run.layout = text_layout;
    run.color = colors_.text;
    run.is_code = true;
    run.color_ranges = std::move(color_ranges);
    lb.text_runs.push_back(std::move(run));

    y += block_height + spacing_.paragraph_spacing;
    result_.blocks.push_back(std::move(lb));
}
```

- [ ] **Step 7: Build and run all tests**

```bash
cmake --build --preset conan-release
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

Expected: all pass. The layout engine now accepts an optional colorizer but falls back gracefully when absent.

- [ ] **Step 8: Commit**

```bash
git add src/layout_engine.h src/layout_engine.cpp src/host_adapter.cpp src/theme_service.h src/theme_service.cpp config/wlx-listerine-md.toml CMakeLists.txt
git commit -m "feat: integrate colorizer-core into md plugin for syntax-highlighted code fences"
```

---

### Task 9: Create standalone WLX colorizer plugin

**Files:**
- Create: `src/colorizer/colorizer_host_adapter.cpp`
- Create: `src/colorizer/colorizer_layout.h`
- Create: `src/colorizer/colorizer_layout.cpp`
- Create: `src/colorizer/colorizer_plugin.def`
- Create: `src/colorizer/colorizer_resource.h`
- Create: `src/colorizer/colorizer_resource.rc`
- Create: `config/wlx-listerine-colorizer.toml`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `config/wlx-listerine-colorizer.toml`**

```toml
version = 1

[general]
extensions = ["c", "cpp", "h", "hpp", "py", "js", "ts", "rs", "go", "java", "cs", "rb", "php", "lua", "sh", "bash", "ps1", "vim", "json", "toml", "yaml", "yml", "xml", "html", "css", "md", "dockerfile", "cmake", "ini", "sql", "gitconfig", "gitignore", "gitattributes"]
detect_string = 'EXT="C" | EXT="CPP" | EXT="H" | EXT="HPP" | EXT="PY" | EXT="JS" | EXT="TS" | EXT="RS" | EXT="GO" | EXT="JAVA" | EXT="CS" | EXT="RB" | EXT="PHP" | EXT="LUA" | EXT="SH" | EXT="BASH" | EXT="PS1" | EXT="VIM" | EXT="JSON" | EXT="TOML" | EXT="YAML" | EXT="YML" | EXT="XML" | EXT="HTML" | EXT="CSS" | EXT="DOCKERFILE" | EXT="CMAKE" | EXT="INI" | EXT="SQL" | EXT="GITCONFIG" | EXT="GITIGNORE" | EXT="GITATTRIBUTES"'
grammar_dir = "grammars"
theme_dir = "themes"

[display]
line_numbers = true
word_wrap = false
tab_width = 4

[fonts]
code = "Cascadia Code"
code_size = 13.0

[spacing]
line_height_factor = 1.4

[themes]
default = "default"
```

- [ ] **Step 2: Create `src/colorizer/colorizer_layout.h`**

```cpp
#pragma once

#include "colorizer.h"
#include "layout_engine.h"
#include "theme_service.h"

#include <dwrite.h>
#include <string>

struct ColorizerDisplayConfig {
    bool line_numbers = true;
    bool word_wrap = false;
    int tab_width = 4;
    float line_height_factor = 1.4f;
};

// Builds a LayoutDocument from source code + color spans.
// Each line becomes a LayoutBlock with a single TextRun.
LayoutDocument layout_source(IDWriteFactory* dwrite,
                             const std::wstring& source,
                             const ColorizeResult& colors,
                             const ThemeService& theme,
                             bool dark_mode,
                             float viewport_width,
                             const ColorizerDisplayConfig& display);
```

- [ ] **Step 3: Create `src/colorizer/colorizer_layout.cpp`**

```cpp
#include "colorizer_layout.h"

#include <algorithm>
#include <string>
#include <vector>

// Split source into lines, preserving empty lines
static std::vector<std::wstring> split_lines(const std::wstring& source) {
    std::vector<std::wstring> lines;
    size_t pos = 0;
    while (pos <= source.size()) {
        size_t nl = source.find(L'\n', pos);
        if (nl == std::wstring::npos) {
            lines.push_back(source.substr(pos));
            break;
        }
        lines.push_back(source.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

// Expand tabs to spaces
static std::wstring expand_tabs(const std::wstring& line, int tab_width) {
    std::wstring out;
    int col = 0;
    for (wchar_t c : line) {
        if (c == L'\t') {
            int spaces = tab_width - (col % tab_width);
            out.append(spaces, L' ');
            col += spaces;
        } else {
            out += c;
            col++;
        }
    }
    return out;
}

LayoutDocument layout_source(IDWriteFactory* dwrite,
                             const std::wstring& source,
                             const ColorizeResult& colors,
                             const ThemeService& theme,
                             bool dark_mode,
                             float viewport_width,
                             const ColorizerDisplayConfig& display) {
    LayoutDocument result;
    result.viewport_width = viewport_width;

    const auto& palette = theme.palette(dark_mode);
    const auto& fonts = theme.fonts();

    auto lines = split_lines(source);

    // Create code text format
    ComPtr<IDWriteTextFormat> code_format;
    dwrite->CreateTextFormat(
        fonts.code_family.c_str(), nullptr,
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fonts.code_size, L"", code_format.GetAddressOf());

    if (!code_format) return result;

    // Line spacing
    float line_height = fonts.code_size * display.line_height_factor;
    code_format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                                 line_height, line_height * 0.8f);

    if (display.word_wrap)
        code_format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    else
        code_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // Gutter width for line numbers
    float gutter_width = 0;
    if (display.line_numbers) {
        int max_digits = 1;
        int n = static_cast<int>(lines.size());
        while (n >= 10) { max_digits++; n /= 10; }
        gutter_width = (max_digits + 2) * fonts.code_size * 0.6f;
    }

    float content_left = gutter_width + 8.0f;  // padding after gutter
    float content_width = viewport_width - content_left - 8.0f;
    if (content_width < 50.0f) content_width = 50.0f;

    float y = 4.0f;  // top padding

    // Build UTF-8 byte offset -> wchar_t offset mapping for color spans
    // (colors are in UTF-8 byte offsets, source is wstring)
    // We need a cumulative wchar_t offset per line start.
    // Since we expanded tabs and split lines from the original source,
    // we map color spans per-line.

    // Precompute line start in wchar_t offsets (before tab expansion)
    std::vector<uint32_t> line_wchar_starts;
    uint32_t wchar_offset = 0;
    for (auto& line : lines) {
        line_wchar_starts.push_back(wchar_offset);
        wchar_offset += static_cast<uint32_t>(line.size()) + 1;  // +1 for \n
    }

    for (size_t i = 0; i < lines.size(); i++) {
        std::wstring expanded = expand_tabs(lines[i], display.tab_width);

        // Create text layout for this line
        const std::wstring& layout_text = expanded.empty() ? std::wstring(1, L' ') : expanded;
        ComPtr<IDWriteTextLayout> text_layout;
        dwrite->CreateTextLayout(
            layout_text.c_str(), static_cast<UINT32>(layout_text.size()),
            code_format.Get(), content_width, 100000.0f,
            text_layout.GetAddressOf());

        if (!text_layout) continue;

        DWRITE_TEXT_METRICS metrics;
        text_layout->GetMetrics(&metrics);
        float block_height = std::max(metrics.height, line_height);

        // Apply color ranges for this line
        // Find color spans that overlap this line's wchar range
        uint32_t line_start_wchar = line_wchar_starts[i];
        uint32_t line_len_wchar = static_cast<uint32_t>(lines[i].size());

        std::vector<ColorRange> line_colors;
        for (auto& cs : colors.spans) {
            // Color spans are in UTF-8 byte offsets relative to the original source.
            // For simplicity in this initial version, we map by assuming ASCII.
            // TODO: proper UTF-8 to wchar mapping for non-ASCII source
            uint32_t span_end = cs.start + cs.length;
            uint32_t line_end_wchar = line_start_wchar + line_len_wchar;

            if (cs.start >= line_end_wchar || span_end <= line_start_wchar)
                continue;  // no overlap

            uint32_t local_start = (cs.start > line_start_wchar)
                ? (cs.start - line_start_wchar) : 0;
            uint32_t local_end = (span_end < line_end_wchar)
                ? (span_end - line_start_wchar) : line_len_wchar;

            if (local_start < local_end) {
                line_colors.push_back({local_start, local_end - local_start, cs.color});
            }
        }

        LayoutBlock lb;
        lb.type = BlockType::Paragraph;  // reuse Paragraph type for source lines
        lb.rect = D2D1::RectF(0, y, viewport_width, y + block_height);
        lb.has_background = true;
        lb.background_color = palette.background;

        // Line number as bullet
        if (display.line_numbers) {
            lb.bullet_text = std::to_wstring(i + 1);
            lb.bullet_pos = D2D1::Point2F(4.0f, y);
            lb.bullet_color = palette.muted;
        }

        TextRun run;
        run.text = expanded;
        run.rect = D2D1::RectF(content_left, y,
                                content_left + content_width, y + block_height);
        run.layout = text_layout;
        run.color = palette.text;
        run.is_code = true;
        run.color_ranges = std::move(line_colors);
        lb.text_runs.push_back(std::move(run));

        result.blocks.push_back(std::move(lb));
        y += block_height;
    }

    result.total_height = y + 4.0f;  // bottom padding
    return result;
}
```

- [ ] **Step 4: Create `src/colorizer/colorizer_plugin.def`**

```
LIBRARY "wlx-listerine-colorizer"
EXPORTS
    ListLoadW
    ListLoadNextW
    ListCloseWindow
    ListGetDetectString
    ListSendCommand
    ListSetDefaultParams
```

- [ ] **Step 5: Create `src/colorizer/colorizer_resource.h`**

```cpp
#pragma once
#define VS_VERSION_INFO 1
```

- [ ] **Step 6: Create `src/colorizer/colorizer_resource.rc`**

```rc
#include <winver.h>
#include "colorizer_resource.h"

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
            VALUE "FileDescription", "Listerine Syntax Colorizer for Total Commander"
            VALUE "FileVersion", "1.0.0.0"
            VALUE "InternalName", "wlx-listerine-colorizer"
            VALUE "OriginalFilename", "wlx-listerine-colorizer.wlx64"
            VALUE "ProductName", "WLX Listerine Colorizer"
            VALUE "ProductVersion", "1.0.0.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
```

- [ ] **Step 7: Create `src/colorizer/colorizer_host_adapter.cpp`**

This follows the same pattern as `src/host_adapter.cpp` but for the colorizer plugin. Key differences: no markdown parser, uses `Colorizer` + `colorizer_layout` instead, loads its own TOML config.

```cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <string>
#include <filesystem>

#include "listerplugin.h"
#include "file_service.h"
#include "render_engine.h"
#include "theme_service.h"
#include "colorizer.h"
#include "colorizer_layout.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

// ---------- colorizer-specific config ----------

struct ColorizerConfig {
    ThemeService theme;
    ColorizerDisplayConfig display;
    std::unordered_map<std::string, std::string> ext_to_language;
    // ext_to_language maps file extension (lowercase, no dot) -> language name

    void load(const std::wstring& toml_path);
};

void ColorizerConfig::load(const std::wstring& toml_path) {
    theme.load(toml_path);
    // Parse display and extension mapping from TOML
    // The ThemeService already loads [fonts], [spacing], [colors.*]
    // We need [display] and extension->language mapping from [general].extensions

    // Defaults
    display.line_numbers = true;
    display.word_wrap = false;
    display.tab_width = 4;
    display.line_height_factor = 1.4f;

    // Build extension -> language map from known extensions
    // This is a hardcoded map of file extensions to tree-sitter language names
    static const std::unordered_map<std::string, std::string> ext_map = {
        {"c", "c"}, {"h", "c"},
        {"cpp", "cpp"}, {"hpp", "cpp"}, {"cc", "cpp"}, {"cxx", "cpp"}, {"hh", "cpp"},
        {"py", "python"}, {"pyw", "python"},
        {"js", "javascript"}, {"mjs", "javascript"}, {"cjs", "javascript"},
        {"ts", "typescript"}, {"tsx", "typescript"},
        {"rs", "rust"},
        {"go", "go"},
        {"java", "java"},
        {"cs", "c-sharp"},
        {"rb", "ruby"},
        {"php", "php"},
        {"lua", "lua"},
        {"sh", "bash"}, {"bash", "bash"},
        {"ps1", "powershell"},
        {"vim", "vim"},
        {"json", "json"},
        {"toml", "toml"},
        {"yaml", "yaml"}, {"yml", "yaml"},
        {"xml", "xml"},
        {"html", "html"}, {"htm", "html"},
        {"css", "css"},
        {"md", "markdown"}, {"markdown", "markdown"},
        {"dockerfile", "dockerfile"},
        {"cmake", "cmake"},
        {"ini", "ini"}, {"cfg", "ini"},
        {"sql", "sql"},
        {"gitconfig", "gitconfig"},
        {"gitignore", "gitignore"},
        {"gitattributes", "gitattributes"},
    };
    ext_to_language = ext_map;

    // TODO: parse [display] section from TOML to override defaults
}

// ---------- per-window state ----------

struct ColorViewState {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    bool dark_mode = false;
    std::wstring file_path;

    std::shared_ptr<LayoutDocument> layout;
    std::unique_ptr<RenderEngine> renderer;

    float scroll_y = 0;
    float max_scroll_y = 0;

    // Selection
    TextPosition sel_anchor;
    TextPosition sel_active;
    bool selecting = false;
};

// ---------- globals ----------

static HMODULE g_hModule = nullptr;
static ColorizerConfig g_config;
static bool g_config_loaded = false;
static ComPtr<ID2D1Factory> g_d2d_factory;
static ComPtr<IDWriteFactory> g_dwrite_factory;
static std::unique_ptr<Colorizer> g_colorizer;
static FileService g_file_service;
static std::unordered_map<HWND, ColorViewState*> g_views;
static ATOM g_window_class = 0;
static std::string g_default_ini_path;

// ---------- helpers ----------

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static void apply_dark_mode(HWND hwnd, bool dark) {
    BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
    SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

static std::wstring get_module_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hModule, path, MAX_PATH);
    std::wstring dir(path);
    auto pos = dir.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? dir.substr(0, pos + 1) : dir;
}

static void ensure_factories() {
    if (!g_d2d_factory)
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_d2d_factory.GetAddressOf());
    if (!g_dwrite_factory)
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(g_dwrite_factory.GetAddressOf()));
}

static void ensure_config() {
    if (!g_config_loaded) {
        std::wstring cfg_path = get_module_dir() + L"wlx-listerine-colorizer.toml";
        g_config.load(cfg_path);
        g_config_loaded = true;

        std::wstring base = get_module_dir();
        g_colorizer = std::make_unique<Colorizer>(
            base + g_config.theme.config().code_grammar_dir,
            base + g_config.theme.config().code_theme_dir);
    }
}

static std::string get_language_for_file(const std::wstring& path) {
    auto ext_pos = path.find_last_of(L'.');
    if (ext_pos == std::wstring::npos) return {};
    std::wstring wext = path.substr(ext_pos + 1);
    std::string ext(wext.begin(), wext.end());
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    auto it = g_config.ext_to_language.find(ext);
    if (it != g_config.ext_to_language.end())
        return it->second;
    return {};
}

static void update_scrollbar(ColorViewState* vs) {
    if (!vs->layout || !vs->hwnd) return;
    float viewport_h = vs->renderer ? vs->renderer->dip_height() : 1.0f;
    vs->max_scroll_y = std::max(0.0f, vs->layout->total_height - viewport_h);
    vs->scroll_y = std::clamp(vs->scroll_y, 0.0f, vs->max_scroll_y);

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = static_cast<int>(vs->layout->total_height);
    si.nPage = static_cast<UINT>(viewport_h);
    si.nPos = static_cast<int>(vs->scroll_y);
    SetScrollInfo(vs->hwnd, SB_VERT, &si, TRUE);
}

static void do_layout(ColorViewState* vs) {
    if (!vs->layout || !g_dwrite_factory) return;
    // Re-layout is handled in load_document; here we just update scrollbar
    update_scrollbar(vs);
}

static void load_document(ColorViewState* vs, const wchar_t* path) {
    vs->file_path = path;
    vs->scroll_y = 0;
    vs->sel_anchor = TextPosition{};
    vs->sel_active = TextPosition{};
    vs->selecting = false;

    auto content = g_file_service.read(path);
    if (!content) return;

    std::string lang = get_language_for_file(path);
    ColorizeResult colors;
    if (!lang.empty() && g_colorizer && g_colorizer->supports(lang)) {
        colors = g_colorizer->colorize(content->raw_utf8, lang, vs->dark_mode);
    }

    float viewport_width = vs->renderer ? vs->renderer->dip_width() : 800.0f;
    auto layout_doc = layout_source(g_dwrite_factory.Get(), content->text, colors,
                                     g_config.theme, vs->dark_mode, viewport_width,
                                     g_config.display);
    vs->layout = std::make_shared<LayoutDocument>(std::move(layout_doc));
    update_scrollbar(vs);
}

// ---------- WndProc ----------

static void handle_scroll(ColorViewState* vs, float delta) {
    if (!vs->layout) return;
    float old_y = vs->scroll_y;
    vs->scroll_y = std::clamp(vs->scroll_y + delta, 0.0f, vs->max_scroll_y);
    if (vs->scroll_y != old_y) {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_POS;
        si.nPos = static_cast<int>(vs->scroll_y);
        SetScrollInfo(vs->hwnd, SB_VERT, &si, TRUE);
        InvalidateRect(vs->hwnd, nullptr, FALSE);
    }
}

static LRESULT CALLBACK ColorViewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* vs = reinterpret_cast<ColorViewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (vs && vs->renderer && vs->layout) {
            if (vs->renderer->needs_recreate())
                vs->renderer->create_device_resources(hwnd);
            auto sel_lo = std::min(vs->sel_anchor, vs->sel_active);
            auto sel_hi = std::max(vs->sel_anchor, vs->sel_active);
            vs->renderer->paint(*vs->layout, vs->scroll_y, sel_lo, sel_hi);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        UINT w = LOWORD(lp);
        UINT h = HIWORD(lp);
        if (vs && vs->renderer && w > 0 && h > 0) {
            vs->renderer->resize(w, h);
            // Re-layout with new viewport width
            if (!vs->file_path.empty())
                load_document(vs, vs->file_path.c_str());
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_VSCROLL: {
        if (!vs) break;
        float page = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        float line = g_config.theme.fonts().code_size * g_config.display.line_height_factor;

        switch (LOWORD(wp)) {
        case SB_LINEUP:    handle_scroll(vs, -line); break;
        case SB_LINEDOWN:  handle_scroll(vs, line); break;
        case SB_PAGEUP:    handle_scroll(vs, -page); break;
        case SB_PAGEDOWN:  handle_scroll(vs, page); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            vs->scroll_y = std::clamp(static_cast<float>(si.nTrackPos), 0.0f, vs->max_scroll_y);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case SB_TOP:    vs->scroll_y = 0; InvalidateRect(hwnd, nullptr, FALSE); break;
        case SB_BOTTOM: vs->scroll_y = vs->max_scroll_y; InvalidateRect(hwnd, nullptr, FALSE); break;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (!vs) break;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        float line = g_config.theme.fonts().code_size * g_config.display.line_height_factor;
        handle_scroll(vs, -static_cast<float>(delta) / 120.0f * line * 3.0f);
        return 0;
    }

    case WM_KEYDOWN: {
        if (!vs) break;
        float page = vs->renderer ? vs->renderer->dip_height() : 100.0f;
        float line = g_config.theme.fonts().code_size * g_config.display.line_height_factor;

        switch (wp) {
        case VK_UP:    handle_scroll(vs, -line); break;
        case VK_DOWN:  handle_scroll(vs, line); break;
        case VK_PRIOR: handle_scroll(vs, -page); break;
        case VK_NEXT:  handle_scroll(vs, page); break;
        case VK_HOME:
            vs->scroll_y = 0;
            update_scrollbar(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case VK_END:
            vs->scroll_y = vs->max_scroll_y;
            update_scrollbar(vs);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------- window class ----------

static void ensure_window_class() {
    if (g_window_class) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ColorViewWndProc;
    wc.hInstance = g_hModule;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = L"WlxListerineColorView";
    g_window_class = RegisterClassExW(&wc);
}

// ---------- DLL entry ----------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        (void)new ComPtr<ID2D1Factory>(std::move(g_d2d_factory));
        (void)new ComPtr<IDWriteFactory>(std::move(g_dwrite_factory));
        (void)new std::unique_ptr<Colorizer>(std::move(g_colorizer));
        g_views.clear();
        if (reserved == nullptr && g_window_class) {
            UnregisterClassW(L"WlxListerineColorView", g_hModule);
            g_window_class = 0;
        }
        break;
    }
    return TRUE;
}

// ---------- WLX exports ----------

extern "C" {

HWND __stdcall ListLoadW(HWND ParentWin, wchar_t* FileToLoad, int ShowFlags) {
    ensure_factories();
    if (!g_d2d_factory || !g_dwrite_factory) return nullptr;

    ensure_config();
    ensure_window_class();

    bool dark = (ShowFlags & lcp_darkmode) != 0;

    RECT rc;
    GetClientRect(ParentWin, &rc);

    HWND hwnd = CreateWindowExW(
        0, L"WlxListerineColorView", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        0, 0, rc.right, rc.bottom,
        ParentWin, nullptr, g_hModule, nullptr);

    if (!hwnd) return nullptr;
    apply_dark_mode(hwnd, dark);

    auto* vs = new ColorViewState{};
    vs->hwnd = hwnd;
    vs->parent = ParentWin;
    vs->dark_mode = dark;

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(vs));
    g_views[hwnd] = vs;

    vs->renderer = std::make_unique<RenderEngine>(
        g_d2d_factory.Get(), g_dwrite_factory.Get(), g_config.theme, dark);
    vs->renderer->create_device_resources(hwnd);

    load_document(vs, FileToLoad);
    InvalidateRect(hwnd, nullptr, FALSE);

    return hwnd;
}

int __stdcall ListLoadNextW(HWND ParentWin, HWND PluginWin, wchar_t* FileToLoad, int ShowFlags) {
    auto it = g_views.find(PluginWin);
    if (it == g_views.end()) return LISTPLUGIN_ERROR;

    auto* vs = it->second;
    bool new_dark = (ShowFlags & lcp_darkmode) != 0;

    if (new_dark != vs->dark_mode) {
        vs->dark_mode = new_dark;
        vs->renderer->set_dark_mode(new_dark);
        apply_dark_mode(vs->hwnd, new_dark);
    }

    load_document(vs, FileToLoad);
    InvalidateRect(vs->hwnd, nullptr, FALSE);
    return LISTPLUGIN_OK;
}

void __stdcall ListCloseWindow(HWND ListWin) {
    auto it = g_views.find(ListWin);
    if (it != g_views.end()) {
        delete it->second;
        g_views.erase(it);
    }
    DestroyWindow(ListWin);
}

void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    ensure_config();
    const auto& ds = g_config.theme.config().detect_string;
    int len = WideCharToMultiByte(CP_ACP, 0, ds.c_str(), static_cast<int>(ds.size()),
                                   DetectString, maxlen - 1, nullptr, nullptr);
    DetectString[len] = '\0';
}

int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter) {
    auto it = g_views.find(ListWin);
    if (it == g_views.end()) return LISTPLUGIN_ERROR;

    auto* vs = it->second;

    switch (Command) {
    case lc_copy:
        return LISTPLUGIN_ERROR;  // No text selection yet

    case lc_selectall:
        return LISTPLUGIN_ERROR;

    case lc_newparams: {
        bool new_dark = (Parameter & lcp_darkmode) != 0;
        if (new_dark != vs->dark_mode) {
            vs->dark_mode = new_dark;
            vs->renderer->set_dark_mode(new_dark);
            if (!vs->file_path.empty())
                load_document(vs, vs->file_path.c_str());
            InvalidateRect(vs->hwnd, nullptr, FALSE);
        }
        return LISTPLUGIN_OK;
    }

    case lc_setpercent: {
        if (vs->layout && vs->max_scroll_y > 0) {
            float pct = static_cast<float>(Parameter) / 100.0f;
            vs->scroll_y = std::clamp(pct * vs->max_scroll_y, 0.0f, vs->max_scroll_y);
            update_scrollbar(vs);
            InvalidateRect(vs->hwnd, nullptr, FALSE);
        }
        return LISTPLUGIN_OK;
    }

    default:
        return LISTPLUGIN_ERROR;
    }
}

void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps) {
    if (dps && dps->size >= sizeof(ListDefaultParamStruct))
        g_default_ini_path = dps->DefaultIniName;
}

} // extern "C"
```

- [ ] **Step 8: Add `wlx-listerine-colorizer` target to CMakeLists.txt**

```cmake
# --- Colorizer Plugin DLL ---
add_library(wlx-listerine-colorizer SHARED
    src/colorizer/colorizer_host_adapter.cpp
    src/colorizer/colorizer_layout.cpp
    src/colorizer/colorizer_plugin.def
    src/colorizer/colorizer_resource.rc
)

target_link_libraries(wlx-listerine-colorizer PRIVATE
    wlx-core
    colorizer-core
    shell32
    dwmapi
    uxtheme
)

set_target_properties(wlx-listerine-colorizer PROPERTIES
    SUFFIX ".wlx64"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
)

add_custom_command(TARGET wlx-listerine-colorizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-colorizer.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-colorizer.toml"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/output/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/themes/default.toml"
        "${CMAKE_SOURCE_DIR}/output/themes/default.toml"
)
```

- [ ] **Step 9: Build**

```bash
cmake --build --preset conan-release
```

Expected: produces `output/wlx-listerine-colorizer.wlx64`, `output/wlx-listerine-colorizer.toml`, `output/themes/default.toml`.

- [ ] **Step 10: Run all tests**

```bash
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
```

Expected: all pass.

- [ ] **Step 11: Commit**

```bash
git add src/colorizer/colorizer_host_adapter.cpp src/colorizer/colorizer_layout.h src/colorizer/colorizer_layout.cpp src/colorizer/colorizer_plugin.def src/colorizer/colorizer_resource.h src/colorizer/colorizer_resource.rc config/wlx-listerine-colorizer.toml CMakeLists.txt
git commit -m "feat: add standalone wlx-listerine-colorizer WLX plugin"
```

---

### Task 10: Copy themes to md plugin output and update CLAUDE.md

**Files:**
- Modify: `CMakeLists.txt` (post-build copy for md plugin)
- Modify: `CLAUDE.md`

- [ ] **Step 1: Add post-build copy for themes to md plugin target**

In CMakeLists.txt, update the `wlx-listerine-md` post-build command:

```cmake
add_custom_command(TARGET wlx-listerine-md POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-md.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-md.toml"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/output/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/themes/default.toml"
        "${CMAKE_SOURCE_DIR}/output/themes/default.toml"
)
```

- [ ] **Step 2: Update CLAUDE.md with colorizer plugin info**

Add under `### Plugins`:

```markdown
- **wlx-listerine-colorizer** (`wlx-listerine-colorizer.wlx64`) — Syntax colorizer (tree-sitter based)
```

Update the Architecture section to include colorizer-core:

```markdown
### colorizer-core (static lib)
  -> GrammarRegistry      tree-sitter grammar DLL discovery + lazy loading
  -> Tokenizer             tree-sitter parse + leaf-node walk
  -> ScopeMapper           per-grammar node-type -> semantic scope tables
  -> ThemeLoader           per-language TOML theme files (light/dark)
```

- [ ] **Step 3: Build, run tests, verify output**

```bash
cmake --build --preset conan-release
./build/Release/tests.exe
./build/Release/colorizer-tests.exe
ls output/
```

Expected: `output/` contains both `.wlx64` files, both `.toml` configs, and `themes/default.toml`.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt CLAUDE.md
git commit -m "chore: copy themes to output and update CLAUDE.md with colorizer plugin"
```

---

### Task 11: Expand scope mapper tables for all shipped languages

**Files:**
- Modify: `src/colorizer/scope_mapper.cpp`
- Modify: `tests/test_colorizer_scope.cpp`

Add keyword/type tables for: Rust, Go, Java, C#, Ruby, PHP, Lua, Bash, PowerShell, Vim script, HTML, CSS, TOML, YAML, JSON, XML, Markdown, Dockerfile, CMake, INI, SQL, Git config files.

- [ ] **Step 1: Add scope mapping tests for remaining languages**

Add tests for each language's key tokens (e.g., Rust `fn`, Go `func`, Java `class`). One test case per language testing at least keywords and comments.

- [ ] **Step 2: Add keyword tables for remaining languages**

Extend `g_keywords` and `g_type_keywords` in `scope_mapper.cpp` for each shipped language.

- [ ] **Step 3: Add generic node type mappings for grammar-specific types**

Some grammars use unique node types (e.g., HTML `tag_name`, CSS `property_name`). Add these to `g_generic_map`.

- [ ] **Step 4: Build and run tests**

```bash
cmake --build --preset conan-release
./build/Release/colorizer-tests.exe
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/colorizer/scope_mapper.cpp tests/test_colorizer_scope.cpp
git commit -m "feat: expand scope mapper tables for all 27 shipped languages"
```

---

### Task 12: Manual integration testing

**Files:** none (testing only)

- [ ] **Step 1: Build grammar DLLs**

```bash
bash scripts/build-grammars.sh
```

Copy produced DLLs to `output/grammars/`.

- [ ] **Step 2: Test standalone colorizer in Total Commander**

1. Copy `output/wlx-listerine-colorizer.wlx64`, `output/wlx-listerine-colorizer.toml`, `output/themes/`, and `output/grammars/` to TC plugins directory
2. Configure TC to use the plugin
3. Open a `.cpp` file — verify syntax highlighting, line numbers, scrolling, dark mode toggle
4. Open `.py`, `.json`, `.toml` — verify each language colorizes correctly

- [ ] **Step 3: Test md plugin code fence highlighting**

1. Copy `output/wlx-listerine-md.wlx64`, `output/wlx-listerine-md.toml`, `output/themes/`, and `output/grammars/` to TC plugins directory
2. Open a `.md` file with fenced code blocks (` ```cpp `, ` ```python `)
3. Verify code blocks show syntax highlighting
4. Verify untagged code blocks remain monochrome (with default config)
5. Set `default_language = "python"` in config, reopen — verify untagged blocks highlight as Python

- [ ] **Step 4: Test graceful degradation**

1. Remove `grammars/` directory
2. Open `.cpp` in colorizer plugin — should show plain text (no crash)
3. Open `.md` with code fences — should show monochrome code blocks (no crash)

- [ ] **Step 5: Document any issues found and fix**

Create follow-up commits for any bugs discovered during manual testing.
