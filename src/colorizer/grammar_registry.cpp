#define NOMINMAX
#include "grammar_registry.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

using GrammarFn = const TSLanguage* (*)();

GrammarRegistry::GrammarRegistry(const std::wstring& grammar_dir)
    : grammar_dir_(grammar_dir) {
    scan_directory();
}

GrammarRegistry::~GrammarRegistry() {
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
        if (filename.size() < 16) continue;
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
    if (entry.load_attempted) return nullptr;

    entry.load_attempted = true;
    entry.handle = LoadLibraryW(entry.dll_path.c_str());
    if (!entry.handle) return nullptr;

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
