#pragma once

#include <string>
#include <string_view>

#include <cwctype>

namespace wlx::plugin_colorizer::language {

namespace detail {

// Extension → tree-sitter grammar id. Lowercase ASCII keys; all matching
// is case-insensitive (callers lowercase before lookup).
//
// "Extension" here is whatever follows the LAST dot in the filename. Because
// dotfiles like `.bashrc` have only the leading dot, their "extension" is
// the entire body after that dot — `.bashrc` → ext "bashrc" → bash, etc.
struct ExtEntry { const wchar_t* ext; const char* lang; };
constexpr ExtEntry kExtTable[] = {
    // ---- C / C++ (both .c and .h route to cpp; cpp/highlights.scm
    //                inherits c/highlights.scm).
    { L"c",              "cpp"        },
    { L"h",              "cpp"        },
    { L"cpp",            "cpp"        },
    { L"cc",             "cpp"        },
    { L"cxx",            "cpp"        },
    { L"hpp",            "cpp"        },
    { L"hxx",            "cpp"        },
    // ---- Python
    { L"py",             "python"     },
    { L"pyi",            "python"     },
    // ---- JavaScript / TypeScript
    { L"js",             "javascript" },
    { L"mjs",            "javascript" },
    { L"cjs",            "javascript" },
    { L"jsx",            "javascript" },
    { L"ts",             "typescript" },
    { L"tsx",            "typescript" },
    { L"mts",            "typescript" },
    // ---- Rust / Go / Java / C#
    { L"rs",             "rust"       },
    { L"go",             "go"         },
    { L"java",           "java"       },
    { L"cs",             "c-sharp"    },
    // ---- PHP / Lua
    { L"php",            "php"        },
    { L"lua",            "lua"        },
    // ---- Shell (incl. dotfile-name "extensions" for rc/profile files)
    { L"sh",             "bash"       },
    { L"bash",           "bash"       },
    { L"zsh",            "bash"       },
    { L"bashrc",         "bash"       },  // .bashrc
    { L"bash_profile",   "bash"       },
    { L"bash_aliases",   "bash"       },
    { L"bash_logout",    "bash"       },
    { L"zshrc",          "bash"       },  // .zshrc (no dedicated zsh grammar)
    { L"zshenv",         "bash"       },
    { L"zprofile",       "bash"       },
    { L"zlogin",         "bash"       },
    { L"zlogout",        "bash"       },
    { L"profile",        "bash"       },  // .profile
    { L"envrc",          "bash"       },  // .envrc (direnv)
    // ---- PowerShell
    { L"ps1",            "powershell" },
    { L"psm1",           "powershell" },
    { L"psd1",           "powershell" },
    // ---- Vim
    { L"vim",            "vim"        },  // .vim, init.vim
    { L"vimrc",          "vim"        },  // .vimrc
    { L"nvimrc",         "vim"        },  // .nvimrc
    // ---- Data / config
    { L"json",           "json"       },
    { L"jsonc",          "json"       },
    { L"toml",           "toml"       },
    { L"yaml",           "yaml"       },
    { L"yml",            "yaml"       },
    // ---- Markup
    { L"html",           "html"       },
    { L"htm",            "html"       },
    { L"xml",            "xml"        },
    { L"svg",            "xml"        },
    { L"css",            "css"        },
    // .md / .markdown intentionally NOT handled — wlx-listerine-md owns those.
    // ---- Visual Studio / MSBuild project files (all XML)
    { L"vcxproj",        "xml"        },
    { L"csproj",         "xml"        },
    { L"fsproj",         "xml"        },
    { L"vbproj",         "xml"        },
    { L"proj",           "xml"        },
    { L"props",          "xml"        },
    { L"targets",        "xml"        },
    { L"filters",        "xml"        },
    { L"xaml",           "xml"        },
    { L"resx",           "xml"        },
    // ---- Build / DevOps
    { L"cmake",          "cmake"      },
    { L"dockerfile",     "dockerfile" },
    { L"sql",            "sql"        },
    // ---- Git
    { L"gitconfig",      "git-config" },  // .gitconfig
    { L"gitmodules",     "git-config" },  // .gitmodules
    { L"gitignore",      "gitignore"  },  // .gitignore
    { L"gitattributes",  "gitattributes" },  // .gitattributes
    { L"dockerignore",   "gitignore"  },  // .dockerignore (same syntax)
    { L"npmignore",      "gitignore"  },  // .npmignore (same syntax)
};

inline std::wstring lowercase_copy(std::wstring_view in) {
    std::wstring out(in);
    for (auto& c : out) c = static_cast<wchar_t>(towlower(c));
    return out;
}

inline std::wstring filename_of(std::wstring_view path) {
    auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring_view::npos) return std::wstring(path);
    return std::wstring(path.substr(slash + 1));
}

}  // namespace detail

// Extension lookup. Pulls the part after the LAST dot in the FILENAME (not
// the path), lowercases it, and consults the table above. Returns "" when
// the filename has no dot at all (e.g. `Pipfile`, `Dockerfile`) — those
// cases fall through to filename_to_language().
//
// Bug-fix vs. naive `path.find_last_of('.')`: that breaks for paths like
// `D:\my.project\Pipfile`, where the last dot lives in a directory name.
// We restrict the dot search to the basename.
inline std::string ext_to_language(std::wstring_view path) {
    std::wstring filename = detail::filename_of(path);
    auto dot = filename.find_last_of(L'.');
    if (dot == std::wstring::npos) return {};
    std::wstring ext = filename.substr(dot + 1);
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    for (const auto& e : detail::kExtTable) {
        if (ext == e.ext) return e.lang;
    }
    return {};
}

// Filename special-cases for files whose extension lookup misses or is
// ambiguous. Always called AFTER ext_to_language() returns empty.
//
// Examples:
//   Pipfile          → "toml"   (no extension at all)
//   uv.lock          → "toml"   (`.lock` is ambiguous; specific name wins)
//   Pipfile.lock     → "json"   (Pipfile.lock is JSON, not TOML)
//   CMakeLists.txt   → "cmake"  (`.txt` doesn't disambiguate)
//   CMakeCache.txt   → "cmake"  (loose — actually KEY:TYPE=VALUE flat
//                                format, but cmake grammar tokenizes the
//                                identifier-shaped keys passably)
//   git-rebase-todo  → "git_rebase"
inline std::string filename_to_language(std::wstring_view path) {
    std::wstring filename = detail::lowercase_copy(detail::filename_of(path));

    // Dockerfile family.
    if (filename == L"dockerfile" || filename == L"containerfile")
        return "dockerfile";
    if (filename.find(L"dockerfile") != std::wstring::npos)
        return "dockerfile";

    // CMake.
    if (filename == L"cmakelists.txt") return "cmake";
    if (filename == L"cmakecache.txt") return "cmake";

    // Python ecosystem.
    if (filename == L"pipfile")        return "toml";
    if (filename == L"pipfile.lock")   return "json";
    if (filename == L"uv.lock")        return "toml";
    if (filename == L"poetry.lock")    return "toml";

    // JS ecosystem.
    if (filename == L"bun.lock")       return "json";

    // Git rebase todo (no extension; matched by substring because TC may
    // present it as `git-rebase-todo` or `.git/rebase-merge/git-rebase-todo`).
    // Note: .gitconfig / .gitmodules / .gitignore / .gitattributes are
    // already caught by ext_to_language via the {gitconfig, gitmodules,
    // gitignore, gitattributes} entries in kExtTable; not duplicated here.
    if (filename.find(L"git-rebase-todo") != std::wstring::npos)
        return "git_rebase";

    return {};
}

}  // namespace wlx::plugin_colorizer::language
