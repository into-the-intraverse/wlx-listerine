#include "runtime/host/grammar_menu.h"

#include <algorithm>
#include <cwctype>

namespace wlx::runtime::host {

namespace {

// Display-name table. Keep alphabetical by id for human readability.
struct DisplayEntry { const char* id; const wchar_t* display; };
constexpr DisplayEntry kDisplayTable[] = {
    {"bash",           L"Bash"},
    {"c",              L"C"},
    {"c-sharp",        L"C#"},
    {"cmake",          L"CMake"},
    {"cpp",            L"C++"},
    {"css",            L"CSS"},
    {"dockerfile",     L"Dockerfile"},
    {"git-config",     L"Git Config"},
    {"git_rebase",     L"Git Rebase"},
    {"gitattributes",  L"Git Attributes"},
    {"gitignore",      L"Git Ignore"},
    {"go",             L"Go"},
    {"html",           L"HTML"},
    {"java",           L"Java"},
    {"javascript",     L"JavaScript"},
    {"json",           L"JSON"},
    {"lua",            L"Lua"},
    {"php",            L"PHP"},
    {"powershell",     L"PowerShell"},
    {"python",         L"Python"},
    {"rust",           L"Rust"},
    {"sql",            L"SQL"},
    {"toml",           L"TOML"},
    {"typescript",     L"TypeScript"},
    {"vim",            L"Vim"},
    {"xml",            L"XML"},
    {"yaml",           L"YAML"},
};

std::wstring capitalize_ascii(std::string_view in) {
    std::wstring out;
    out.reserve(in.size());
    bool first = true;
    for (char c : in) {
        wchar_t w = static_cast<wchar_t>(static_cast<unsigned char>(c));
        if (first) {
            if (w >= L'a' && w <= L'z') w = static_cast<wchar_t>(w - 32);
            first = false;
        } else {
            if (w >= L'A' && w <= L'Z') w = static_cast<wchar_t>(w + 32);
        }
        out.push_back(w);
    }
    return out;
}

}  // namespace

std::wstring grammar_display_name(std::string_view grammar_id) {
    if (grammar_id.empty()) return {};
    for (const auto& e : kDisplayTable) {
        if (grammar_id == e.id) return e.display;
    }
    return capitalize_ascii(grammar_id);
}

std::vector<LanguageOption> available_grammars(WlxCore* core) {
    if (!core) return {};

    WlxLanguageList list{};
    if (wlx_core_list_languages(core, &list) != 0 || list.count == 0) {
        wlx_core_free_language_list(&list);
        return {};
    }

    // RAII guard: free the C-side allocation even if a string copy below
    // throws std::bad_alloc. Without this, the unwind path skips the
    // explicit free at the end of the function and leaks list.ids.
    struct ListGuard {
        WlxLanguageList* p;
        ~ListGuard() { wlx_core_free_language_list(p); }
    } guard{&list};

    std::vector<LanguageOption> out;
    out.reserve(list.count);
    for (uint32_t i = 0; i < list.count; ++i) {
        if (!list.ids[i]) continue;
        LanguageOption opt;
        opt.grammar_id   = list.ids[i];
        opt.display_name = grammar_display_name(opt.grammar_id);
        out.push_back(std::move(opt));
    }

    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        // Case-insensitive lexicographic compare on display_name.
        const auto& x = a.display_name;
        const auto& y = b.display_name;
        size_t n = std::min(x.size(), y.size());
        for (size_t i = 0; i < n; ++i) {
            wchar_t cx = static_cast<wchar_t>(towlower(static_cast<wint_t>(x[i])));
            wchar_t cy = static_cast<wchar_t>(towlower(static_cast<wint_t>(y[i])));
            if (cx != cy) return cx < cy;
        }
        return x.size() < y.size();
    });

    return out;
}

}  // namespace wlx::runtime::host
