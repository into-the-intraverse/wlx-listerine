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

    // Returns compiled query for the language, lazily compiled from highlights.scm.
    // Returns nullptr if language not available or query fails to compile.
    // Returned pointer is valid for the lifetime of this GrammarRegistry.
    const TSQuery* get_query(const std::string& language);

    // Parse source code with the given language grammar. Returns a new tree.
    // Caller owns the returned tree and must call ts_tree_delete().
    // Returns nullptr if language not available.
    TSTree* parse(const std::string& language, const std::string& source);

private:
    void scan_directory();
    std::string resolve_query_source(const std::string& language, int depth = 0);

    std::wstring grammar_dir_;

    struct GrammarEntry {
        std::wstring dll_path;
        HMODULE handle = nullptr;
        const TSLanguage* language = nullptr;
        bool load_attempted = false;
        std::string query_source;
        TSQuery* query = nullptr;
        bool query_compiled = false;
    };

    std::unordered_map<std::string, GrammarEntry> grammars_;
};
