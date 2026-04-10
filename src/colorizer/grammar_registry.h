#pragma once

#include <tree_sitter/api.h>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class GrammarRegistry {
public:
    explicit GrammarRegistry(const std::wstring& grammar_dir);
    ~GrammarRegistry();

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

    std::unordered_map<std::string, GrammarEntry> grammars_;
};
